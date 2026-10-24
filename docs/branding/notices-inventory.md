# Notices inventory

Where Market Canopy Office carries each notice ONLYOFFICE's license and its
additional terms require (ADR 0527 decision 2), and what produces it. Checked by
`scripts/smoke-test` where a check is named. This is an engineering inventory,
not legal clearance: the legal review is the owner's.

## What the license asks

| Requirement (LICENSE) | Where Office meets it | Produced by | Check |
|---|---|---|---|
| Additional term 1: keep the copyright notices, license notices, warranty disclaimers and origin notices | Upstream file headers are kept; every changed upstream file adds a line below them. The start page About and every editor's About carry the copyright lines and the warranty disclaimer. | `patches/**`, `branding/marketcanopy/notices/notice.json` | "every changed upstream file carries a modification line" |
| Additional term 2: prominent notice of modification, with the dates, and that it is based on ONLYOFFICE by Ascensio System SIA | The origin statement, "Modified on/between ..." and "Based on ONLYOFFICE Desktop Editors <tag>" in both About dialogs; "Modified by Market Canopy on 2026-10-24 for Market Canopy Office: ..." in every changed upstream file | `notices.py` (dates from `git log` of `branding/`, `patches/`, `UPSTREAM`) | "notice carries the origin statement" |
| Additional term 3: Appropriate Legal Notices in the user interface, identifying ONLYOFFICE, that the version is modified, and the license | About on the start page (Windows, Linux) and About in every editor's File menu (all platforms): origin, modification, license, warranty, links to the license, the additional terms, third-party licenses, this build's source and ONLYOFFICE's source; the three license texts bundled under "Open-source licenses" | desktop-apps `0004`, web-apps `0001`, `notices.py` | "license texts are keyboard reachable", "editors' About loads the notice", "start page About shows the notice" |
| Additional term 4: no trademark license | The notice states that ONLYOFFICE is Ascensio System SIA's trademark and that Office is not affiliated with or endorsed by it. No ONLYOFFICE logo ships: every upstream image carrying one is replaced (`branding/marketcanopy/artwork/MAPPING.md`). | `notice.json`, `scripts/make-artwork` | "replacement files match their digests" |
| Additional term 5: CC BY-SA 4.0 for non-code elements | The CC BY-SA text is bundled in both About dialogs and in the packages; the notice and `3DPARTYLICENSE` say Market Canopy artwork is not under CC BY-SA | `notice.json`, `3DPARTYLICENSE` | |
| AGPL section 6: the Corresponding Source | "Source code for this build" links `github.com/marketcanopy/office/tree/<source commit>`; P04's source archive is published beside each release | `notice.json`, `build/officebuild/source_release.py` (P04) | "notice links the source of the build" |
| New files of the modification | AGPL header naming Market Canopy and `SPDX-License-Identifier: AGPL-3.0-only` | each new file | "every new file carries the Market Canopy AGPL header" |

## Where the texts are

| Text | Repository | Installed with the package | Shown in About |
|---|---|---|---|
| AGPL v3 with the additional terms | `LICENSE`, `branding/marketcanopy/desktop-apps/package/common/license/opensource/LICENSE.{txt,rtf,html}` (upstream's, verbatim) | Windows installer license page and `{app}`, Linux `/usr/share/licenses` and the Debian `copyright` file | yes, bundled (`marketcanopy-license-agpl.txt`, the editors' template) |
| CC BY-SA 4.0 | `.../opensource/LICENSE-CC.txt` | as above | yes |
| Third-party components | `.../3dparty/3DPARTYLICENSE`: upstream's list plus a Market Canopy section (dictionaries, fonts, CC BY-SA non-code, Market Canopy artwork, trademarks) | as above | yes |
| Debian copyright | desktop-apps `0005`: Ascensio System SIA 2009-, Market Canopy 2026-, the origin statement | `.deb` | |
| Windows version resource | desktop-apps `0001` `version.h`: "© Ascensio System SIA 2009-YYYY. Modifications © Market Canopy 2026-YYYY." | `MarketCanopyOffice.exe` properties | |
| macOS | desktop-apps `0002`: `NSHumanReadableCopyright` "Copyright © 2009-2026 Ascensio System SIA. Modified by Market Canopy, 2026."; the localized `InfoPlist.strings` keep upstream's copyright line | Finder Get Info, the native About window | the native About names the product, the AGPL link and the site |

## Known gaps

- macOS has no start page About: the app uses its native About window, which
  names the product and links the AGPL but does not carry the whole notice
  block. The editors' About carries it on macOS too.
- The notices are English only.
- The Debian `copyright` file's CC BY-SA stanza (`*.png *.svg ...`) is
  upstream's pattern and also matches Market Canopy artwork, which is not under
  CC BY-SA; the notice and `3DPARTYLICENSE` say so, the stanza does not.
- `SUPPORT_MAIL` / `urls.supportMail` is the placeholder
  `office-support@marketcanopy.invalid` until the owner names a support mailbox;
  it appears in the Debian and RPM metadata.
- "Market Canopy" is the copyright holder named for the modifications; the
  owner confirms the legal entity name.
