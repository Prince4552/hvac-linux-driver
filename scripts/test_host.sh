#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Fast host tests. Creates .venv first if missing, so CI can call this script directly.
# Phase 1: lint only. Later phases add unit tests, sanitizers, Valgrind, coverage and simulation.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"${ROOT}/scripts/setup_host_venv.sh"

echo "[test-host] step: lint"
"${ROOT}/scripts/lint.sh"

echo "[test-host] all host checks passed"
