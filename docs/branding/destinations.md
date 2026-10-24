# Web destinations audit

Every web address the patched v10.0.0.176 tree still points at outside Market
Canopy, found by searching `desktop-apps/win-linux/src`, `desktop-apps/macos`,
`desktop-apps/common/loginpage` and the editors' `web-apps/apps/*/main` code
(locale files and bundled help excluded) for `onlyoffice.com` after the patch
queue and the branding hook ran. The build settings in
`branding/marketcanopy/settings.json` replace the editors' publisher, support,
help and feedback addresses (web-apps `Gruntfile.js` placeholders).

| Destination | Where | Decision |
|---|---|---|
| `download.onlyoffice.com/install/desktop/editors/...` update feeds | build_tools defaults | Refused: the branding hook fails a build that would use them; `scripts/smoke-test` fails on any branding file or patch that adds one. |
| BSL license of ONLYOFFICE's AI add-on (`docspace.onlyoffice.com/s/...`) | desktop-apps `defines.h` `URL_BSL_LICENSE` | No longer shown: desktop-apps `0003` drops it from About; the add-on is off (`disable_addon_mode`). The define stays unused. |
| Plugin store, `<scheme>://action\|...`, `<scheme>://command/...` | desktop-apps, macOS `AppDelegate.mm` | Disabled by desktop-apps `0003` (P02 contract section 3.2). |
| `templates.onlyoffice.com/?desktop=true` | desktop-apps `cascapplicationmanagerwrapper.cpp` (opening a template) | Kept: ONLYOFFICE's public template gallery, used only when a person opens Templates. `urls.templates` is empty until Market Canopy has a gallery. |
| `templates-embed.onlyoffice.com` | start page `routes/templates/index.jsx` | Kept, as above. |
| Dropbox and Box sign-in apps at `onlyoffice.com/app`, and the other cloud connectors | start page `providers/*/config.json` | Removed (desktop-apps `0006`, owner decision 2026-10-24): every upstream connector is deleted and the start page has no Clouds menu item; Office connects to Market Canopy by itself. |
| `helpcenter.onlyoffice.com/.../InsertFieldCodes.aspx` | document editor `HeaderFooterTab.js` | Kept: a help link in the field codes dialog. |
| `api.onlyoffice.com/docs/plugin-and-macros/...` | `MacrosDialog.js` | Kept: developer documentation for macros, nominative use. |
| `onlyoffice.com/blog/2025/10/docs-9-1-released` | document and presentation editors' `Toolbar.js` | Kept: a "what's new" link of upstream features. |
| `onlyoffice.com/trademark-policy` | the license text | Required: part of ONLYOFFICE's additional term 4. |

With `urls.help` empty, build_tools patch `0001` defines no online help address
(`URL_WEBAPPS_HELP`), so the editors use the help bundled with them rather than
ONLYOFFICE's help site. Not verified in a running build.
