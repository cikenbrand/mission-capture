# Upstream relationship

Mission Capture is a fork of [OBS Studio](https://github.com/obsproject/obs-studio) that intends to
**stay mergeable with upstream indefinitely**. This document records the fork point, the branch
topology, the merge procedure, and the merge history.

If you read only one thing here: **merge monthly**. Merging monthly is a chore; merging annually is
a project.

---

## Fork point

| | |
|---|---|
| **Upstream** | `https://github.com/obsproject/obs-studio.git` |
| **Fork commit** | `14e3dae77f9893a15d69c8b7bae57ac8ab961f59` |
| **Short** | `14e3dae77` |
| **Date** | 2026-08-10 17:48:54 −0400 |
| **Subject** | `frontend: Fix spacer in permissions dialog` |
| **Forked on** | 2026-08-12 |

At the fork point our tree was **identical** to `upstream/master` — zero commits ahead, zero
behind.

---

## Remotes

| Remote | Fetch | Push |
|---|---|---|
| `origin` | `git@github.com:cikenbrand/mission-capture.git` | same |
| `upstream` | `https://github.com/obsproject/obs-studio.git` | **`DISABLED`** |

`origin` is a fresh, empty repository rather than a GitHub fork of obs-studio — which is what we
want. A GitHub fork would have carried all 22 of upstream's release branches into our namespace,
where they would only ever be noise; upstream history is reachable through the `upstream` remote
regardless.

`upstream`'s push URL is deliberately set to the literal string `DISABLED`, so
`git push upstream` fails immediately with a clear error instead of attempting to write to the OBS
Project's repository. Do not "fix" this.

```bash
git remote set-url --push upstream DISABLED
```

---

## Branch topology

```
upstream/master          pristine OBS Studio, fetched not merged
      │
      ├─► master         our shippable branch
      │       ▲
      │       │ merge when a phase is verified
      │       │
      └─────► develop    integration; upstream merges land here first
                  ▲
                  │
                  ├── feature/mc-<task>    one per task group, e.g. feature/mc-layers-tree
                  └── feature/mc-<task>
```

| Branch | Purpose | Rules |
|---|---|---|
| `master` | Shippable. Every commit is a state we would give to a vessel | Only ever fast-forwarded from `develop` after a phase's acceptance criteria pass |
| `develop` | Integration | Feature branches and upstream merges land here |
| `feature/mc-*` | One per task or small task group | Short-lived. Delete after merge |

We keep `master` rather than renaming to `main`. Git already disambiguates ours (`master`) from
upstream's (`upstream/master`), so the rename would buy clarity we already have.

Neither `master` nor `develop` has been pushed yet, so neither has an upstream tracking ref. The
first push sets it:

```bash
git push -u origin master
git push -u origin develop
```

There is **no local `upstream-tracking` branch.** The plan originally called for one, but
`remotes/upstream/master` — which git maintains automatically on every fetch — does the same job,
and a local copy is one more thing to forget to update. Merge from `upstream/master` directly.

---

## Merge procedure

Monthly, on `develop`, never directly on `master`.

A scheduled task, **`mission-capture-upstream-merge`**, runs at 09:00 on the 1st of each month and
reports drift: how far behind we are, which seam files upstream touched, and — most importantly —
whether any *non-seam* file we have modified also changed upstream, which is the signal that the
fork strategy is slipping. It reports only; it never merges. (It runs while the app is open, or on
next launch if the app was closed when it was due.)

```bash
git fetch upstream --no-tags
git switch develop
git merge upstream/master
```

Then, in order:

1. **Resolve conflicts.** Every conflict should be in a file on the seam list
   ([architecture.md §8](architecture.md#8-upstream-seams)). A conflict in a file that is *not* a
   documented seam means we have drifted from the fork strategy — log it as an open item in
   [PROGRESS.md](PROGRESS.md) rather than just fixing it and moving on.
2. **Build clean** with the `windows-subsea-x64` preset.
3. **Run the full suite**: `tools/subsea-tests/run-tests.ps1 -Suite all`.
4. **Record the merge** in the log below, including which seams conflicted. The pattern over time
   tells us which seams are becoming expensive.
5. Merge `develop` → `master` only if the suite is green.

### Health signals

Watch these. They are how the mergeability promise fails — slowly, then suddenly.

- **Seam count above ~30** ([architecture.md §8](architecture.md#8-upstream-seams)) — we are
  touching too much upstream code
- **Conflicts outside the seam list** — drift
- **The same seam conflicting every month** — that seam is in a file upstream is actively
  refactoring; consider whether the integration can move
- **A merge taking more than half a day** — the cadence is too slow or the seams are too deep

---

## Merge log

| Date | Upstream commit | Conflicts | Seams touched | Suite | Notes |
|---|---|---|---|---|---|
| 2026-08-12 | `14e3dae77` | — | — | — | Fork point. Tree identical to upstream |

---

## Formatting

Upstream's `clang-format` and `gersemi` configurations are kept **unchanged**, and CI keeps
enforcing them ([Phase 0 task 0.6](phase-0-foundation.md)). Reformatting the tree to a different
style would produce a conflict on essentially every merged file, forever. This is the cheapest
mergeability win available and it costs nothing but the discipline not to touch `.clang-format`.
