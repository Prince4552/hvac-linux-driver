#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Mutation testing (Phase 4): proves the guest tests catch real bugs. Each patch in
# tests/mutation/mutants/ breaks the driver on purpose; the script applies it, rebuilds the image,
# runs the full guest test suite and expects it to FAIL ("mutant killed"). The original driver
# source is always restored (trap), and the original image is rebuilt at the end.
# Writes results/host/mutation.json. Exits non-zero unless every mutant is killed.
set -euo pipefail

# shellcheck source=scripts/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

MUTANTS_DIR="${ROOT}/tests/mutation/mutants"
BACKUP="${ROOT}/results/tmp/mutation-driver-backup"
GUEST_BACKUP="${ROOT}/results/tmp/mutation-guest-results-backup"
OUT_JSON="${ROOT}/results/host/mutation.json"
mkdir -p "${LOG_DIR}" "${ROOT}/results/tmp" "${ROOT}/results/host"

restore_driver() {
	if [[ -d "${BACKUP}" ]]; then
		echo "[mutation] restoring the original driver source"
		rm -rf "${ROOT}/driver"
		cp -r "${BACKUP}" "${ROOT}/driver"
		rm -rf "${BACKUP}"
	fi
	# The mutant runs overwrite results/guest; put the real (unmutated) results back.
	if [[ -d "${GUEST_BACKUP}" ]]; then
		rm -rf "${ROOT}/results/guest"
		cp -r "${GUEST_BACKUP}" "${ROOT}/results/guest"
		rm -rf "${GUEST_BACKUP}"
	fi
}
trap restore_driver EXIT

rm -rf "${BACKUP}" "${GUEST_BACKUP}"
cp -r "${ROOT}/driver" "${BACKUP}"
cp -r "${ROOT}/results/guest" "${GUEST_BACKUP}"

total=0
killed=0
entries=()
for patch_file in "${MUTANTS_DIR}"/*.patch; do
	name="$(basename "${patch_file}" .patch)"
	total=$((total + 1))
	echo "[mutation] === ${name}: apply, rebuild, run guest tests (expect FAILURE)"
	rm -rf "${ROOT}/driver"
	cp -r "${BACKUP}" "${ROOT}/driver"
	patch -p1 -s -d "${ROOT}" < "${patch_file}"

	status="survived"
	if ! "${ROOT}/scripts/build.sh" > "${LOG_DIR}/mutation-${name}-build.log" 2>&1; then
		# A mutant must compile; a build failure says nothing about the tests.
		status="build-error"
	elif "${ROOT}/scripts/test_qemu.sh" > "${LOG_DIR}/mutation-${name}-test.log" 2>&1; then
		status="survived"
	else
		status="killed"
		killed=$((killed + 1))
	fi
	cp "${LOG_DIR}/qemu-console.log" "${LOG_DIR}/mutation-${name}-console.log" 2> /dev/null || true
	failed_tests="$(grep -c 'FAILED:' "${LOG_DIR}/mutation-${name}-test.log" 2> /dev/null || true)"
	echo "[mutation] ${name}: ${status} (${failed_tests:-0} failing tests)"
	entries+=("{\"mutant\": \"${name}\", \"status\": \"${status}\", \"failing_tests\": ${failed_tests:-0}}")
done

restore_driver
echo "[mutation] rebuilding the original image"
"${ROOT}/scripts/build.sh" > "${LOG_DIR}/mutation-restore-build.log" 2>&1

joined="$(IFS=,; echo "${entries[*]}")"
cat > "${OUT_JSON}" << EOF
{"schema": 1, "killed": ${killed}, "total": ${total}, "mutation_score": "${killed}/${total}",
 "mutants": [${joined}]}
EOF
echo "[mutation] score ${killed}/${total}"
if [[ "${killed}" -ne "${total}" || "${total}" -eq 0 ]]; then
	exit 1
fi
