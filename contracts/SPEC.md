# Market Canopy Office integration contract

- Contract version: `1.3.0` (see `VERSION`; `CHANGELOG.md` lists each revision). `1.0.0` was frozen 2026-10-24 after reconciling P01's report (platform-api `prompts/marketcanopy-office/evidence/P01.md`). The hash is in `CONTRACT.sha256`.
- Upstream baseline: ONLYOFFICE Desktop Editors `v10.0.0.176` (desktop-apps `3fd628f986f5ae42853fefa3dbbb10c3992bf71c`, desktop-sdk `a75c35326aad475a5a4ec904baa9a5d518bcc162`). Every upstream seam cited below was read at that tag and is byte-identical to the research pins (desktop-apps `332c1905`, desktop-sdk `ae6cf2a0`).
- Decision records: platform-api ADRs 0523 to 0530. This document replaces the contract baseline in platform-api `docs/architecture/marketcanopy-office/contracts.md` where they differ; section 14 lists every difference and why.
- Normative words: MUST, MUST NOT, SHOULD. Examples are synthetic; no value in this folder is a real credential.

Executable parts: `schemas/` (JSON Schema 2020-12), `fixtures/` (plain JSON, XML and SSE text that native consumers read directly), and `src/` (a TypeScript reference implementation of every parser and validator, which the fixture runner checks). The runner is `npm test` in this folder.

## 1. Parties and channels

| From | To | Channel | Carries | Section |
| --- | --- | --- | --- | --- |
| Desktop app | Office | Process launch (argument array) | Local paths, `--new:<kind>`, one deep link | 3 |
| Browser, desktop app | Office | Office's URL scheme | A cloud document reference; the sign-in callback | 4 |
| Desktop app, Office | Each other | OS credential store entries and one lock file | The linked Office grant and the installation id | 5 |
| Office | API | HTTPS | Sign-in, refresh, web-session exchange, bootstrap, AI session, AI gateway | 6 to 8 |
| Office plugins DD777 and DD007 | Office native | `AscDesktopEditor.getMarketCanopyAiSession()` | An AI-audience access token and the model list | 9 |
| Canopy local tools | Local files | OS file locks and the lock file | "Office has this file open" | 10 |
| Desktop app | Office data folder | `recents.xml`, read only | Recent local files | 11 |
| Desktop app | Release feed | Signed install manifest | Installer URL, hash, publisher | 12 |

There is no pipe, socket or loopback listener between the desktop app and Office. No channel carries a credential except the credential store entry, Office's own HTTPS calls and the GUID-gated native method.

## 2. Identity values

These name Office on each OS. The desktop app, the installer and Office MUST use exactly these. Values marked "P03" are fixed by this contract and implemented by P03's branding overlay; P03 changes one only through a contract revision.

| Name | Value | Status |
| --- | --- | --- |
| Product name | `Market Canopy Office` | ADR 0527 |
| URL scheme | `mc-office` | P03. MUST be exactly 9 characters while desktop-sdk `cefview.cpp` `CCefView::load` strips the scheme with the literal offsets 12 and 10 (written for `oo-office`); any other length needs a patch line there |
| Production credential service | `com.marketcanopy.office` | Section 5 |
| Staging credential service | `com.marketcanopy.office.staging` | Section 5 |
| API client app id (`X-Client-App`) | `com.marketcanopy.office` | Section 6. The desktop app's is `com.marketcanopy.desktop` |
| Client platforms | `windows`, `macos`, `linux` | ADR 0501 |
| macOS bundle identifier | `com.marketcanopy.office` | P03 |
| macOS data folder | `~/Library/Application Support/com.marketcanopy.office/` (upstream: `applicationDataPath` = Application Support + bundle identifier) | P03, follows the bundle id |
| Windows registry key | `HKLM\Software\MarketCanopy\Office`, value `AppPath` (upstream `APP_REG_PATH` = `Software\<sIntCompanyName>\<sIntProductName>`), 64-bit view | P03 |
| Windows uninstall key | `HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\Market Canopy Office_is1`, value `DisplayVersion` (Inno `AppId` + `_is1`) | P03 |
| Windows executable | `<AppPath>\MarketCanopyOffice.exe` (upstream `DesktopEditors.exe`) | P03 |
| Windows data folder | `%LOCALAPPDATA%\MarketCanopy\Office\` (upstream: Qt `GenericDataLocation` + `APP_DATA_PATH`) | P03 |
| Linux package | `marketcanopy-office` | P03 |
| Linux desktop entry | `/usr/share/applications/marketcanopy-office.desktop`, `Exec=/usr/bin/marketcanopy-office %U` | P03 |
| Linux data folder | `${XDG_DATA_HOME:-$HOME/.local/share}/marketcanopy/office/` | P03 |
| Agent plugin (start window) | `{9DC93CDB-B576-4F0C-B55E-FCC9C48DD777}` | Upstream, kept (ADR 0527 decision 1) |
| In-editor AI plugin | `{9DC93CDB-B576-4F0C-B55E-FCC9C48DD007}` (`asc.` prefix in its `config.json`) | Upstream, kept |
| Cloud provider id | `marketcanopy` | ADR 0526 |
| Web-session cookie | `__Host-mc_office_web` | Section 7. MUST NOT be `asc_auth_key` |

## 3. Launch contract

The desktop app starts Office with an argument array, never a shell, and only with the executable discovered from section 2's registration (never a path a renderer supplied). Office's single-instance logic forwards to the running process.

### 3.1 Allowed arguments

| Argument | Effect | Notes |
| --- | --- | --- |
| An absolute local path | Opens the file in a tab, or focuses its tab | Canonicalized by the desktop main process; inside a granted root or picked by the person |
| `--new:word`, `--new:cell`, `--new:slide` | New document, spreadsheet, presentation | Lower case, exactly these |
| One deep link (section 4.1) | Opens a cloud document | At most one per launch |

Every argument MUST:

- be at most 4,096 UTF-8 bytes;
- contain no NUL, CR, LF or U+0001. Upstream joins forwarded arguments with `\x01` (desktop-apps `main.cpp`, the non-primary branch of `SingleApplication`), so U+0001 inside a path would split it;
- if it is a path: be absolute (`/…`, `X:\…` or `\\server\share\…`), contain no `.` or `..` segment, not start with `-`, and not start with `http:`, `https:`, `file:` or the scheme (upstream treats those as URLs);
- if it is a path: name a file whose extension is one Office opens. The desktop app offers `docx`, `doc`, `rtf`, `odt`, `txt`, `xlsx`, `xls`, `csv`, `ods`, `pptx`, `ppt`, `odp` and `pdf`.

### 3.2 Not allowed

- `--view=<path>`, `--review=<path>`, `--edit=<path>`, `--force-use-window` and every other `--` option. Upstream forwards only `--new:` options and non-option arguments to an already running instance and drops the rest (desktop-apps `main.cpp` at the tag; observed on Linux x64 10.0.0-169: a forwarded `--view=` opened nothing). The desktop app cannot know whether Office is running, so it never relies on them.
- `<scheme>://action|…` and `<scheme>://command/…`. Our build disables both (ADR 0529 expected patches: plugin-store deep links).
- Any credential, code or token, in any argument (Linux exposes argv in `/proc/<pid>/cmdline`).

