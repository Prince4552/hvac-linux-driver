#!/bin/sh
# SPDX-License-Identifier: MIT
# Guest test runner (runs inside QEMU, started by /etc/init.d/S99autotest). Runs pytest on
# /opt/hvac-tests and prints the results between fixed markers on the console, where
# scripts/test_qemu.sh on the host picks them up. Contract: PROJECT_PLAN.md section 4.8.
set -eu

TEST_DIR=/opt/hvac-tests
RES_DIR=/tmp/hvac-results
export HVAC_RESULTS_DIR="${RES_DIR}"

# Stop kernel messages from printing on the console so they cannot break the result block.
dmesg -n 1
if ! grep -q " /sys/kernel/debug debugfs " /proc/mounts; then
	mount -t debugfs none /sys/kernel/debug
fi

rm -rf "${RES_DIR}"
mkdir -p "${RES_DIR}/metrics"

cd "${TEST_DIR}"
python3 -c 'import hvac_test_lib as h; h.record_boot_metric()'

# Optional development filter: hvac_pytest_k=<expr> on the kernel command line (never in a gate).
k_expr="$(tr ' ' '\n' < /proc/cmdline | sed -n 's/^hvac_pytest_k=//p')"

set +e
if [ -n "${k_expr}" ]; then
	echo "run_guest_tests: limiting tests with -k ${k_expr}"
	python3 -m pytest -p no:cacheprovider -q --junitxml="${RES_DIR}/junit.xml" -k "${k_expr}" "${TEST_DIR}"
else
	python3 -m pytest -p no:cacheprovider -q --junitxml="${RES_DIR}/junit.xml" "${TEST_DIR}"
fi
rc=$?
set -e

python3 "${TEST_DIR}/make_summary.py" "${RES_DIR}" > "${RES_DIR}/summary.json" ||
	echo '{"schema":1,"passed":0,"failed":0,"errors":1,"skipped":0,"tests":[],"metrics":{}}' \
		> "${RES_DIR}/summary.json"

echo "===HVAC-RESULTS-BEGIN==="
cat "${RES_DIR}/summary.json"
echo "===HVAC-RESULTS-END==="
echo "===HVAC-DMESG-BEGIN==="
dmesg
echo "===HVAC-DMESG-END==="
echo "===HVAC-AUTOTEST-DONE exit=${rc}==="
sync
poweroff -f
