# Changelog

Every change after the freeze increments `VERSION`, recomputes `CONTRACT.sha256` (`npm run hash > CONTRACT.sha256`) and names the downstream units that rerun.

## 1.1.0 (2026-10-24)

Requested by P06 while implementing the identity routes. Minor version under `SPEC.md` section 14: the routes are additions, and dropping the second-factor answer narrows what the API returns, which no consumer has to handle.

- 6.4: the `/auth/native` page mints the handoff with the new `POST /v1/auth/native/handoffs` (`{ clientApp, codeChallenge, state }` returns `{ redirectUri, expiresAt }`). Codes are minted only after every factor passed in the last 5 minutes, so `POST /v1/auth/native/exchange` never returns a second-factor challenge: removed `fixtures/native-sign-in/second-factor.json` and the challenge branch of `native-exchange-response`.
- 6.3: `CLIENT_UPGRADE_REQUIRED` keeps the API's existing message (`Version <v> is no longer supported; update the app`) and `details: { storeUrl, minSupportedVersion }`; `envelope-error` now checks both.
- 7.4, 7.5: new `POST /v1/office/web-session/token` (the forwarded cookie for a short Office access token and the bound `organizationId`) and `POST /v1/office/web-session/logout` (idempotent, clears the cookie). 7.2 now says the API sets the cookie and the page relays it.

Rerun: P05 (standalone sign-in, the 426 handling), P06 (routes), P07 (connect and document pages). Unchanged for P03, P04, P08 to P12.

## 1.0.0 (2026-10-24)

Frozen after reconciling with P01 (upstream tag `v10.0.0.176` in every component). Supersedes the baseline in platform-api `docs/architecture/marketcanopy-office/contracts.md`; the differences are listed in `SPEC.md` section 14.

Consumers: P03 (identity values, scheme), P04 (launch, discovery, recents, locks, credential entries), P05 (deep links, sign-in, credential entries, bootstrap), P06 (identity routes, error codes), P07 (web session, connect page), P08 (bootstrap, AI session, gateway), P09 (native AI session method), P10 (Canopy local tools and lock states), P12 (install manifest).
