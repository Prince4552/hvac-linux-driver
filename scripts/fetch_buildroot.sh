#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Downloads the pinned Buildroot release into .cache/, checks its SHA-256 against both the
# value pinned in scripts/common.sh (also in VERSIONS.md) and the official .sign file, then extracts it
# to .cache/buildroot-<version>/. Safe to re-run: does nothing if already extracted.
set -euo pipefail

# shellcheck source=scripts/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

BR_URL="https://buildroot.org/downloads"
CACHE="${CACHE_DIR}"
TARBALL="${CACHE}/buildroot-${BR_VERSION}.tar.xz"
SIGN="${TARBALL}.sign"
DEST="${BR_DIR}"

if [[ -f "${DEST}/Makefile" ]]; then
	echo "[fetch] Buildroot ${BR_VERSION} already present at ${DEST}"
	exit 0
fi

mkdir -p "${CACHE}"
cleanup() { rm -f "${TARBALL}.part"; }
trap cleanup EXIT

if [[ ! -f "${TARBALL}" ]]; then
	echo "[fetch] downloading buildroot-${BR_VERSION}.tar.xz"
	wget -q -O "${TARBALL}.part" "${BR_URL}/buildroot-${BR_VERSION}.tar.xz"
	mv "${TARBALL}.part" "${TARBALL}"
fi
echo "[fetch] downloading the official .sign file"
wget -q -O "${SIGN}" "${BR_URL}/buildroot-${BR_VERSION}.tar.xz.sign"

actual="$(sha256sum "${TARBALL}" | cut -d' ' -f1)"
signed="$(awk -v f="buildroot-${BR_VERSION}.tar.xz" '$1 == "SHA256:" && $3 == f {print $2}' "${SIGN}")"
echo "[fetch] sha256 actual: ${actual}"
echo "[fetch] sha256 pinned: ${BR_SHA256}"
echo "[fetch] sha256 signed: ${signed}"
if ! [[ "${actual}" == "${BR_SHA256}" && "${actual}" == "${signed}" ]]; then
	echo "[fetch] ERROR: checksum mismatch; deleting the download" >&2
	rm -f "${TARBALL}"
	exit 1
fi

echo "[fetch] extracting to ${DEST}"
tmp="${DEST}.tmp"
rm -rf "${tmp}"
mkdir -p "${tmp}"
tar -xf "${TARBALL}" -C "${tmp}" --strip-components=1
mv "${tmp}" "${DEST}"
echo "[fetch] done"
