#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Verifies that the push to GitHub really landed (PROJECT_PLAN.md section 11): local main equals
# origin/main (same commit and same commit count), every local tag exists on the remote pointing at
# the same commit, and prints the repository URL. Exits non-zero on any mismatch.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"
fails=0

echo "[verify] fetching origin"
git fetch --quiet origin --tags

local_head="$(git rev-parse main)"
remote_head="$(git rev-parse origin/main)"
if [[ "${local_head}" == "${remote_head}" ]]; then
	echo "[verify] main == origin/main (${local_head})"
else
	echo "[verify] MISMATCH: main ${local_head} vs origin/main ${remote_head}"
	fails=$((fails + 1))
fi

local_count="$(git rev-list --count main)"
remote_count="$(git rev-list --count origin/main)"
if [[ "${local_count}" == "${remote_count}" ]]; then
	echo "[verify] commit count ${local_count} on both"
else
	echo "[verify] MISMATCH: ${local_count} local commits vs ${remote_count} remote"
	fails=$((fails + 1))
fi

remote_tags="$(git ls-remote --tags origin)"
for tag in $(git tag --list); do
	want="$(git rev-parse "${tag}^{commit}")"
	# Annotated tags are listed twice; the "^{}" line holds the commit the tag points at.
	got="$(awk -v t="refs/tags/${tag}^{}" '$2 == t {print $1}' <<< "${remote_tags}")"
	[[ -n "${got}" ]] || got="$(awk -v t="refs/tags/${tag}" '$2 == t {print $1}' <<< "${remote_tags}")"
	if [[ "${got}" == "${want}" ]]; then
		echo "[verify] tag ${tag} -> ${want:0:12} on remote"
	else
		echo "[verify] MISMATCH: tag ${tag} local ${want:0:12}, remote '${got:0:12}'"
		fails=$((fails + 1))
	fi
done

echo "[verify] repository: $(gh repo view --json url --jq .url)"
if [[ "${fails}" -ne 0 ]]; then
	echo "[verify] FAILED (${fails} mismatch(es))"
	exit 1
fi
echo "[verify] push verified"
