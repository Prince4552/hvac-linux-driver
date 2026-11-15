# Decisions

Every choice the project plan leaves open is recorded here before the code relies on it. This keeps the
project explainable: a reader can see what was decided, why, and what else was considered.

Format (newest at the bottom):

```
## D-NNN: <short title>
- Date / phase:
- Context: what question came up
- Decision: what was chosen
- Reason: why (evidence, links to logs or docs)
- Alternatives considered:
- Consequences: what this affects, anything to revisit
```

## D-001: Plan documents are the source of truth
- Date / phase: project start
- Context: an earlier study guide ("Carrier Internship Prep: Project Guide") describes the same project
  more loosely.
- Decision: `docs/PROJECT_PLAN.md` and `docs/ENGINEERING_RULES.md` win wherever the two differ.
- Reason: the plan fixes exact interfaces, units and tests, which the guide does not.
- Alternatives considered: none.
- Consequences: example code in the guide (for example a 32-bit time field) is superseded by the plan.

## D-002: Repository root is `claude-code-kickoff/`
- Date / phase: 2026-11-15, Phase 0
- Context: CLAUDE.md names the project root as `/home/priyanshu/projects/linux-driver-project`, but the
  given files (CLAUDE.md, docs/) live in `/home/priyanshu/projects/linux-driver-project/claude-code-kickoff`.
- Decision: the git repository root is `.../linux-driver-project/claude-code-kickoff`, where CLAUDE.md is.
- Reason: section 3 of the plan puts CLAUDE.md at the repository root; moving the user's files is riskier
  than using them where they are. All scripts find the root relative to themselves, never by absolute path.
- Alternatives considered: moving everything one level up.
- Consequences: none for the code. The rules "not under /mnt" and "under /home" still hold.

## D-003: Host Ubuntu 26.04 accepted
- Date / phase: 2026-11-15, Phase 0
- Context: the plan lists Ubuntu 22.04 or 24.04; this host runs Ubuntu 26.04 LTS.
- Decision: `check_env.sh` checks tools and resources, not the release number.
- Reason: what matters is that the tools exist; all of them are installed. CI uses ubuntu-24.04 as planned.
- Alternatives considered: failing on an unlisted release (would block for no technical reason).
- Consequences: host and CI tool versions may differ slightly (for example cppcheck); both must be clean.

## D-004: Host Python requirements pin every package, including dependencies
- Date / phase: 2026-11-15, Phase 0
- Context: the plan pins pytest, pyflakes, gcovr and matplotlib but says nothing about their dependencies.
- Decision: `requirements-host.txt` is the full `pip freeze` of a clean install of pytest 9.2.0,
  pyflakes 4.1.0, gcovr 8.8 and matplotlib 3.11.2.
- Reason: "pinned versions only" applies to every download; pinning only the top level would let
  dependencies drift.
- Alternatives considered: top-level pins only; a lock tool (extra dependency).
- Consequences: upgrading a tool means regenerating the whole file.

## D-005: Windows `Zone.Identifier` files are git-ignored
- Date / phase: 2026-11-15, Phase 0
- Context: the given files arrived with `*:Zone.Identifier` companions (Windows download metadata).
- Decision: ignore them in `.gitignore`; do not delete the user's files.
- Reason: they carry no project content, and a colon in a file name breaks checkouts on Windows.
- Alternatives considered: deleting them.
- Consequences: none.

## D-006: Empty sub-folders hold `.gitkeep`; their parent README describes them
- Date / phase: 2026-11-15, Phase 0
- Context: git does not store empty folders; the plan asks for a short README in empty folders.
- Decision: each top-level area (`driver/`, `thermostat/`, `tests/`, `br2-external/`, `results/`,
  `examples/hello_module/`, `docs/phases/`, `docs/images/`) has a README describing its sub-folders;
  deeper empty sub-folders hold an empty `.gitkeep`. Each `.gitkeep` is removed once real files arrive.
- Reason: avoids a dozen near-identical one-line READMEs while keeping the layout explained.
- Alternatives considered: a README in every empty folder.
- Consequences: none.

## D-007: Buildroot 2026.02.3, checksum verified against the `.sign` file without GPG
- Date / phase: 2026-11-15, Phase 1
- Context: the plan asks for the newest point release of the newest LTS series and a SHA-256 check
  against the official `.sign` file.
- Decision: Buildroot 2026.02.3 (newest `2026.02.x` on buildroot.org on 2026-11-15). `fetch_buildroot.sh`
  requires the download's SHA-256 to equal both the value pinned in `scripts/common.sh` and the value in
  the `.sign` file. The PGP signature itself is not verified.
- Reason: the pinned value protects against a changed download even if the server were compromised;
  the `.sign` comparison satisfies the plan. GPG verification would need the maintainer's key fetched from
  a third site, adding a moving part with little extra protection once the checksum is pinned.
- Alternatives considered: full `gpg --verify` with a pinned key file.
- Consequences: upgrading Buildroot means updating `scripts/common.sh` and `VERSIONS.md` together.

