#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Fast host tests (no QEMU). Creates .venv first if missing, so CI can call this script directly.
# Steps: lint, cppcheck, unit tests (normal, ASan+UBSan, Valgrind), coverage with thresholds,
# software-in-the-loop simulation with checks and plot, daemon integration tests (pytest). Each step writes a small JSON file to
# results/host/; host_summary.json totals them. Exits non-zero on the first failing step.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PY="${ROOT}/.venv/bin/python3"
SRC="${ROOT}/thermostat"
OUT="${ROOT}/results/host"
LOGS="${ROOT}/results/logs"
BUILD="${ROOT}/build"
# Unit-test executables (CMake target names).
UNIT_TESTS=(test_hvac_core test_config test_sensor_iio test_modbus_regs)
# Coverage thresholds (PROJECT_PLAN.md Phase 5): file:min_line_pct:min_branch_pct
COVERAGE_TARGETS=("hvac_core.c:95:90" "config.c:90:0")

mkdir -p "${OUT}" "${LOGS}" "${BUILD}" "${ROOT}/results/sil" "${ROOT}/results/tmp"
"${ROOT}/scripts/setup_host_venv.sh"

step() { echo "[test-host] step: $*"; }

configure_and_build() { # dir, extra cmake args...
	local dir="$1"
	shift
	cmake -S "${SRC}" -B "${dir}" -DCMAKE_BUILD_TYPE=Debug "$@" > "${dir}.cmake.log" 2>&1 ||
		{ cat "${dir}.cmake.log"; return 1; }
	cmake --build "${dir}" -j "$(nproc)" > "${dir}.build.log" 2>&1 ||
		{ cat "${dir}.build.log"; return 1; }
}

# Runs every unit test in a build dir, optionally under a wrapper; prints "passed failed ignored".
run_unit_tests() { # build dir, log prefix, wrapper...
	local dir="$1" prefix="$2"
	shift 2
	local passed=0 failed=0 ignored=0 t log line
	for t in "${UNIT_TESTS[@]}"; do
		log="${LOGS}/${prefix}-${t}.log"
		if ! "$@" "${dir}/${t}" > "${log}" 2>&1; then
			echo "[test-host]   ${t} FAILED (${log})" >&2
			failed=$((failed + 1))
		fi
		line="$(grep -E '^[0-9]+ Tests [0-9]+ Failures [0-9]+ Ignored' "${log}" || echo "0 Tests 1 Failures 0 Ignored")"
		passed=$((passed + $(awk '{print $1 - $3 - $5}' <<< "${line}")))
		ignored=$((ignored + $(awk '{print $5}' <<< "${line}")))
	done
	echo "${passed} ${failed} ${ignored}"
}

step "lint"
"${ROOT}/scripts/lint.sh"

step "cppcheck"
cpp_log="${LOGS}/cppcheck.log"
cpp_rc=0
cppcheck --enable=warning,style,performance,portability --std=c11 --error-exitcode=1 -q \
	-I "${SRC}/include" -i "${SRC}/third_party" --suppress=missingIncludeSystem \
	--template='{file}:{line}: {severity}: {id}: {message}' "${SRC}" > "${cpp_log}" 2>&1 || cpp_rc=$?
cpp_findings="$(grep -c ': \(error\|warning\|style\|performance\|portability\): ' "${cpp_log}" || true)"
echo "{\"schema\": 1, \"cppcheck_findings\": ${cpp_findings}}" > "${OUT}/cppcheck.json"
if [[ "${cpp_rc}" -ne 0 || "${cpp_findings}" -ne 0 ]]; then
	cat "${cpp_log}"
	exit 1
fi

step "unit tests (normal build)"
configure_and_build "${BUILD}/thermostat"
read -r passed failed ignored <<< "$(run_unit_tests "${BUILD}/thermostat" unit)"
echo "{\"schema\": 1, \"passed\": ${passed}, \"failed\": ${failed}, \"ignored\": ${ignored}}" \
	> "${OUT}/unit_tests.json"
echo "[test-host]   ${passed} passed, ${failed} failed, ${ignored} ignored"
[[ "${failed}" -eq 0 && "${ignored}" -eq 0 && "${passed}" -gt 0 ]]

