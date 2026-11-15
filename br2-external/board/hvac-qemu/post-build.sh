#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Buildroot post-build hook: copies the guest tests (tests/guest, tests/common) and the reference
# files they check against (VERSIONS.md, linux.fragment) into /opt/hvac-tests in the target image.
# Buildroot calls it with TARGET_DIR as $1 and exports BR2_EXTERNAL_HVAC_PATH.
set -euo pipefail

TARGET_DIR="${1:?usage: post-build.sh TARGET_DIR}"
if [[ -z "${BR2_EXTERNAL_HVAC_PATH:-}" || ! -d "${BR2_EXTERNAL_HVAC_PATH}" ]]; then
	echo "post-build.sh: BR2_EXTERNAL_HVAC_PATH is not set or not a directory" >&2
	exit 1
fi
REPO="$(cd "${BR2_EXTERNAL_HVAC_PATH}/.." && pwd)"
DEST="${TARGET_DIR}/opt/hvac-tests"

echo "post-build.sh: installing guest tests into ${DEST}"
rm -rf "${DEST}"
mkdir -p "${DEST}"
# Copy file by file (no caches); tests/common files land next to the guest tests so imports work.
find "${REPO}/tests/guest" "${REPO}/tests/common" -maxdepth 1 -type f \
	\( -name '*.py' -o -name '*.sh' -o -name '*.ini' \) -exec cp {} "${DEST}/" \;
chmod 0755 "${DEST}/run_guest_tests.sh"
cp "${REPO}/VERSIONS.md" "${DEST}/VERSIONS.md"
cp "${BR2_EXTERNAL_HVAC_PATH}/board/hvac-qemu/linux.fragment" "${DEST}/linux.fragment"
