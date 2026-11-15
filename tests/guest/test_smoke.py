# SPDX-License-Identifier: MIT
"""Platform smoke tests (Phase 1): the emulated board boots with every tool and kernel option the
later tests depend on. If one of these fails, every later failure is suspect, so they come first.
"""
import gzip
import os
import re

import pytest

import hvac_test_lib as h

HERE = os.path.dirname(os.path.abspath(__file__))

# Boot-log lines that match a "bad" pattern but are expected; each one is justified in
# docs/DECISIONS.md (decision number in the comment). Regular expressions.
BOOT_LOG_ALLOWED: tuple[str, ...] = (
    r"^kmemleak: Kernel memory leak detector initialized \(mem pool available: \d+\)$",  # D-014
)


def fragment_lines() -> list[str]:
    """Lines of linux.fragment (copied next to the tests by post-build.sh)."""
    with open(os.path.join(HERE, "linux.fragment"), encoding="utf-8") as f:
        return [line.strip() for line in f if line.strip()]


def running_config() -> str:
    with gzip.open("/proc/config.gz", "rt", encoding="utf-8") as f:
        return f.read()


def pinned_kernel_version() -> str:
    """The kernel version from the 'Linux kernel' row of VERSIONS.md."""
    with open(os.path.join(HERE, "VERSIONS.md"), encoding="utf-8") as f:
        for line in f:
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cells) >= 2 and cells[0] == "Linux kernel":
                return cells[1]
    raise AssertionError("no 'Linux kernel' row in VERSIONS.md")


def test_machine_is_aarch64() -> None:
    assert os.uname().machine == "aarch64"


def test_kernel_version_matches_versions_md() -> None:
    assert os.uname().release == pinned_kernel_version()


@pytest.mark.parametrize("line", fragment_lines())
def test_kernel_config_option_took_effect(line: str) -> None:
    config = running_config()
    unset = re.fullmatch(r"# (CONFIG_\w+) is not set", line)
    if unset:
        assert not re.search(rf"^{unset.group(1)}=", config, re.MULTILINE), \
            f"{unset.group(1)} should not be set"
    else:
        assert re.search(rf"^{re.escape(line)}$", config, re.MULTILINE), \
            f"{line} missing from /proc/config.gz (dropped by unmet dependency?)"


def test_debugfs_is_mounted() -> None:
    with open("/proc/mounts", encoding="utf-8") as f:
        assert any(line.split()[1:3] == ["/sys/kernel/debug", "debugfs"] for line in f)


@pytest.mark.parametrize("path", ["/sys/kernel/debug/kmemleak", "/sys/kernel/debug/kunit"])
def test_debugfs_entry_exists(path: str) -> None:
    assert os.path.exists(path)


def test_i2c_stub_round_trip_and_clean_unload() -> None:
    h.unload_stub()
    bus = h.load_stub(0x48)
    try:
        assert bus is not None
        h.set_chip_reg(0x02, 0x1234)
        assert h.get_chip_reg(0x02) == 0x1234
    finally:
        h.unload_stub()
    assert not h.module_loaded("i2c_stub")
    assert h.stub_bus() is None


@pytest.mark.parametrize("tool", ["python3", "i2cset", "i2cget", "i2cdetect"])
def test_tool_is_present(tool: str) -> None:
    assert any(os.access(os.path.join(d, tool), os.X_OK)
               for d in os.environ.get("PATH", "/usr/bin:/bin:/usr/sbin:/sbin").split(":"))


def test_pytest_is_present() -> None:
    version = h.run(["python3", "-c", "import pytest; print(pytest.__version__)"]).strip()
    assert re.fullmatch(r"\d+\.\d+.*", version)


def test_boot_log_is_clean() -> None:
    """Kernel log from boot up to the first test marker has no unexpected problem lines."""
    lines = h.kernel_log()
    first_marker = next((i for i, line in enumerate(lines) if h.MARKER_PREFIX in line),
                        len(lines))
    h.assert_kernel_log_clean(lines[:first_marker], BOOT_LOG_ALLOWED)
