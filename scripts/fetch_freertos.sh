#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Fetches the pinned FreeRTOS-Kernel release (Ext 4) into .cache/ with a shallow git clone of its tag,
# and checks that the tag points at the pinned commit (VERSIONS.md, decision D-030).
# Safe to re-run: an existing checkout is only re-checked. Never edit files under .cache/.
set -euo pipefail

# shellcheck source=scripts/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

FREERTOS_TAG="V11.3.3"
FREERTOS_COMMIT="89592801dd8b73329a7583719b7e3567b50910e5"
FREERTOS_URL="https://github.com/FreeRTOS/FreeRTOS-Kernel.git"
DEST="${CACHE_DIR}/FreeRTOS-Kernel-${FREERTOS_TAG}"
TMP="${DEST}.tmp"

cleanup() { rm -rf "${TMP}"; }
trap cleanup EXIT

if [[ ! -d "${DEST}/.git" ]]; then
	echo "[freertos] cloning ${FREERTOS_URL} at ${FREERTOS_TAG}"
	rm -rf "${TMP}"
	mkdir -p "${CACHE_DIR}"
	git -c advice.detachedHead=false clone --quiet --depth 1 --branch "${FREERTOS_TAG}" \
		"${FREERTOS_URL}" "${TMP}"
	mv "${TMP}" "${DEST}"
fi

actual="$(git -C "${DEST}" rev-parse HEAD)"
echo "[freertos] commit actual: ${actual}"
echo "[freertos] commit pinned: ${FREERTOS_COMMIT}"
if [[ "${actual}" != "${FREERTOS_COMMIT}" ]]; then
	echo "[freertos] ERROR: commit mismatch; delete ${DEST} and re-run" >&2
	exit 1
fi
echo "[freertos] ready at ${DEST}"
