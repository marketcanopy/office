# Changelog

Every change after the freeze increments `VERSION`, recomputes `CONTRACT.sha256` (`npm run hash > CONTRACT.sha256`) and names the downstream units that rerun.

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
