# Changelog

Every change after the freeze increments `VERSION`, recomputes `CONTRACT.sha256` (`npm run hash > CONTRACT.sha256`) and names the downstream units that rerun.

## 2.1.0 (2026-10-27)

Requested by the Office test-issues thread after the owner tested the published 10.0.0-mc.1. Minor version under section 14: both changes widen what a consumer accepts, and nothing it already accepts changes.

- 4.1: Office appends `placement=desktop` when it opens a document link as an editor tab from a page (desktop-sdk `cefview.cpp`, the `bIsEditor` branch), so the drive's documents opened in Office reached the `/office/documents/` page with a parameter it refused ("This is not a Market Canopy document link"). The page and the deep-link parser now accept `placement=desktop` beside `desktop=true`, each at most once and with exactly that value. New fixtures `open-placement-param.json` and `placement-other-value.json`.
- 9: an editor loads its system plugins from file URLs (desktop-sdk `plugins.h` `serializePlugins`), not through the plugin scheme, so the in-editor AI plugin never got the AI session ("AI needs a network connection" on every request, and custom assistants failing). `getMarketCanopyAiSession()` is now callable from `file:///<system plugins folder>/<GUID>/<entry>.html` as well; the user plugins folder still never qualifies.

Rerun: P05 (native account module: deep-link parser, AI frame check) and P07 (`/office/documents/` page). Unchanged for P03, P04, P06, P08 to P12.

## 2.0.0 (2026-10-26)

Requested by the Office test-issues thread after the owner ruled on 2026-10-26 that Office builds production only. The staging origins have no DNS, so release run 36, a staging build, could never sign in. Major version under section 14: a consumer that still accepts staging must change. Wire shapes are unchanged: schema `$id`s stay under `/office/v1/` and bootstrap's `contractVersion` stays `"1"`.

- 2, 5.1: one credential service, `com.marketcanopy.office`; `com.marketcanopy.office.staging` is gone. `src/credential-entry.ts` exports `SERVICE` in place of `SERVICES`, and `storeAddress` takes no flavor.
- 4.1: the only allowed app origin is `https://app.marketcanopy.com`. `fixtures/deep-link/open-staging.json` is now `staging-origin.json`, which expects `INVALID_LINK`.
- `common.schema.json`: `apiOrigin` and `appOrigin` are production only. `install-manifest.schema.json`: `channel` is `stable` or `beta`.
- `fixtures/credential-store/other-origin.json` uses `https://api.example.com` as its other origin.
- 7.3: the provider declaration's note no longer mentions a staging build.

Rerun: P04 and P05 (native account module: link and bootstrap origins, credential service), P07 (connect and document pages on the production origin only), P12 (install manifest channels). Unchanged for P03, P06, P08 to P11.

## 1.3.0 (2026-10-25)

Requested by P08 while implementing the gateway. Minor version under section 14: one more gateway error code, which an OpenAI client already handles as an `invalid_request_error`.

- 8.3: the gateway refuses a body outside the accepted subset (`n` other than 1, a remote or non-image `image_url`, audio or file parts, no `messages`, an image sent to a model without `vision`) with 422 `VALIDATION_ERROR`, type `invalid_request_error`, `param` naming the failing field. Added to `gateway-error.schema.json` and `GATEWAY_ERROR_TYPES`; new fixtures `gateway/validation-error.json` and `gateway/validation-error-image-not-vision.json`.
- 8.3: a body above the API server's own transport limit may be refused before the gateway answers, outside the gateway shape; clients treat any 413 as `REQUEST_TOO_LARGE`.

Rerun: P08 (already answers this way), P09 (show the bug-report state for it). Unchanged for every other unit.

## 1.2.0 (2026-10-25)

Requested by P07, the provider declaration's first consumer, after reading upstream's provider readers at `v10.0.0.176`. Minor version under section 14: no unit had shipped a 7.3 declaration, and nothing else in the contract changes, so every other consumer can ignore it (bootstrap's `contractVersion` stays `"1"`).

- 7.3: `config.json` is one object, not a one-item array (desktop-sdk `providers.h` `GetAllJSON` would hand the start page a nested array). The connect page is `startPage` (`/office/connect`, appended to the `portal:login` domain); `entryPage` is refused, since upstream opens it as an absolute address. `editorPage` is fixed to `/office/documents/`. Icons use upstream's keys, `themeLight` and `themeDark` with `connectionsList` and `buttonLogo`. New fixtures `provider-config-array.json` and `provider-config-entry-page.json`.

Rerun: P07. Unchanged for P03 to P06 and P08 to P12.

## 1.1.0 (2026-10-24)

Requested by P06 while implementing the identity routes. Minor version under `SPEC.md` section 14: the routes are additions, and dropping the second-factor answer narrows what the API returns, which no consumer has to handle.

- 6.4: the `/auth/native` page mints the handoff with the new `POST /v1/auth/native/handoffs` (`{ clientApp, codeChallenge, state }` returns `{ redirectUri, expiresAt }`). Codes are minted only after every factor passed in the last 5 minutes, so `POST /v1/auth/native/exchange` never returns a second-factor challenge: removed `fixtures/native-sign-in/second-factor.json` and the challenge branch of `native-exchange-response`.
- 6.3: `CLIENT_UPGRADE_REQUIRED` keeps the API's existing message (`Version <v> is no longer supported; update the app`) and `details: { storeUrl, minSupportedVersion }`; `envelope-error` now checks both.
- 7.4, 7.5: new `POST /v1/office/web-session/token` (the forwarded cookie for a short Office access token and the bound `organizationId`) and `POST /v1/office/web-session/logout` (idempotent, clears the cookie). 7.2 now says the API sets the cookie and the page relays it.

Rerun: P05 (standalone sign-in, the 426 handling), P06 (routes), P07 (connect and document pages). Unchanged for P03, P04, P08 to P12.

## 1.0.0 (2026-10-24)

Frozen after reconciling with P01 (upstream tag `v10.0.0.176` in every component). Supersedes the baseline in platform-api `docs/architecture/marketcanopy-office/contracts.md`; the differences are listed in `SPEC.md` section 14.

Consumers: P03 (identity values, scheme), P04 (launch, discovery, recents, locks, credential entries), P05 (deep links, sign-in, credential entries, bootstrap), P06 (identity routes, error codes), P07 (web session, connect page), P08 (bootstrap, AI session, gateway), P09 (native AI session method), P10 (Canopy local tools and lock states), P12 (install manifest).
