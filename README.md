# Market Canopy™ Office

Market Canopy Office is a modified version of ONLYOFFICE Desktop Editors, the
original software developed by Ascensio System SIA. This repository holds the
whole of Market Canopy's modification: the build_tools branding overlay, a
bounded patch queue and the build and sync tooling. It is licensed under the
GNU Affero General Public License v3 with the additional terms in
[LICENSE](LICENSE); see [NOTICE](NOTICE) for the origin and trademark notices.

## Source code offer

Every Market Canopy Office build names the commit of this repository it was
built from in its About dialog ("Source code for this build"). That commit, the
ONLYOFFICE commits pinned in its `UPSTREAM` file and the patches in `patches/`
are the complete Corresponding Source; each release also publishes a source
archive of the patched tree beside its installers. ONLYOFFICE's original source
is at https://github.com/ONLYOFFICE.

## Layout

| Path | What it is |
| --- | --- |
| `UPSTREAM` | The ONLYOFFICE release tag and the commit of every component the build uses. |
| `branding/marketcanopy/` | The build_tools branding overlay, staged as `<workspace>/marketcanopy` and passed as `--branding marketcanopy`: `settings.json` (identity, URLs, update feeds), `build_tools/make.py` (the hook build_tools runs), `notices/`, the package overlay under `desktop-apps/package/`, the artwork sources and `replace/` (generated images copied over the checkout, listed in `replace/MANIFEST.json`). |
| `patches/<component>/NNNN-*.patch` | The patch queue, one `git format-patch` series per component, applied with `git am --3way` in name order. Budget: 15 patches, 1,500 changed upstream lines ([report](docs/branding/patch-budget.md)). |
| `build/` | `office-build`, which checks out, patches, builds and packages ([build/README.md](build/README.md)). |
| `contracts/` | The integration contract with Market Canopy (frozen; [contracts/README.md](contracts/README.md)). |
| `scripts/` | `sync-upstream` (move to another ONLYOFFICE tag), `smoke-test` (branding checks), `patch-budget`, `make-artwork` (regenerate the artwork). |
| `docs/branding/` | Notices inventory, destinations audit, patch budget and the upstream sync rehearsal. |

## Changing the branding

- Identity, URLs and update feeds: `branding/marketcanopy/settings.json`, then
  the files `scripts/smoke-test` names if it disagrees.
- Artwork: replace a file under `branding/marketcanopy/artwork/source/` and run
  `scripts/make-artwork` (needs Pillow); `scripts/make-artwork --check` verifies
  the committed outputs.
- An upstream change: edit the component at its `UPSTREAM` commit with the queue
  applied, commit, and export with
  `git format-patch --zero-commit --no-signature --no-numbered -o patches/<component> <pinned commit>..HEAD`.
  Every changed upstream file gets a "Modified by Market Canopy on <date> for
  Market Canopy Office: ..." line under its header, every new file the AGPL
  header naming Market Canopy.
- Run `scripts/smoke-test` before a pull request; `build/office-build --check-patches`
  is the CI check that the queue applies.
