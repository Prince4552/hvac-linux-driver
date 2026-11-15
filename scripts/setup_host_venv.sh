#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Creates the host Python virtual environment (.venv) from requirements-host.txt.
# Every package is pinned to an exact version so results are reproducible.
# Safe to re-run: an up-to-date .venv is left as it is.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV="${ROOT}/.venv"
REQ="${ROOT}/requirements-host.txt"
STAMP="${VENV}/.requirements.sha256"

want="$(sha256sum "${REQ}" | cut -d' ' -f1)"
if [[ -x "${VENV}/bin/python3" && -f "${STAMP}" && "$(cat "${STAMP}")" == "${want}" ]]; then
	echo "[venv] up to date: ${VENV}"
	exit 0
fi

echo "[venv] creating ${VENV}"
python3 -m venv "${VENV}"
echo "[venv] installing pinned packages from requirements-host.txt"
"${VENV}/bin/python3" -m pip install --quiet --disable-pip-version-check --require-virtualenv \
	-r "${REQ}"
echo "${want}" > "${STAMP}"
echo "[venv] done"
