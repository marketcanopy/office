# Upstream sync rehearsal

`scripts/sync-upstream --to <tag>` moves the build to another ONLYOFFICE tag
(P03; the release repository's `office-sync.yml` runs it, commits the result
to `sync/upstream-<tag>` and opens a pull request with the report as its body).
It was rehearsed on 2026-10-24 between the previous public release,
ONLYOFFICE Desktop Editors 9.4.0 (component tag `v9.4.0.131`), and the pinned
`v10.0.0.176`, in a scratch clone of this repository.

## Runs

1. **`v9.4.0.131` to `v10.0.0.176`, the forward sync.** UPSTREAM was first set to
   the 9.4.0 commits (`scripts/sync-upstream --to v9.4.0.131 --resolve-only`),
   then synced to `v10.0.0.176`. Every component resolved by its tag except
   `onlyoffice.github.io`, which has none and resolves to its default branch,
   as build_tools' own checkout does. The queue applied (8 patches), the patches
   were refreshed with `git format-patch`, and the 29 smoke checks passed against
   the patched workspace. The resulting `UPSTREAM` and `patches/` are
   byte-identical to the ones committed here, so a refresh with no upstream
   change is a no-op. Report: [v9.4.0.131-to-v10.0.0.176.md](sync-rehearsal/v9.4.0.131-to-v10.0.0.176.md).
   It lists 6 overlay copies and 5 replacements whose upstream originals changed
   between the two tags: those are the files a reviewer compares by hand.

2. **`v10.0.0.176` to `v9.4.0.131`, a sync that conflicts.** The queue, written
   for 10.0.0, does not apply to 9.4.0: desktop-apps `0002` conflicts in
   `ASCAboutController.m` and the Fastfile. The script exited 1, left `UPSTREAM`
   and `patches/` unchanged, did not run the smoke suite, and its report names
   the conflicting patch and files and the 36 overlay and replacement files that
   differ (9.4.0 has no `idx-logo.svg` start page and no About image set, for
   example). Report: [v10.0.0.176-to-v9.4.0.131.md](sync-rehearsal/v10.0.0.176-to-v9.4.0.131.md).

## What a sync pull request asks of its reviewer

- Every "overlay copy" row: diff upstream's new file against the old one and
  carry the change into the copy under `branding/marketcanopy/` (for example
  `package/inno/defines.iss`, `3DPARTYLICENSE`), or record why not.
- `build_tools/make.py` changed: confirm it still runs the branding's
  `build_tools/make.py` with no arguments and `OO_RUNNING_BRANDING=1`.
- Every "replacement" row: regenerate with `scripts/make-artwork` if upstream
  changed an image's size or added one that carries the ONLYOFFICE logo.
- A conflict: rebase the patch by hand on the new tag in a checkout of the
  component, export it with `git format-patch --zero-commit --no-signature
  --no-numbered`, and run the sync again.

## Not rehearsed

- The `office-sync.yml` workflow itself (P04/P12): only the script it runs.
- A build of the synced tree: the rehearsal applies and checks the queue; it
  compiles nothing.
