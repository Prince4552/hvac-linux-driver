#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Builds the ARM64 Linux image with Buildroot: fetches pinned Buildroot (first time), applies our
# defconfig, re-applies the kernel config fragment if it changed, rebuilds our local packages so
# source edits are picked up, and writes results/host/build_info.json. Logs to results/logs/build.log.
set -euo pipefail

# shellcheck source=scripts/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

mkdir -p "${LOG_DIR}" "${ROOT}/results/host" "${BR2_DL_DIR}"
BUILD_LOG="${LOG_DIR}/build.log"
exec > >(tee "${BUILD_LOG}") 2>&1

start_s="$(date +%s)"
echo "[build] $(date -u +%FT%TZ) starting"

"${ROOT}/scripts/fetch_buildroot.sh"

echo "[build] applying ${BR_DEFCONFIG_NAME}"
br_make "${BR_DEFCONFIG_NAME}"

# Buildroot does not notice edits to the kernel config fragment by itself. Keep a checksum and
# re-run the kernel configure step when it changes.
FRAGMENT="${BR_EXTERNAL_DIR}/board/hvac-qemu/linux.fragment"
STAMP="${BR_OUT}/.hvac-linux-fragment.sha256"
want="$(sha256sum "${FRAGMENT}" | cut -d' ' -f1)"
if [[ -f "${STAMP}" && "$(cat "${STAMP}")" != "${want}" ]]; then
	echo "[build] linux.fragment changed: reconfiguring the kernel"
	br_make linux-reconfigure
fi

# Local packages (SITE_METHOD = local) are copied once; force a fresh copy so edits are built.
for pkg_dir in "${BR_EXTERNAL_DIR}"/package/*/; do
	[[ -d "${pkg_dir}" ]] || continue
	pkg="$(basename "${pkg_dir}")"
	if compgen -G "${BR_OUT}/build/${pkg}-*" > /dev/null; then
		echo "[build] rebuilding local package ${pkg}"
		br_make "${pkg}-rebuild"
	fi
done

echo "[build] building the image"
br_make
echo "${want}" > "${STAMP}"

end_s="$(date +%s)"
duration_min="$(awk -v s="$((end_s - start_s))" 'BEGIN { printf "%.1f", s / 60 }')"
kernel_version="$(sed -n 's/^BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE="\(.*\)"/\1/p' "${BR_OUT}/.config")"
toolchain="$(grep -A1 '^config BR2_TOOLCHAIN_EXTERNAL_BOOTLIN_AARCH64_GLIBC_STABLE$' \
	"${BR_DIR}/toolchain/toolchain-external/toolchain-external-bootlin/Config.in.options" |
	sed -n 's/^[[:space:]]*bool "\(.*\)"/Bootlin \1/p')"
gcc_version="$("${BR_OUT}/host/bin/aarch64-linux-gcc" -dumpfullversion)"
image_bytes="$(stat -c %s "${IMAGES_DIR}/Image")"
# The ext4 image file is sparse: report the space it really uses, not its 768 MB nominal size.
rootfs_used_mb="$(du -m -L "${IMAGES_DIR}/rootfs.ext4" | cut -f1)"

cat > "${ROOT}/results/host/build_info.json" << EOF
{"schema": 1, "buildroot_version": "${BR_VERSION}", "kernel_version": "${kernel_version}",
 "toolchain": "${toolchain}", "gcc_version": "${gcc_version}",
 "build_duration_min": ${duration_min}, "kernel_image_bytes": ${image_bytes},
 "rootfs_size_mb": ${rootfs_used_mb}}
EOF
echo "[build] done in ${duration_min} min; image ${image_bytes} bytes, rootfs ${rootfs_used_mb} MB used"
