#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Boots the built image interactively (demo mode). Log in as root (no password).
# Modbus TCP port 502 inside the board is reachable from the host at 127.0.0.1:5020.
# Quit QEMU with Ctrl-A then X.
set -euo pipefail

# shellcheck source=scripts/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
# shellcheck source=scripts/qemu_common.sh
source "${ROOT}/scripts/qemu_common.sh"

require_images
qemu_build_args ""
echo "[run] booting QEMU (quit with Ctrl-A then X)"
exec qemu-system-aarch64 "${QEMU_ARGS[@]}"
