# Pinned versions

Every external download uses an exact version (or commit) recorded here, so anyone can reproduce
the exact same build. "Why pin?" A floating version ("latest") can change under you and break a
build that worked yesterday; a pinned one cannot.

| Component | Version | Checksum / commit | Where it is fetched |
| --- | --- | --- | --- |
| Host Python tools | see `requirements-host.txt` | pip resolves exact `==` pins | PyPI |

Rows are added as phases bring in new components.
