# SPDX-License-Identifier: MIT
# Top-level commands for the project. Each target calls one script in scripts/, so the logic
# lives in readable shell files and CI runs exactly the same commands as a developer.
# Run "make help" for the list.

SHELL := /usr/bin/env bash

.PHONY: help check-env venv lint build run test-host test-qemu test-all metrics mutation

help:
	@echo "make check-env  - verify host tools, disk space, git identity, gh login"
	@echo "make venv       - create .venv with pinned Python tools"
	@echo "make lint       - shellcheck + pyflakes on all scripts"
	@echo "make build      - fetch pinned Buildroot and build the ARM64 image"
	@echo "make run        - boot the image interactively (demo mode)"
	@echo "make test-host  - fast host tests"
	@echo "make test-qemu  - boot QEMU in autotest mode and run all in-guest tests"
	@echo "make test-all   - test-host then test-qemu (the phase gate)"
	@echo "make metrics    - regenerate results/metrics.json and results/METRICS.md"
	@echo "make mutation   - mutation testing of the driver (slow)"

check-env:
	./scripts/check_env.sh

venv:
	./scripts/setup_host_venv.sh

lint: venv
	./scripts/lint.sh

# Placeholders: they fail on purpose so nobody mistakes them for a passing step.
build run test-host test-qemu test-all:
	@echo "make $@: added in Phase 1"; exit 1

metrics:
	@echo "make $@: added in Phase 7"; exit 1

mutation:
	@echo "make $@: added in Phase 4"; exit 1
