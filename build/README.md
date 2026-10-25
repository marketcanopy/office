# office-build

`build/office-build` builds Market Canopy Office from ONLYOFFICE's sources at
the tag in `UPSTREAM`, with this repository's branding overlay and patch queue
applied (platform-api ADRs 0529 and 0530). It is Python 3.8+ with no
dependencies beyond the standard library; the per-OS toolchain is upstream
build_tools' own.

```
build/office-build --targets all|windows|macos|linux|<list> --flavor production|staging
                   [--package] [--sign] [--source-release] [--upstream-tag <tag>]
```

Targets: `win-x64`, `win-arm64`, `mac-arm64`, `mac-x64`, `linux-x64`,
`linux-arm64`. The host builds what it can and prints `SKIPPED <target>: <why>`
for every other requested target; it exits `3` when it can build none of them.

| Host | Builds |
| --- | --- |
| Windows x64 | `win-x64`, `win-arm64` (cross-built) |
| macOS (Apple Silicon or Intel) | `mac-arm64`, `mac-x64` |
| Linux x64 | `linux-x64`, `linux-arm64` (cross-built with upstream's sysroot) |

## Options

| Option | Meaning |
| --- | --- |
| `--flavor` | `staging` (default) or `production`. Production refuses a component that `UPSTREAM` does not pin. |
| `--package` | Run every phase: compile, then installers, then seal. Without it the build stops after compile. |
| `--sign` | A signed release. macOS signs, notarizes and staples here (keychain identity and App Store Connect key from the environment). Windows refuses to make installers from unsigned binaries; the release workflow signs between phases. Without `--sign` every artifact is named `-development` and cannot enter a release manifest. |
| `--source-release` | Write `<out>/source/`: the source archive of this repository and every patched component, `SOURCE-MANIFEST.json`, an SPDX 2.3 SBOM and `SHA256SUMS`. |
| `--upstream-tag` | Build another tag; the commit pins are dropped and resolve from the tag. For sync rehearsals. |
| `--release-number` | `<n>` of the version `<upstream>-mc.<n>`. `0` (default) is a development build. |
| `--phase` | Run one of `prepare`, `compile`, `stage`, `installers`, `seal`. |
| `--work-dir` | The workspace (default `$OFFICE_WORK_DIR`, else `../office-work`). It must be outside any git work tree. |
| `--out` | Artifact directory (default `build/out/`). |
| `--install-deps` | Provision the toolchain (CI runners): apt packages, Qt, Inno Setup, Visual Studio 2019 Build Tools, CMake 3. |
| `--check-patches` | Only check out the components, apply the patch queue and the overlay, and report the patch budget. |
| `--dry-run` | Print the plan and the commands; run nothing. |

## Phases

1. `prepare`: a depth-1 checkout of build_tools and of every component build_tools lists for `--module desktop`, each at its `UPSTREAM` commit; the branding folder `branding/marketcanopy` copied beside them; `patches/<repo>/*.patch` applied with `git am --3way` under a fixed identity, so a patched component is the same commit everywhere; `branding/marketcanopy/replace/` copied over the tree and checked against its `MANIFEST.json`. It writes `<work>/resolved-commits.json`.
2. `compile`: build_tools `configure.py` and `make.py` with `--branding marketcanopy`.
3. `stage`: the app folder ready to sign (Windows: upstream `make.ps1`; macOS: an unsigned `xcodebuild` of the ONLYOFFICE scheme for the architecture). Prints `stage_dir`.
4. `installers`: Windows ZIP and Inno Setup installer; Linux deb, rpm, rpm-suse and tar.xz through `make_package.py`; macOS zip and dmg, signed and notarized under `--sign`, with the Sparkle EdDSA signature of the zip.
5. `seal`: `build-info.json` (version, flavor, signed, upstream commits, patch digests, this repository's commit, each file's SHA-256) and `SHA256SUMS` over the final files.

Every run writes `<out>/measurements/measurements-<targets>-<phases>.json` with
wall time per phase, the build directory's peak size, the volume's and the
memory's peak use, and the host. In GitHub Actions it also writes the
`built`, `skipped` and `stage_dir` step outputs and a step summary.

## Environment it exports to build_tools

`PRODUCT_VERSION`, `BUILD_NUMBER`, `OFFICE_VERSION`, `OFFICE_UPSTREAM_TAG`,
`OFFICE_FLAVOR`, `OFFICE_BUILD_SIGNED`, `OFFICE_SOURCE_ROOT`,
`OFFICE_PATCHES_APPLIED=1` and `OFFICE_OVERLAY_APPLIED=1` (the branding hook
then does not apply the queue, the replacements or the added files again).

## Requirements

| Host | Needs |
| --- | --- |
| Linux x64 | Ubuntu 22.04 or 24.04, sudo, about 100 GB free, 16 GB of memory or swap. `--install-deps` runs build_tools' `deps.py` and fetches its prebuilt Qt 5.9.9. |
| Windows x64 | Visual Studio 2019 (any edition or the Build Tools; `--install-deps` installs the Build Tools) with the C++ x64, ARM64 and ATL components and, for win-arm64, Windows SDK 10.0.22621 (the 2019 ARM64 compiler cannot build the 10.0.26100 headers; `--install-deps` installs it, and `office-build` passes the version to `vcvarsall` and to boost's b2), Python 3, Git, CMake 3 (CMake 4 refuses x265's policies; `--install-deps` puts CMake 3.31.6 first on `PATH`); Qt 5.15.2 msvc2019_64 in `OFFICE_QT_DIR` or fetched with aqtinstall under `--install-deps`; Inno Setup 6. |
| macOS | Xcode, Python 3, CMake 3 (as on Windows), Qt 5.15.2 clang_64 in `OFFICE_QT_DIR` or fetched with aqtinstall under `--install-deps`. |

The release builds run in `circuitid/marketcanopy-office-release`
(`office-release.yml`). This repository's own CI runs on GitHub-hosted runners
with no secrets: lint, these tests and the patch-apply check.

## Tests

```
python3 -m unittest discover -s build/tests -t build
ruff check build && ruff format --check build
```
