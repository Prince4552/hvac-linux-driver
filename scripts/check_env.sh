#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Checks that this host can build and test the project: location, tools, libraries, disk,
# memory, git identity and GitHub login. Prints a pass/fail table and, for anything missing,
# the exact apt command to install it. Exits non-zero if any check fails. Never runs sudo.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MIN_DISK_GB=25
WARN_RAM_GB=8

FAILS=0
MISSING_PKGS=()

row() { # status, check, detail
	printf '%-5s | %-28s | %s\n' "$1" "$2" "$3"
}
pass() { row "PASS" "$1" "$2"; }
warn() { row "WARN" "$1" "$2"; }
fail() { row "FAIL" "$1" "$2"; FAILS=$((FAILS + 1)); }

# command name -> apt package that provides it
COMMANDS=(
	"gcc:build-essential" "g++:g++" "make:make" "git:git" "bison:bison" "flex:flex" "bc:bc"
	"rsync:rsync" "cpio:cpio" "unzip:unzip" "wget:wget" "file:file" "patch:patch" "perl:perl"
	"xz:xz-utils" "bzip2:bzip2" "python3:python3" "pip3:python3-pip"
	"qemu-system-aarch64:qemu-system-arm" "cmake:cmake" "pkg-config:pkg-config" "ccache:ccache"
	"jq:jq" "valgrind:valgrind" "cppcheck:cppcheck" "sparse:sparse" "shellcheck:shellcheck"
	"mosquitto_pub:mosquitto-clients" "gh:gh"
)
# Packages without a single command to test; checked with dpkg-query.
LIBRARY_PKGS=(libncurses-dev libssl-dev libelf-dev libmodbus-dev libmosquitto-dev python3-venv mosquitto)

echo "Host environment check for ${ROOT}"
echo
printf '%-5s | %-28s | %s\n' "Result" "Check" "Detail"
echo "------+------------------------------+-------------------------------------------"

case "${ROOT}" in
/mnt/*) fail "project location" "${ROOT} is under /mnt (slow, wrong permissions); move it to /home" ;;
*) pass "project location" "${ROOT}" ;;
esac

for entry in "${COMMANDS[@]}"; do
	cmd="${entry%%:*}"
	pkg="${entry##*:}"
	if command -v "${cmd}" > /dev/null 2>&1; then
		pass "command ${cmd}" "$(command -v "${cmd}")"
	else
		fail "command ${cmd}" "missing (apt package ${pkg})"
		MISSING_PKGS+=("${pkg}")
	fi
done

for pkg in "${LIBRARY_PKGS[@]}"; do
	if dpkg-query -W -f='${Status}' "${pkg}" 2> /dev/null | grep -q "install ok installed"; then
		pass "package ${pkg}" "installed"
	else
		fail "package ${pkg}" "missing"
		MISSING_PKGS+=("${pkg}")
	fi
done

free_gb="$(df -BG --output=avail "${ROOT}" | tail -1 | tr -dc '0-9')"
if [[ "${free_gb}" -ge "${MIN_DISK_GB}" ]]; then
	pass "free disk" "${free_gb} GB (need ${MIN_DISK_GB})"
else
	fail "free disk" "${free_gb} GB, need at least ${MIN_DISK_GB} GB"
fi

ram_kb="$(awk '/^MemTotal:/ {print $2}' /proc/meminfo)"
ram_gb=$((ram_kb / 1024 / 1024))
if [[ "${ram_gb}" -ge "${WARN_RAM_GB}" ]]; then
	pass "RAM" "${ram_gb} GB"
else
	warn "RAM" "${ram_gb} GB; ${WARN_RAM_GB} GB recommended (builds will be slower)"
fi

git_name="$(git config user.name || true)"
git_email="$(git config user.email || true)"
if [[ -n "${git_name}" && -n "${git_email}" ]]; then
	pass "git identity" "user.name and user.email set"
else
	fail "git identity" "run: git config --global user.name 'Your Name'; git config --global user.email you@example.com"
fi

if command -v gh > /dev/null 2>&1 && gh auth status > /dev/null 2>&1; then
	pass "GitHub CLI login" "gh auth status OK"
else
	fail "GitHub CLI login" "run: gh auth login"
fi

echo
if [[ "${#MISSING_PKGS[@]}" -gt 0 ]]; then
	echo "Install the missing packages with:"
	echo "  sudo apt update && sudo apt install -y $(printf '%s\n' "${MISSING_PKGS[@]}" | sort -u | tr '\n' ' ')"
	echo
fi

if [[ "${FAILS}" -gt 0 ]]; then
	echo "check-env: FAILED (${FAILS} check(s))"
	exit 1
fi
echo "check-env: all checks passed"
