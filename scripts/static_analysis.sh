#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Kernel static analysis of driver/ (Phase 4): checkpatch.pl (coding style and common mistakes) and
# sparse (kernel type checker, via the kernel build with C=2) against the pinned kernel source.
# Writes results/host/static_analysis.json; exits non-zero on any error or non-allowed warning.
# Needs the built kernel tree, so it runs inside "make test-qemu" after the build.
set -euo pipefail

# shellcheck source=scripts/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

# checkpatch warning types that are accepted, each justified in docs/DECISIONS.md (D-019).
ALLOWED_TYPES=""

KSRC="${BR_OUT}/build/linux-$(sed -n 's/^BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE="\(.*\)"/\1/p' \
	"${BR_OUT}/.config")"
CROSS="${BR_OUT}/host/bin/aarch64-linux-"
SCRATCH="${ROOT}/results/tmp/sparse-driver"
OUT_JSON="${ROOT}/results/host/static_analysis.json"
LOG="${LOG_DIR}/static_analysis.log"

if [[ ! -x "${KSRC}/scripts/checkpatch.pl" ]]; then
	echo "[static] kernel source not found at ${KSRC}; run 'make build' first" >&2
	exit 1
fi
mkdir -p "${LOG_DIR}" "${ROOT}/results/host"
: > "${LOG}"

echo "[static] checkpatch.pl on driver/"
cp_out="$(perl "${KSRC}/scripts/checkpatch.pl" --no-tree --file --terse --show-types \
	"${ROOT}"/driver/*.c "${ROOT}"/driver/*.h || true)"
echo "${cp_out}" >> "${LOG}"
cp_errors="$(grep -c ': ERROR:' <<< "${cp_out}" || true)"
cp_warnings=0
cp_allowed=0
while IFS= read -r line; do
	[[ "${line}" == *": WARNING:"* || "${line}" == *": CHECK:"* ]] || continue
	type="$(sed -n 's/.*: \(WARNING\|CHECK\):\([A-Z_]*\):.*/\2/p' <<< "${line}")"
	if [[ -n "${ALLOWED_TYPES}" && " ${ALLOWED_TYPES} " == *" ${type} "* ]]; then
		cp_allowed=$((cp_allowed + 1))
	else
		cp_warnings=$((cp_warnings + 1))
		echo "[static]   ${line}"
	fi
done <<< "${cp_out}"

echo "[static] sparse (make C=2) on a scratch copy of driver/"
rm -rf "${SCRATCH}"
mkdir -p "$(dirname "${SCRATCH}")"
cp -r "${ROOT}/driver" "${SCRATCH}"
sp_out="$(make -C "${KSRC}" M="${SCRATCH}" ARCH=arm64 CROSS_COMPILE="${CROSS}" C=2 modules 2>&1)"
echo "${sp_out}" >> "${LOG}"
sp_checked="$(grep -c '^ *CHECK ' <<< "${sp_out}" || true)"
sp_warnings="$(grep -cE ': (warning|error):' <<< "${sp_out}" || true)"
grep -E ': (warning|error):' <<< "${sp_out}" | sed 's/^/[static]   /' || true
rm -rf "${SCRATCH}"

cat > "${OUT_JSON}" << EOF
{"schema": 1, "checkpatch_errors": ${cp_errors}, "checkpatch_warnings": ${cp_warnings},
 "checkpatch_allowed_warnings": ${cp_allowed}, "allowed_types": "${ALLOWED_TYPES}",
 "sparse_files_checked": ${sp_checked}, "sparse_warnings": ${sp_warnings}}
EOF
echo "[static] checkpatch: ${cp_errors} errors, ${cp_warnings} warnings (${cp_allowed} allowed);" \
	"sparse: ${sp_warnings} warnings in ${sp_checked} files"

if [[ "${sp_checked}" -eq 0 ]]; then
	echo "[static] FAILED: sparse checked no files (is sparse installed?)" >&2
	exit 1
fi
if [[ "${cp_errors}" -ne 0 || "${cp_warnings}" -ne 0 || "${sp_warnings}" -ne 0 ]]; then
	echo "[static] FAILED"
	exit 1
fi
echo "[static] clean"
