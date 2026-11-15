# Unity (vendored)

Unity is a small C unit-test framework by ThrowTheSwitch.org (MIT license, see `LICENSE.txt`).

| Item | Value |
| --- | --- |
| Version | v2.7.0 |
| Commit | `b6763fbd9cedfacaa89e2ad9fd00d615a234e355` |
| Source | https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.7.0.tar.gz |
| Archive SHA-256 | `e84eb301ca7967831e68b1728f911e87fa2d345d8ddb64f897bc2f2ee24a321c` |
| Files copied | `src/unity.c`, `src/unity.h`, `src/unity_internals.h`, `LICENSE.txt` (unchanged) |

It is compiled as its own library with default flags (no `-Werror`) and is excluded from `cppcheck`
and coverage (ENGINEERING_RULES.md section 4.1).