Reference: `src/launch-args.ts` `validateLaunchArgs`; fixtures `fixtures/launch/`.

## 4. URL scheme

### 4.1 Open a cloud document

Grammar (upstream's `open` form, desktop-sdk `CCefView::load`; the URL is the last `|` field and Office loads it in a tab):

```
mc-office://open|n|<name>|<documentUrl>
```

- `<name>`: the tab title, percent-encoded UTF-8, 1 to 255 characters after decoding, no `|`. Upstream decodes only `%20` in it; producers MUST encode only what a URL needs and SHOULD keep names to letters, digits, spaces and `.-_()`.
- `<documentUrl>`: `https://<appOrigin>/office/documents/<documentId>?organizationId=<organizationId>`
  - `<appOrigin>` is one of the allowed app origins: production `https://app.marketcanopy.com`, staging `https://staging-app.marketcanopy.com` (P07 records the deployed origins; a change is a contract revision).
  - `<documentId>` and `<organizationId>`: 24 lower-case hex characters (MongoDB ObjectId).
  - No user info, port, fragment, or any other query parameter. Upstream appends `desktop=true` when it loads the URL (observed); the page MUST accept that one extra parameter and nothing else.
- Upstream also accepts `f` (download through `ascdesktop://external/`); our links never carry it.

Refusal: a consumer (Office's account module and the `/office/documents/` page, P05 and P07) MUST refuse a link that does not match the grammar exactly, and MUST refuse with `CREDENTIAL_IN_LINK` any link whose query carries a parameter named `token`, `access_token`, `refresh_token`, `id_token`, `code`, `ticket`, `key`, `apikey`, `api_key`, `secret`, `password`, `auth`, `authorization`, `jwt`, `session`, `sig` or `signature` (any case), or any value that looks like a credential (`dsr_…`, a three-part JWT, `Bearer …`). The refusal happens before any network request and is never logged with the link's query.

When Office is signed out, or the person cannot read the document, the page shows the reason; it never falls back to another account.

### 4.2 Sign-in callback (standalone Office)

```
mc-office://auth/callback?code=<code>&state=<state>
mc-office://auth/callback?error=<errorCode>&state=<state>
```

- `<code>`: the API's one-time handoff code, 43 base64url characters, valid 5 minutes (`SSO_DESKTOP_HANDOFF_TTL_MS`), single use and bound to the PKCE S256 challenge Office sent. It is useless without Office's verifier, which never leaves Office's memory.
- `<state>`: 43 base64url characters Office generated for this sign-in. Office MUST ignore a callback whose `state` does not match a sign-in it started in the last 5 minutes, and MUST NOT log the URL.
- Upstream would treat this URL as a document to open; P05's account module MUST intercept `mc-office://auth/` before upstream's argument handling.

Reference: `src/deep-link.ts` `parseSchemeUrl`; fixtures `fixtures/deep-link/`.

## 5. OS credential store

### 5.1 Entries

Two entries per OS user, under the service for the build flavor (`com.marketcanopy.office` or `com.marketcanopy.office.staging`):

| Account | Value | Written by | Deleted by |
| --- | --- | --- | --- |
| `installation` | `{"version":1,"installationId":"<uuid v4>"}` | Whoever needs it first (create if absent) | Never (uninstall may) |
| `grant|<apiOrigin>` (for example `grant|https://api.marketcanopy.com`) | Grant value, 5.2 | The desktop app after provisioning, or Office after a standalone sign-in or a rotation | Whoever signs out, after the server revocation call |

One Market Canopy account per API origin is signed in to Office at a time. An account switch replaces the grant entry.

### 5.2 Grant value (version 1)

UTF-8 JSON, no BOM, at most 2,048 bytes (Windows limits a credential blob to 2,560):

```json
{
  "version": 1,
  "apiOrigin": "https://api.marketcanopy.com",
  "userId": "66f0c0ffee0000000000a001",
  "deviceSessionId": "66f0c0ffee0000000000d001",
  "installationId": "4f7d2c1e-8a8b-4c39-9d7e-2b1f5a0c9e11",
  "refreshToken": "dsr_<opaque>",
  "issuedAt": "2026-10-24T21:40:00.000Z",
  "writtenBy": "desktop",
  "lastActiveOrganizationId": "66f0c0ffee0000000000b001"
}
```

`installationId` MUST equal the `installation` entry's value. `writtenBy` is `desktop` or `office`. `lastActiveOrganizationId` is `null` until Office has selected one. Unknown fields are ignored; a different `version` is treated as absent (Office asks the person to sign in; the desktop app provisions again).

### 5.3 Per-OS storage

| OS | Store | Item |
| --- | --- | --- |
| Windows | Credential Manager | `CRED_TYPE_GENERIC`, `TargetName` = `<service>:<account>`, `UserName` = `<account>`, `Persist` = `CRED_PERSIST_LOCAL_MACHINE`, blob = the UTF-8 JSON without a terminator |
| macOS | Login keychain | `kSecClassGenericPassword`, `kSecAttrService` = `<service>`, `kSecAttrAccount` = `<account>`, `kSecAttrLabel` = `Market Canopy Office`, data = the UTF-8 JSON. The writer adds the other app to the item's trusted applications when it can; otherwise one OS access prompt is acceptable (ADR 0501 decision 3) |
| Linux | Secret Service (default collection) | Schema `com.marketcanopy.office.Credential`, attributes `service` = `<service>`, `account` = `<account>`, label `Market Canopy Office`, secret = the UTF-8 JSON |

### 5.4 Rotation lock and rules

Lock file (directory mode 0700, file 0600; on Windows the folder inherits the user-only `%LOCALAPPDATA%` ACL):

| OS | Path |
| --- | --- |
| Windows | `%LOCALAPPDATA%\MarketCanopy\credentials\<service>.lock`, locked with `LockFileEx` exclusive |
| macOS | `~/Library/Application Support/MarketCanopy/credentials/<service>.lock`, `flock(LOCK_EX)` |
| Linux | `$XDG_RUNTIME_DIR/marketcanopy/<service>.lock`, else `~/.local/state/marketcanopy/<service>.lock`, `flock(LOCK_EX)` |

1. Every write and every delete of either entry happens while holding the lock, and re-reads the entry after acquiring it.
2. Only Office rotates the refresh token (`POST /v1/auth/refresh`, section 6.3). It holds the lock from reading the entry until the rotated value is written. A lock held longer than 30 seconds is abandoned by its holder.
3. The desktop app writes the grant entry only when it is absent, unreadable, a different `version`, or for another `userId`; it never overwrites a current grant for the same user (doing so would race Office's rotation and trip reuse detection).
4. Deleting is compare-and-delete: delete only if the stored `deviceSessionId` is the one the deleter revoked. A newer grant written meanwhile stays.
5. A signed-out Office re-reads the entry at launch, when its start window gains focus, and every 60 seconds while signed out.
6. No secure store (no Secret Service on Linux, a locked keychain the person declines): Office keeps a memory-only session and reports `credentialStore: "unavailable"`; the desktop app does not provision. No plaintext file, ever.

Reference: `src/credential-entry.ts`; fixtures `fixtures/credential-store/`.

## 6. Identity routes

All API routes follow the API's conventions: `/v1` prefix, `{ success, data, meta }` envelope, errors `{ success: false, error: { code, message, details? }, meta: { timestamp, path, requestId } }`. Office sends on every call: `X-Client-App: com.marketcanopy.office`, `X-Client-Platform`, `X-Client-Version` (6.1), `X-Device-Id: <installationId>`, and `x-active-organization-id` once an organization is selected (allowed: these are device sessions, not `api`/`mcp` credentials, ADR 0248).

"New" routes are proposals that P06 and P08 implement and publish in OpenAPI; "existing" routes keep their current behavior. P06 and P08 may rename a new route only through a contract revision.

### 6.1 Client version header

`X-Client-Version` MUST match the API's `CLIENT_VERSION_PATTERN` (`^\d{1,6}(\.\d{1,6}){0,3}$`), so Office's release version `<major>.<minor>.<patch>-mc.<n>` (ADR 0529) is sent as `<major>.<minor>.<patch>.<n>`: `10.0.0-mc.3` is `10.0.0.3`. Versions compare numerically, part by part. Reference: `src/client-version.ts`.

### 6.2 Linked grant from the desktop app (new)

`POST /v1/auth/native/linked-grants`

- Caller: the desktop app's own device session (`X-Client-App: com.marketcanopy.desktop`, ADR 0501 decision 3), bearer access token. A stateless desktop JWT is refused (`UNAUTHORIZED`): it cannot parent a revocable grant.
- Body: `{ "clientApp": "com.marketcanopy.office", "installationId": "<uuid>" }`. Only `com.marketcanopy.office` is accepted from this caller in v1 (the CLI's value arrives with its own revision).
- Idempotent per (parent device group, client app, installation): the first call creates the Office device session (201, `created: true`); a repeat returns the same `deviceSessionId` (200, `created: false`) with a newly rotated refresh token, and the previous token stops working after the API's 30-second reuse grace.
- Response `data`: `{ deviceSessionId, clientApp, installationId, userId, refreshToken, refreshTokenExpiresAt, created }`.
- The grant's sliding (90 days) and absolute (365 days) lifetimes never exceed the parent's. Provisioning is not a fresh interactive authentication.
- Errors: `UNAUTHORIZED` (401), `GRANT_REVOKED` (401, the parent device group was revoked), `VALIDATION_ERROR` (422), `INVALID_CLIENT_HEADERS` (400, existing).

### 6.3 Refresh and sign-out (existing)

- `POST /v1/auth/refresh` with `{ "refreshToken": "dsr_…" }` and Office's client headers returns `{ accessToken, refreshToken, expiresIn }`. Access tokens live 15 minutes (`DEVICE_SESSION_ACCESS_EXPIRES_IN`); every refresh rotates; a rotated token presented again after 30 seconds revokes the session. At most 10 sessions per user per app.
- `POST /v1/auth/logout` from Office's session revokes it. Desktop sign-out revokes the desktop device group and every linked grant under it (P06). A revoked or expired session answers 401: Office treats any 401 from a credential route as signed out (clears its grant entry by compare-and-delete, drops the provider's cookies and AI state, keeps editing local files).
- Below the client app's minimum version the API answers 426 `CLIENT_UPGRADE_REQUIRED` (existing code and response, ADR 0404 decision 2, `ClientContextGuard`): message `Version <X-Client-Version> is no longer supported; update the app`, `details: { storeUrl, minSupportedVersion }` from the client-app registry (`storeUrl` may be empty while none is registered). Office shows "Update required" for cloud and AI, offers `details.storeUrl` when it is not empty, and keeps local editing.

### 6.4 Standalone browser sign-in (new)

1. Office generates a PKCE verifier (43 to 128 characters, RFC 7636) and `state` (43 base64url characters), and opens the system browser at `<appOrigin>/auth/native?clientApp=com.marketcanopy.office&codeChallenge=<S256>&state=<state>`. That dashboard page signs the person in with every method the web offers (password, SSO, every second factor) and requires that the web session passed all of them in the last 5 minutes; an older session signs in again first.
2. The page mints the handoff: `POST /v1/auth/native/handoffs` (new), bearer the dashboard's web session, body `{ clientApp, codeChallenge, state }`. `clientApp` must be a registered client app with an `ssoRedirectUri`; `codeChallenge` is the S256 challenge (43 base64url characters); `state` is Office's value, which the API only echoes. Response `data`: `{ redirectUri, expiresAt }`, where `redirectUri` is the app's registered `ssoRedirectUri` with `code` and `state` added, `mc-office://auth/callback?code=<code>&state=<state>` (section 4.2), and `expiresAt` is the code's expiry (5 minutes). The page sends the browser to `redirectUri`. Errors: `UNAUTHORIZED` (401, no web session, or one whose factors are older than 5 minutes), `VALIDATION_ERROR` (422, an unknown client app, one without `ssoRedirectUri`, or a malformed challenge or state).
3. Office redeems: `POST /v1/auth/native/exchange` with `{ code, codeVerifier }` and its client headers. Response `data`: `{ accessToken, refreshToken, expiresIn, deviceSessionId, user: { id, email, displayName } }`. A code is minted only after every factor passed, so the exchange never returns a second-factor challenge.
4. Office writes the grant entry with `writtenBy: "office"`.

Exchange errors: `CODE_EXPIRED` (401), `CODE_REPLAYED` (401), `UNAUTHORIZED` (401, verifier mismatch; the code stays redeemable by the holder of the right verifier, as `SsoDesktopHandoffService.exchange` does today).

## 7. Market Canopy Cloud connection

### 7.1 Web-session code (new)

`POST /v1/office/web-session`, bearer Office access token. Body `{}`. Response `data`: `{ code, audience: "office-web", expiresAt }`.

- `code`: 43 base64url characters; valid 60 seconds; single use; bound to the calling device session and the active organization; audience `office-web`.
- Office's native code loads `<appOrigin>/office/connect` in its own CEF with a POST body `code=<code>` (never a query string, so the code stays out of history and logs).

### 7.2 Redeem (new)

`POST /v1/office/web-session/redeem`, called server-side by the connect page with `{ code }`. Response `data`: `{ userId, organizationId, displayName, deviceSessionId, sessionExpiresAt }`, and the response carries the cookie, which the page's server relays to the browser unchanged:

```
Set-Cookie: __Host-mc_office_web=<opaque>; Path=/; Secure; HttpOnly; SameSite=Lax; Max-Age=28800
```

The session is renewed while used, ends at the device session's revocation, and is checked against the device session on every request. Its name MUST NOT be `asc_auth_key`: desktop-sdk exposes the cookie of that name to the agent plugin through `AscDesktopEditor.getPortalAuthToken` (`cefview.cpp`, `get_portal_auth_token`).

Errors: `CODE_EXPIRED` (401), `CODE_REPLAYED` (401), `WRONG_AUDIENCE` (403, a code minted for another audience), `GRANT_REVOKED` (401), `NOT_A_MEMBER` (403).

After redemption the page calls `portal:login` with `{ provider: "marketcanopy", domain: <appOrigin>, uid: <userId>, displayName }`.

### 7.4 Access token for the connect pages (new)

`POST /v1/office/web-session/token`, called server-side by the connect and document pages, which forward the browser's `Cookie: __Host-mc_office_web=<opaque>` header and nothing else. Body `{}`. Response `data`: `{ accessToken, expiresAt, organizationId }`: an access token of the Office device session the web session is bound to (Office's own audience, never `office-ai`), living at most 15 minutes (`DEVICE_SESSION_ACCESS_EXPIRES_IN`), and the organization the session is bound to. The page's server sends that token as the bearer and `organizationId` as `x-active-organization-id` on its API calls, and never hands the token to the browser. Errors: `UNAUTHORIZED` (401, no cookie, or an expired or revoked web session), `GRANT_REVOKED` (401, the device session was revoked), `NOT_A_MEMBER` (403).

### 7.5 Web-session logout (new)

`POST /v1/office/web-session/logout`, called server-side with the forwarded cookie. Body `{}`. Revokes the web session (not the device session) and answers `data`: `{ revoked }` (`false` when there was no live web session; the call is idempotent and never fails for a missing or expired cookie) with the cookie cleared:

```
Set-Cookie: __Host-mc_office_web=; Path=/; Secure; HttpOnly; SameSite=Lax; Max-Age=0
```

The page relays the cookie and calls `portal:logout`.

### 7.3 Provider declaration

`common/loginpage/providers/marketcanopy/config.json` in the branding overlay: one strict-JSON object in the shape upstream's own providers use at the tag, validated by `schemas/provider-config.schema.json`:

| Field | Value | Upstream reader |
| --- | --- | --- |
| `provider` | `marketcanopy` | desktop-sdk `providers.h` (the provider id), the start page |
| `name` | `Market Canopy` | desktop-sdk `providers.h`, the start page |
| `check.url` | `/office/connect/check` (200 on the app origin) | the start page's Clouds route (kept, not linked) |
| `editorPage` | `/office/documents/` | desktop-sdk `cefview.cpp` `IsExternalCloudEditor`: a navigation or pop-up whose URL contains it opens as an editor tab |
| `startPage` | `/office/connect` | the start page's `openConnectedPortal`: Office opens `<domain><startPage>`, `<domain>` being the `portal:login` domain |
| `order` | `0` | the start page's sort |
| `icons` | `themeLight` and `themeDark`, each `{ connectionsList, buttonLogo }`, `./assets/<name>.svg` | the start page's provider icons |

An optional `$comment` string carries the file's license line, since strict JSON has no comments. There is no `entryPage`: upstream opens an `entryPage` as the whole address (an OAuth provider's own page) instead of appending `startPage` to the signed-in domain. The declaration holds no origin, so the staging and production builds ship the same file; the domain comes from `portal:login`.

## 8. Bootstrap and AI

### 8.1 Bootstrap (new)

`GET /v1/office/bootstrap`, bearer Office access token, allowed below the minimum version (`@AllowOutdatedClient`) so Office can say "Update required". Non-secret. `data` (`schemas/bootstrap.schema.json`):

- `contractVersion`: `"1"`.
- `user`: `{ id, email, displayName }`; `organizations`: active memberships `[{ id, name }]`; `activeOrganizationId`.
- `office`: `{ minimumVersion, latestVersion, updateRequired }` in the release form (`10.0.0-mc.3`).
- `docs`: `{ enabled, appOrigin, connectPath: "/office/connect", documentsPath: "/office/documents" }`.
- `ai`: `{ enabled, disabledReason, baseUrl, models: [{ id, name, capabilities }], defaultModel, limits: { maxRequestBytes, maxOutputTokens } }`. `disabledReason` is `null`, `AI_NOT_ENTITLED` or `QUOTA_EXCEEDED`. `capabilities` ⊆ `chat`, `tools`, `vision`, `reasoning`.
- When `office.updateRequired` is true, `docs.enabled` and `ai.enabled` are false.

### 8.2 AI session token (new)

`POST /v1/office/ai-session`, bearer Office access token. (Outside the gateway base URL, which accepts only `office-ai` tokens.) Response `data`: `{ accessToken, expiresAt, organizationId }`. The token's audience is `office-ai`, it lives at most 15 minutes, carries the device session and the organization, and is accepted by the gateway and nothing else; every other route refuses it with `WRONG_AUDIENCE`. This is the only token JavaScript ever sees (ADR 0524 decision 2).

### 8.3 Gateway (new)

Base URL `<apiOrigin>/v1/office/ai` (bootstrap `ai.baseUrl`). OpenAI-shaped bodies, outside the API envelope, as the MCP endpoint is. Every request carries `Authorization: Bearer <office-ai token>` and `x-active-organization-id` equal to the token's organization (a mismatch is `NOT_A_MEMBER`).

`GET /models`: `{ "object": "list", "data": [{ "id", "object": "model", "created", "owned_by": "marketcanopy" }] }`, only the organization's aliases.

`POST /chat/completions`, the subset both AI surfaces send (agent: desktop-sdk `plugins/ai-agent/src/providers/openai/index.ts`; in-editor plugin: `sdkjs-plugins/content/ai/scripts/engine/`):

| Field | Accepted |
| --- | --- |
| `model` | Required; one of the organization's aliases; anything else `MODEL_NOT_ALLOWED` |
| `messages` | Roles `system`, `developer`, `user`, `assistant`, `tool`; content a string or parts `text` and `image_url` (data URLs only, `vision` models only); `assistant.tool_calls`; `tool.tool_call_id` |
| `stream` | `true` or `false` |
| `stream_options.include_usage` | Optional |
| `tools` | `[{ type: "function", function: { name, description, parameters } }]`, at most 128 |
| `tool_choice` | `auto`, `none`, `required`, or a named function |
| `reasoning_effort` | `low`, `medium`, `high` (reasoning models; ignored otherwise) |
| `temperature`, `top_p`, `max_tokens`, `max_completion_tokens`, `stop` | Optional, bounded by `ai.limits` |
| `n` | Absent or 1 |

Other fields are ignored and never forwarded. The gateway executes no tool.

Non-streaming responses are `chat.completion` objects with `usage`. Streaming responses are `text/event-stream`:

1. `data: <chat.completion.chunk>` events. The first carries `delta.role: "assistant"`. Text arrives in `delta.content`; reasoning, when the model gives it, in `delta.reasoning_content`.
2. A tool call's first delta carries `index`, `id`, `type: "function"`, `function.name` and `function.arguments: ""`; later deltas carry `index` and `function.arguments` fragments only.
3. The last choice event carries `finish_reason` (`stop`, `length`, `tool_calls` or `content_filter`) and an empty `delta`. It MUST NOT carry content: the agent stops reading a choice at `finish_reason` and drops anything in that event.
4. With `include_usage`, one event with `choices: []` and `usage`.
5. `data: [DONE]`.

A failure after the stream has started is one event `data: {"error": {…}}` and the end of the stream, with no `[DONE]`. A client that closes the connection cancels the upstream request within 2 seconds; only tokens actually produced are metered.

Errors (`schemas/gateway-error.schema.json`): `{ "error": { "message", "type", "code", "param": null, "request_id" } }`:

| Code | HTTP | `type` | Note |
| --- | --- | --- | --- |
| `UNAUTHORIZED` | 401 | `authentication_error` | Missing, expired or malformed token |
| `GRANT_REVOKED` | 401 | `authentication_error` | |
| `WRONG_AUDIENCE` | 403 | `permission_error` | Any token that is not `office-ai`: a web, desktop or ordinary Office access token, an API key |
| `NOT_A_MEMBER` | 403 | `permission_error` | Membership gone, or header and token organizations differ |
| `CLIENT_UPGRADE_REQUIRED` | 426 | `invalid_request_error` | |
| `AI_NOT_ENTITLED` | 403 | `permission_error` | |
| `MODEL_NOT_ALLOWED` | 400 | `invalid_request_error` | `param: "model"` |
| `REQUEST_TOO_LARGE` | 413 | `invalid_request_error` | Above `ai.limits.maxRequestBytes` |
| `RATE_LIMITED` | 429 | `rate_limit_error` | `Retry-After` set; clients may retry |
| `QUOTA_EXCEEDED` | 429 | `insufficient_quota` | `x-should-retry: false`: the OpenAI SDK the agent uses retries 429 unless told not to |
| `PROVIDER_UNAVAILABLE` | 503 | `api_error` | Upstream model failure after the router's fallbacks |
| `VALIDATION_ERROR` | 422 | `invalid_request_error` | A body outside the accepted subset: `n` other than 1, a remote or non-image `image_url`, audio or file parts, no `messages`, an image sent to a model without `vision`. `param` names the failing field |

A body above the API server's own transport limit (larger than any `ai.limits.maxRequestBytes`) may be refused by the server framework before the gateway answers, so that 413 is not in the gateway shape; clients treat any 413 as `REQUEST_TOO_LARGE`.

CORS: both surfaces call from `onlyoffice://plugin/…` frames. The gateway answers preflight for any origin without credentials (`Access-Control-Allow-Origin: *`, no `Access-Control-Allow-Credentials`), methods `GET, POST, OPTIONS`, and lists `Authorization` explicitly (a wildcard does not cover it): `authorization, content-type, x-active-organization-id, x-stainless-arch, x-stainless-lang, x-stainless-os, x-stainless-package-version, x-stainless-retry-count, x-stainless-runtime, x-stainless-runtime-version, x-stainless-timeout`. It exposes `x-request-id`.

Reference: `src/sse.ts` (assembles a stream and checks the event order); fixtures `fixtures/gateway/`.

## 9. Native AI session method

```
window.AscDesktopEditor.getMarketCanopyAiSession(): string   // JSON
```

Synchronous, like upstream's `callToolFunction`, because the in-editor plugin builds request headers synchronously (`Provider.getRequestHeaderOptions`). Returns one of (`schemas/ai-session.schema.json`):

```json
{ "state": "ready", "baseUrl": "https://api.marketcanopy.com/v1/office/ai", "accessToken": "<office-ai token>", "expiresAt": "2026-10-24T21:55:00.000Z", "organizationId": "66f0c0ffee0000000000b001", "models": [{ "id": "mc-standard", "name": "Standard", "capabilities": ["chat", "tools"] }], "defaultModel": "mc-standard" }
{ "state": "signed_out" }
{ "state": "offline" }
{ "state": "update_required" }
{ "state": "not_entitled" }
```

- Native code keeps the token in memory, refreshes it (8.2) when less than 60 seconds remain, and never returns an expired one. Callers MUST call the method for every request rather than cache the token.
- Callable only from a frame whose URL is `onlyoffice://plugin/<system plugins folder>/<GUID>/<entry>.html` with GUID `{9DC93CDB-B576-4F0C-B55E-FCC9C48DD777}` or `{9DC93CDB-B576-4F0C-B55E-FCC9C48DD007}` and an entry the build lists. Unlike upstream's `IsPrivilegedFrame`, the user plugins folder never qualifies: a person can install plugins there. Any other caller gets `undefined`, and the method name is added to `NSPluginPermissions::IsRestrictedMethod`.
- Both plugins MUST be in the system plugins folder of our build. Upstream's v10.0.0 Linux x64 release package (`10.0.0-169`) ships the in-editor plugin (`config.json` version `3.2.99`) there but not the DD777 agent; P01 reads the 10.0 changelog as moving the in-editor plugin to an add-on, and onlyoffice.github.io has no tag, so P03 pins its commit. P09 bundles both from pinned source.
- The in-editor plugin's `[external]` provider mechanism (requests relayed to the host by the `ai_onExternalFetch` event) is an alternative P09 may use; whichever is used, the token comes only from this method and the base URL only from bootstrap.

## 10. Local file lock contract

Before a Canopy local tool writes, renames or deletes a local Office file, it computes the file's lock state. Upstream Office takes an OS lock on every file it opens for editing and writes a lock file beside it (desktop-sdk `filelocker.cpp`, every OS, `IsUseLockFile() == true`).

| Check | Windows | macOS, Linux |
| --- | --- | --- |
| OS lock | `CreateFileW(path, GENERIC_READ \| GENERIC_WRITE, 0, …, OPEN_EXISTING)`; `ERROR_SHARING_VIOLATION` means another process holds it (Office opens with `FILE_SHARE_READ`) | `open(path, O_RDWR)` then `fcntl(F_GETLK)` with `F_WRLCK` over the whole file; a result other than `F_UNLCK` means a lock (Office takes `F_WRLCK`) |
| Lock file | `<dir>\.~lock.<name>#` (hidden attribute) | `<dir>/.~lock.<name>#` |

States (`schemas/lock-state.schema.json`):

| State | OS lock | Lock file | Write |
| --- | --- | --- | --- |
| `unlocked` | No | No | Allowed, atomically (temporary file in the same folder, then rename) inside the granted root |
| `locked` | Yes | Any | Refused: `DOCUMENT_OPEN_IN_OFFICE`, "Close it in Market Canopy Office first" |
| `lock_file_only` | No | Yes | Refused with the same code. It is a crashed Office (observed: a killed Office leaves the lock file and releases the OS lock), a file open read-only, or another editor using the same convention (LibreOffice) |
| `read_only` | n/a | n/a | The file is not writable by the person: `PATH_NOT_WRITABLE` |

Lock file content (LibreOffice-compatible, observed on Linux): `,<user>,<host>,<DD.MM.YYYY HH:MM UTC>,<app data dir>;` with commas inside fields written as `&#39;`. Consumers MAY show the user and time; they MUST NOT trust it for anything else. Reference: `src/lock-file.ts`; fixtures `fixtures/locks/`.

The check is advisory and racy by nature: the write itself still goes through the atomic rename, and a failed rename is reported, never retried in place.

## 11. `recents.xml`

Location: `<data folder>/data/recents.xml` on Windows and Linux (`recover_path + "/../recents.xml"`, with `recover_path` = `<data folder>/data/recover`), and `<data folder>/recents.xml` on macOS (`recoveryDataPath` + `/..`). Read only; Office rewrites it by delete and create, so a reader MUST tolerate a missing or truncated file by showing nothing and retrying later.

Format (observed on Linux x64 10.0.0-169, `fixtures/recents/linux-x64-10.0.0-169.xml`):

```xml
<?xml version="1.0" encoding="utf-8"?><recents><file path="/home/u/docs/Budget &amp; forecast.xlsx" type="257" date="24.10.2026 21:31" url="" parent="/home/u/docs" /></recents>
```

- The file starts with a UTF-8 BOM.
- One `file` element per entry, most recent first. Attributes: `path` (required), `type` (integer, informational), `date` (`DD.MM.YYYY HH:MM`, local time of the machine that wrote it), `url` (empty for a local file), `parent`, and optionally `externalcloud`, `crypted="1"`, `pin="1"`.
- The "On this computer" strip shows entries with an empty `url` and an absolute `path`, in file order, at most 10, deriving the kind from the extension (never from `type`). Entries with a `url` are cloud entries and are not shown. `crypted="1"` entries are shown without a preview.
- A root element other than `recents`, a parse error, or no readable file: show nothing and log one warning without file paths.

Reference: `src/recents.ts`; fixtures `fixtures/recents/`.

## 12. Install manifest

Signed with the runtime-pack key the desktop app already trusts (ADR 0528 decision 2), published under `office/<channel>/manifest.json` on the desktop app's Azure Storage site (`schemas/install-manifest.schema.json`):

`{ schemaVersion: 1, channel, version, publishedAt, minimumDesktopVersion, targets: [{ os, arch, url, sha256, size, kind, publisher, sourceArchiveUrl, sourceArchiveSha256 }] }`, where `os` ∈ `windows`, `macos`, `linux`; `arch` ∈ `x64`, `arm64`; `kind` ∈ `exe`, `msi`, `dmg`, `deb`, `rpm`, `appimage`; every URL `https`; `publisher` is the Windows Authenticode subject or the Apple team id, and empty for Linux. The signature is detached (`manifest.json.sig`, Ed25519 over the exact bytes), as runtime packs do. P12 owns the values; this freezes the shape.

## 13. Error codes

`schemas/error-code.schema.json` lists them; `src/errors.ts` maps each to its HTTP status and to Office's displayed state.

| Code | HTTP | Surfaces | Office shows |
| --- | --- | --- | --- |
| `UNAUTHORIZED` | 401 | All API, including the handoff and the web-session token | Signed out |
| `GRANT_REVOKED` | 401 | Linked grant, refresh, web session and its token, gateway | Signed out |
| `ACCOUNT_CHANGED` | 409 | Office internal: the grant entry now names another user | Reconnecting |
| `CODE_EXPIRED` | 401 | Sign-in exchange, web-session redeem | Retry silently once, then "Couldn't connect" |
| `CODE_REPLAYED` | 401 | Same | Same |
| `WRONG_AUDIENCE` | 403 | Web-session redeem, gateway, every route given an `office-ai` token | Signed out |
| `NOT_A_MEMBER` | 403 | Bootstrap, web session and its token, gateway | Organization picker |
| `CLIENT_UPGRADE_REQUIRED` | 426 | All API (existing code) | Update required |
| `AI_NOT_ENTITLED` | 403 | Gateway | "AI isn't included in your plan" |
| `QUOTA_EXCEEDED` | 429 | Gateway | "Your organization has used its AI allowance" |
| `RATE_LIMITED` | 429 | Gateway | Retry after `Retry-After` |
| `MODEL_NOT_ALLOWED` | 400 | Gateway | Reload models |
| `REQUEST_TOO_LARGE` | 413 | Gateway | "That request is too large" |
| `PROVIDER_UNAVAILABLE` | 503 | Gateway | "AI is unavailable, try again" |
| `VALIDATION_ERROR` | 422 | Envelope routes, gateway | Bug report |
| `INVALID_CLIENT_HEADERS` | 400 | Envelope routes (existing) | Bug report |
| `DOCUMENT_OPEN_IN_OFFICE` | n/a | Canopy local tools | "Close it in Market Canopy Office first" |
| `PATH_OUTSIDE_GRANT` | n/a | Canopy local tools, launch | Refused |
| `PATH_NOT_WRITABLE` | n/a | Canopy local tools | Refused |
| `VERSION_CONFLICT` | 409 | Cloud saves (existing editor path) | Existing conflict handling |
| `NETWORK_UNAVAILABLE` | n/a | Office internal | Offline |
| `CREDENTIAL_IN_LINK` | n/a | Deep-link consumers | Refused, nothing loaded |
| `INVALID_LINK` | n/a | Deep-link consumers | Refused, nothing loaded |
| `INVALID_ARGUMENT` | n/a | Launch | Refused, Office not started |

Every error carries safe text and a request id, never a token, a code, a path from another user or document content.

## 14. Versioning, freeze and changes from the baseline

- `1.0.0` was frozen once P01's tag (`v10.0.0.176` in every component) and seam list matched the citations at the top of this document. The coordinator records the version and the hash printed by `npm run hash` (also in `CONTRACT.sha256`).
- After freeze, any change increments the version (minor for additions a consumer can ignore, major otherwise), adds a `CHANGELOG.md` entry naming the downstream units that rerun, and recomputes the hash. No consumer changes the contract privately.

Changes from the baseline (`contracts.md`, 2026-10-24):

| Area | Baseline | v1 | Why |
| --- | --- | --- | --- |
| Arguments | `--view=<path>` allowed | Not allowed | Upstream does not forward it to a running instance (main.cpp; observed) |
| Deep link | `<scheme>://open?…` | `<scheme>://open\|n\|<name>\|<url>` | Upstream's actual grammar (`CCefView::load`; observed) |
| Scheme name | Branding choice | `mc-office`, 9 characters | `CCefView::load` hard-codes the length of `oo-office` |
| Credential account | `<apiOrigin>\|<userId>\|<installationId>` | `grant\|<apiOrigin>` plus an `installation` entry | The desktop app must learn Office's installation id without a channel; a fixed key needs no enumeration and an account switch leaves no stale entry |
| AI token | "Office access token" at the gateway | A separate `office-ai` audience token (8.2) | ADR 0524 decision 2: plugins get only an AI-audience token |
| Update required | `UPDATE_REQUIRED` | The API's existing `CLIENT_UPGRADE_REQUIRED` (426) | One code for one condition |
| Client app id | `office` | `com.marketcanopy.office` | The client-app registry keys apps by reverse-DNS id (`com.marketcanopy.mobile`) |
| New codes | | `RATE_LIMITED`, `PROVIDER_UNAVAILABLE`, `PATH_NOT_WRITABLE`, `CREDENTIAL_IN_LINK`, `INVALID_LINK`, `INVALID_ARGUMENT`, `VALIDATION_ERROR`, `INVALID_CLIENT_HEADERS` | Conditions the baseline did not name |
| Lock states | Locked or not | Adds `lock_file_only` and `read_only` | A crashed Office leaves the lock file (observed) |
| Provider declaration (1.2.0) | `contracts.md` named the fields; 1.0.0 made it a one-item array with `entryPage` and `icons.light/dark.{normal,big}` | One object with `startPage` and upstream's `icons.themeLight/themeDark.{connectionsList,buttonLogo}`, no `entryPage` | desktop-sdk `providers.h` `GetAllJSON` splices `"path"` after the file's first brace and joins the files into one array, so an array file reaches the start page as a nested array; the start page reads only upstream's icon keys and opens `entryPage` as an absolute address (P07, read at the tag) |

## 15. Not verified

Windows and macOS behavior (registry and Launch Services discovery, Credential Manager and Keychain access, the Windows sharing-violation check, macOS `fcntl` from a second process, both `recents.xml` locations) was read from source at the tag, not run. Only Linux x64 was observed, on the upstream release package `10.0.0-169`, not on a Market Canopy build. Every API route marked new does not exist yet. The gateway subset was read from the two AI surfaces' source; no request was captured from a running surface. The in-editor plugin's version at `v10.0.0.176` is P01's to record.
