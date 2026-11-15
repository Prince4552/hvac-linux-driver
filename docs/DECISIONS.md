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
