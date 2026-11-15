#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Lints every shell script with shellcheck and every Python file with pyflakes.
# Finds files by name and shebang, skipping caches, the venv and vendored code.
# Writes results/host/lint.json and exits non-zero on any finding.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"
PY="${ROOT}/.venv/bin/python3"
OUT_DIR="${ROOT}/results/host"
mkdir -p "${OUT_DIR}"

if [[ ! -x "${PY}" ]]; then
	"${ROOT}/scripts/setup_host_venv.sh"
fi

# Tracked or untracked files, minus ignored ones (so .cache/ and .venv/ are never scanned).
list_files() {
	git ls-files --cached --others --exclude-standard | grep -v '^thermostat/third_party/' || true
}

shell_files=()
python_files=()
while IFS= read -r f; do
	[[ -f "${f}" ]] || continue
	case "${f}" in
	*.sh) shell_files+=("${f}") ;;
	*.py) python_files+=("${f}") ;;
	*)
		# Extension-less scripts such as init scripts: detect by shebang.
		if head -n1 "${f}" 2> /dev/null | grep -qE '^#!.*(bash|/sh)'; then
			shell_files+=("${f}")
		fi
		;;
	esac
done < <(list_files)

sh_status=0
py_status=0

echo "[lint] shellcheck on ${#shell_files[@]} shell file(s)"
if [[ "${#shell_files[@]}" -gt 0 ]]; then
	shellcheck -x "${shell_files[@]}" || sh_status=$?
fi

echo "[lint] pyflakes on ${#python_files[@]} Python file(s)"
if [[ "${#python_files[@]}" -gt 0 ]]; then
	"${PY}" -m pyflakes "${python_files[@]}" || py_status=$?
fi

ok=true
if [[ "${sh_status}" -ne 0 || "${py_status}" -ne 0 ]]; then
	ok=false
fi
printf '{"schema": 1, "shell_files": %d, "python_files": %d, "shellcheck_ok": %s, "pyflakes_ok": %s}\n' \
	"${#shell_files[@]}" "${#python_files[@]}" \
	"$([[ ${sh_status} -eq 0 ]] && echo true || echo false)" \
	"$([[ ${py_status} -eq 0 ]] && echo true || echo false)" > "${OUT_DIR}/lint.json"

if [[ "${ok}" != true ]]; then
	echo "[lint] FAILED"
	exit 1
fi
echo "[lint] clean"
