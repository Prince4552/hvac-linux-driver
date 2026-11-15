#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Boots the image in autotest mode: the guest runs all in-guest tests, prints the results between
# markers on the serial console and powers off. Saves the console to results/logs/qemu-console.log,
# extracts results/guest/summary.json, guest_metrics.json and results/logs/guest-dmesg.log, and
# exits non-zero on a missing marker, a non-zero pytest exit code, or any failed/error/skipped test.
# Optional: HVAC_PYTEST_K=<expr> limits the tests (development only, never in a gate).
set -euo pipefail

# shellcheck source=scripts/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
# shellcheck source=scripts/qemu_common.sh
source "${ROOT}/scripts/qemu_common.sh"

TIMEOUT="60m"
CONSOLE_LOG="${LOG_DIR}/qemu-console.log"

require_images
mkdir -p "${LOG_DIR}" "${ROOT}/results/guest"
rm -f "${ROOT}/results/guest/summary.json" "${ROOT}/results/guest/guest_metrics.json"

extra="panic=5 hvac_autotest=1"
if [[ -n "${HVAC_PYTEST_K:-}" ]]; then
	echo "[test-qemu] WARNING: limiting tests to -k ${HVAC_PYTEST_K} (not valid for a gate)"
	extra="${extra} hvac_pytest_k=${HVAC_PYTEST_K}"
fi
qemu_build_args "${extra}"

echo "[test-qemu] booting QEMU in autotest mode (timeout ${TIMEOUT}); console -> ${CONSOLE_LOG}"
start_s="$(date +%s)"
qemu_rc=0
# -snapshot: the guest's disk writes are thrown away, so every run starts from the same image.
timeout "${TIMEOUT}" qemu-system-aarch64 "${QEMU_ARGS[@]}" -no-reboot -snapshot \
	< /dev/null > "${CONSOLE_LOG}" 2>&1 || qemu_rc=$?
echo "[test-qemu] QEMU exited with ${qemu_rc} after $(($(date +%s) - start_s)) s"
if [[ "${qemu_rc}" -eq 124 ]]; then
	echo "[test-qemu] ERROR: timed out after ${TIMEOUT}" >&2
fi

python3 "${ROOT}/scripts/extract_guest_results.py" "${CONSOLE_LOG}" \
	"${ROOT}/results/guest" "${LOG_DIR}/guest-dmesg.log"
