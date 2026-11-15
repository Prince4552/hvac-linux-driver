#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Shared settings for the build and QEMU scripts: pinned Buildroot version, folder locations and
# a clean PATH. Sourced (not run) by other scripts, so every script uses the same values.
# shellcheck disable=SC2034 # variables are used by the scripts that source this file

BR_VERSION="2026.02.3"
BR_SHA256="5a59e7501b0b4ec52c41f4bfa79412320e0b37eae5f719605a258e8d0c6fc7fb"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE_DIR="${ROOT}/.cache"
BR_DIR="${CACHE_DIR}/buildroot-${BR_VERSION}"
BR_OUT="${CACHE_DIR}/br-output"
BR_EXTERNAL_DIR="${ROOT}/br2-external"
BR_DEFCONFIG_NAME="hvac_qemu_aarch64_defconfig"
IMAGES_DIR="${BR_OUT}/images"
LOG_DIR="${ROOT}/results/logs"

# WSL adds Windows folders (with spaces) to PATH, which Buildroot rejects. Use a clean PATH.
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export BR2_DL_DIR="${CACHE_DIR}/br-dl"

# Runs Buildroot's make with our output folder and external tree.
br_make() {
	make -C "${BR_DIR}" O="${BR_OUT}" BR2_EXTERNAL="${BR_EXTERNAL_DIR}" "$@"
}