## D-008: Defconfig differences from Buildroot's `qemu_aarch64_virt_defconfig`
- Date / phase: 2026-11-15, Phase 1
- Context: the plan says "start from `qemu_aarch64_virt_defconfig`, then set ...".
- Decision: kept the kernel version (6.18.7), kernel config file, patch dir, hash checking, DHCP on eth0
  and ext4 rootfs. Changed: Bootlin external toolchain instead of the internal one (so the
  `HOST_LINUX_HEADERS_CUSTOM_6_18` line is dropped: the toolchain brings its own headers); removed
  `BR2_PACKAGE_HOST_QEMU` (QEMU comes from the host package `qemu-system-arm`; building it would add
  time for nothing); removed the post-image script (it only writes a `start-qemu.sh` for Buildroot's own
  defconfig names and does nothing for ours). Added `BR2_PACKAGE_PYTHON3_PYEXPAT` (XML parsing for the
  JUnit report) and `BR2_PACKAGE_PYTHON3_ZLIB` (reading `/proc/config.gz`).
- Reason: smallest change that meets the plan; each removal saves build time without losing a feature.
- Alternatives considered: building host QEMU inside Buildroot (pins QEMU too, but adds ~15 minutes).
- Consequences: the QEMU version is the host's; record it in `build_info` if a QEMU problem ever appears.

## D-009: Kernel log buffer enlarged to 4 MB (`log_buf_len=4M`)
- Date / phase: 2026-11-15, Phase 1
- Context: each test finds its kernel-log lines by searching for a marker. Long tests (500 load/unload
  cycles) could push the marker out of the default 128 KB ring buffer.
- Decision: both QEMU command lines pass `log_buf_len=4M`. If a marker is still lost, the helper fails the
  test loudly instead of silently checking nothing.
- Reason: the log check must never pass by accident.
- Alternatives considered: changing `CONFIG_LOG_BUF_SHIFT` in the fragment (needs a kernel rebuild to tune).
- Consequences: uses 4 MB of the guest's 2048 MB.

## D-010: QEMU command line compared with Buildroot's reference
- Date / phase: 2026-11-15, Phase 1
- Context: the plan says to prefer Buildroot's generated start script as the reference and record differences.
- Decision: the reference (`board/qemu/aarch64-virt/readme.txt`) is used with these changes: `-smp 2`
  (instead of 1, so concurrency tests really run in parallel), `-m 2048` (KASAN needs memory),
  `hostfwd=tcp:127.0.0.1:5020-:502` (Modbus from the host), `log_buf_len=4M` (D-009). Autotest adds
  `-no-reboot -snapshot` and `panic=5 hvac_autotest=1`.
- Reason: `-snapshot` throws away the guest's disk writes, so every test run starts from the identical image
  and a crashed run cannot leave a half-written file system behind.
- Alternatives considered: copying the image before each run (slower, uses disk).
- Consequences: files created in the guest during `make test-qemu` do not persist (intended).

## D-011: Guest tests read the pinned kernel version from a copy of `VERSIONS.md`
- Date / phase: 2026-11-15, Phase 1
- Context: the smoke test must compare `uname -r` with the version recorded in `VERSIONS.md`.
- Decision: `post-build.sh` copies `VERSIONS.md` and `linux.fragment` into `/opt/hvac-tests/`; the test parses
  the `| Linux kernel | <version> |` row.
- Reason: one source of truth; no version number repeated in test code.
- Alternatives considered: hard-coding the version in the test.
- Consequences: keep the "Linux kernel" row format stable.

## D-012: `CONFIG_DEBUG_KERNEL=y` added to the kernel fragment
- Date / phase: 2026-11-15, Phase 1
- Context: the first smoke run showed `CONFIG_DEBUG_KMEMLEAK`, `CONFIG_PROVE_LOCKING` and
  `CONFIG_DEBUG_ATOMIC_SLEEP` missing from `/proc/config.gz`. In Linux 6.18 all three
  `depends on DEBUG_KERNEL` (`mm/Kconfig.debug`, `lib/Kconfig.debug`), which Buildroot's QEMU kernel
  config leaves unset, so the kernel configurator silently dropped them (pitfall in plan section 12).
- Decision: add `CONFIG_DEBUG_KERNEL=y` to `linux.fragment` (the smoke test now checks it too).
- Reason: it is the missing dependency; no other way to get kmemleak and lockdep.
- Alternatives considered: none.
- Consequences: the plan's fragment list (section 5.4) gains one line.

## D-013: I2C adapters are found under `/sys/bus/i2c/devices/i2c-N`
- Date / phase: 2026-11-15, Phase 1
- Context: the first smoke run could not find the i2c-stub adapter: Linux 6.18 no longer has the old
  `/sys/class/i2c-adapter` compatibility class.
- Decision: helpers and the demo init script scan `/sys/bus/i2c/devices/i2c-*/name` for
  `SMBus stub driver` (entries such as `1-0048` are client devices and are skipped).
- Reason: this is the current, documented location of adapters.
- Alternatives considered: `i2cdetect -l` output parsing (an extra tool and text format to depend on).
- Consequences: none.

## D-014: Boot-log exception for kmemleak's start-up line
- Date / phase: 2026-11-15, Phase 1
- Context: the clean-boot-log check matches the plain word `kmemleak` (ENGINEERING_RULES.md 7.5). With
  kmemleak enabled, every boot prints the informational line
  `kmemleak: Kernel memory leak detector initialized (mem pool available: N)`.
- Decision: the boot-log check allows exactly that line (anchored regular expression in
  `tests/guest/test_smoke.py`). No other boot line is excepted, and the per-test log checks have no
  exceptions at all (this line is printed only once, at boot).
- Reason: it reports that the checker started, not a problem. A real kmemleak report reads
  `kmemleak: N new suspected memory leaks` and would still fail the check.
- Alternatives considered: weakening the pattern to `kmemleak: .*leak` everywhere (would be broader).
- Consequences: none.
