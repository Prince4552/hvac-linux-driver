#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# The QEMU command line shared by run_qemu.sh (interactive) and test_qemu.sh (autotest), so both
# boot exactly the same virtual board. Sourced, not run. Reference: Buildroot's
# board/qemu/aarch64-virt/readme.txt; differences are listed in docs/DECISIONS.md (D-010).
# shellcheck disable=SC2034 # QEMU_ARGS is used by the scripts that source this file

# Usage: qemu_build_args "<extra kernel arguments>"  -> fills the QEMU_ARGS array.
qemu_build_args() {
	local extra_append="$1"
	local kernel_args="rootwait root=/dev/vda console=ttyAMA0 log_buf_len=4M ${extra_append}"
	QEMU_ARGS=(
		-M virt -cpu cortex-a53 -smp 2 -m 2048 -nographic
		-kernel "${IMAGES_DIR}/Image"
		-append "${kernel_args}"
		-netdev "user,id=eth0,hostfwd=tcp:127.0.0.1:5020-:502"
		-device "virtio-net-device,netdev=eth0"
		-drive "file=${IMAGES_DIR}/rootfs.ext4,if=none,format=raw,id=hd0"
		-device "virtio-blk-device,drive=hd0"
	)
}

require_images() {
	local f
	for f in "${IMAGES_DIR}/Image" "${IMAGES_DIR}/rootfs.ext4"; do
		if [[ ! -f "${f}" ]]; then
			echo "missing ${f}; run 'make build' first" >&2
			exit 1
		fi
	done
}
