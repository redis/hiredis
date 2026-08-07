---
name: backport-pr
description: Backport a fix to hiredis maintenance branches (release/vX.Y.X) and land it as a PR so Release Drafter lists it in the auto-drafted patch release. Use when asked to backport a commit or fix to release lines, create maintenance branches for past versions, or wire up automatic draft releases for them.
---

# Backport a fix to hiredis maintenance branches

Backports land on `release/vX.Y.X` maintenance branches and MUST arrive as merged
pull requests — Release Drafter builds the `$CHANGES` section of a draft release
exclusively from merged PRs found in the `last-release-tag...branch-head`
comparison. Commits pushed directly to the branch never appear in the changelog
(they only contribute commit authors to `$CONTRIBUTORS`), and the association
cannot be created after the fact: GitHub links a commit to a PR only when a PR
containing that exact SHA is merged.

## Maintenance branch conventions

- One branch per release line, named `release/vX.Y.X` (literal `X` suffix,
  e.g. `release/v1.0.X`), created from the release tag (`v1.0.2`).
- Skip release candidates (`v1.1.0-rc1`) when enumerating versions to backport to.
- Enumerate versions from remote tags (`git ls-remote --tags`), not the local
  clone — local tags may be stale.
- If a maintenance branch does not exist yet, create it from the newest
  non-RC tag of that line and add the Release Drafter setup below before
  landing the backport.

## Backport flow (per branch)

1. Review the fix against each target line before touching anything — the
   affected code can differ substantially between release lines, and older
   lines may not even be affected. Confirm the vulnerable/buggy code exists
   on a line before backporting to it.
2. Create a topic branch from the maintenance branch:
   `backport-<topic>-X.Y.x` (e.g. `backport-nesting-depth-1.0.x`).
3. `git cherry-pick <sha>` — preserves original authorship, which is what puts
   the original author into the draft's Contributors section. Resolve conflicts
   minimally: prefer dropping stale comments or context over restructuring the
   surrounding code.
4. Build and test on every branch: `make hiredis-test && ./hiredis-test`.
   The FULL suite must pass against a real Redis server — the connection
   tests are part of the merge bar, not optional. Before running the tests,
   check the server is up with `redis-cli ping`; if it is not running, STOP
   and prompt the user to start it (e.g. `redis-server --daemonize yes`) —
   do not start one yourself, do not skip the connection tests, and do not
   proceed to the PR with anything less than a clean `-Werror` build and a
   fully passing suite on every branch.
5. Open a PR: base = the maintenance branch, head = the topic branch.
   - Title becomes the changelog line — keep the upstream commit subject.
   - Label it so it lands in the right changelog category (`bug` → "🐛 Bug
     Fixes", `maintenance`, `feature`, ...); the label must already exist in
     the repo.
   - Credit the original author and, for security fixes, the reporter in the
     PR body.
6. Merge with a merge commit (`gh pr merge --merge --delete-branch`).
   The merge push triggers Release Drafter, which updates the branch's draft.

Do NOT open PRs for CI/infra changes (workflows, release-drafter config) —
push those directly to the maintenance branch. Only functional backports go
through PRs.

## Verifying

- `gh run list --workflow "Release Drafter" --branch release/vX.Y.X` for run status.
- Draft content: `gh api repos/redis/hiredis/releases --jq '.[] | select(.draft)'`
  — drafts are invisible to unauthenticated/underprivileged tokens, so an empty
  result may mean an auth problem, not missing drafts.
- A correct draft names the next patch version, targets the maintenance branch,
  lists each backport PR in its category, and credits both the backporter and
  the original commit authors as contributors.

## Known gotchas

- The draft's version self-corrects after each published release; no manual
  version pinning is needed once `filter-by-range` is in place.