step "unit tests (AddressSanitizer + UndefinedBehaviorSanitizer)"
configure_and_build "${BUILD}/thermostat-asan" -DHVAC_SANITIZE=ON
read -r s_passed s_failed s_ignored <<< "$(ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	run_unit_tests "${BUILD}/thermostat-asan" asan)"
san_errors="$(cat "${LOGS}"/asan-*.log | grep -cE 'ERROR: (Address|Leak)Sanitizer|runtime error:' || true)"
echo "{\"schema\": 1, \"passed\": ${s_passed}, \"failed\": ${s_failed}, \"sanitizer_errors\": ${san_errors}}" \
	> "${OUT}/sanitizers.json"
echo "[test-host]   ${s_passed} passed, ${s_failed} failed, ${san_errors} sanitizer reports"
[[ "${s_failed}" -eq 0 && "${s_ignored}" -eq 0 && "${san_errors}" -eq 0 ]]

step "unit tests and simulator under Valgrind memcheck"
VG=(valgrind --leak-check=full "--errors-for-leak-kinds=definite,indirect" --error-exitcode=1)
read -r v_passed v_failed _ <<< "$(run_unit_tests "${BUILD}/thermostat" valgrind "${VG[@]}")"
mkdir -p "${ROOT}/results/tmp/sil-valgrind"
"${VG[@]}" "${BUILD}/thermostat/sil" "${ROOT}/results/tmp/sil-valgrind" \
	> "${LOGS}/valgrind-sil.log" 2>&1 || v_failed=$((v_failed + 1))
vg_errors="$(cat "${LOGS}"/valgrind-*.log | sed -n 's/.*ERROR SUMMARY: \([0-9]*\) errors.*/\1/p' |
	awk '{s += $1} END {print s + 0}')"
vg_leaked="$(cat "${LOGS}"/valgrind-*.log |
	sed -n 's/.*\(definitely\|indirectly\) lost: \([0-9,]*\) bytes.*/\2/p' | tr -d ',' |
	awk '{s += $1} END {print s + 0}')"
echo "{\"schema\": 1, \"passed\": ${v_passed}, \"failed\": ${v_failed}, \"valgrind_errors\": ${vg_errors}, \"valgrind_leaked_bytes\": ${vg_leaked}}" \
	> "${OUT}/valgrind.json"
echo "[test-host]   ${v_passed} passed, ${v_failed} failed, ${vg_errors} errors, ${vg_leaked} bytes leaked"
[[ "${v_failed}" -eq 0 && "${vg_errors}" -eq 0 && "${vg_leaked}" -eq 0 ]]

step "coverage (gcov + gcovr)"
rm -rf "${BUILD}/thermostat-cov"
configure_and_build "${BUILD}/thermostat-cov" -DHVAC_COVERAGE=ON
run_unit_tests "${BUILD}/thermostat-cov" cov > /dev/null
"${PY}" -m gcovr --root "${SRC}" --object-directory "${BUILD}/thermostat-cov" \
	--filter "${SRC}/src/" --json-summary "${OUT}/coverage_raw.json" --txt "${LOGS}/coverage.txt" \
	> /dev/null
"${PY}" "${ROOT}/scripts/check_coverage.py" "${OUT}/coverage_raw.json" "${OUT}/coverage.json" \
	"${COVERAGE_TARGETS[@]}"
rm -f "${OUT}/coverage_raw.json"

step "software-in-the-loop simulation"
"${BUILD}/thermostat/sil" "${ROOT}/results/sil" > "${LOGS}/sil.log"
"${PY}" "${ROOT}/scripts/check_sil.py" "${ROOT}/results/sil/sil_metrics.json" "${OUT}/sil_check.json"
"${PY}" "${ROOT}/scripts/plot_sil.py" "${ROOT}/results/sil" "${ROOT}/docs/images/sil_hot_day.png"

step "daemon integration tests (pytest, fake sysfs, Modbus, Valgrind)"
pytest_rc=0
HVACD_BIN="${BUILD}/thermostat/hvacd" "${PY}" -m pytest -p no:cacheprovider -q \
	--junitxml="${ROOT}/results/tmp/pytest_host.xml" "${ROOT}/tests/host" || pytest_rc=$?
"${PY}" "${ROOT}/scripts/junit_counts.py" "${ROOT}/results/tmp/pytest_host.xml" \
	"${OUT}/pytest_host.json"
[[ "${pytest_rc}" -eq 0 ]]

"${PY}" "${ROOT}/scripts/host_summary.py" "${OUT}"
echo "[test-host] all host checks passed"
