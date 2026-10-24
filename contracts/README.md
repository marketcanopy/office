# Market Canopy Office contracts

The versioned interface between Market Canopy Office, the Market Canopy desktop app, the Market Canopy API and the browser. `SPEC.md` is the normative text; this folder also holds the machine-readable parts every implementation tests against.

This folder is licensed under the MIT License (`LICENSE`), not the AGPL terms that cover the rest of the repository, so that closed implementations (the desktop app, the API) can copy the schemas and fixtures.

| Path | What it is |
| --- | --- |
| `SPEC.md` | The contract, version in `VERSION` |
| `CONTRACT.sha256` | SHA-256 over the contract files (`src/hash-cli.ts` says which) |
| `schemas/` | JSON Schema 2020-12, one file per document, `$id` under `https://contracts.marketcanopy.com/office/v1/` |
| `fixtures/<area>/*.json` | Cases with an input and the expected result; plain JSON (plus one captured XML and lock file) that native code reads directly |
| `src/` | TypeScript reference parsers and validators, run by Node 22.18+ without a build step |
| `test/` | The fixture runner |

## Commands

```
npm ci
npm run typecheck
npm test                 # every fixture against the schemas and src/
npm run hash             # prints "<sha256>  office-contracts-<version>"
npm run check            # all three, and compares the hash with CONTRACT.sha256
```

## Fixture format

```json
{ "fixture": 1, "name": "…", "description": "…", "subject": "…", "input": …, "expect": … }
```

`subject` says how a consumer runs the case:

| Subject | Input | Expect |
| --- | --- | --- |
| `launch-args` | Argument array | `{ ok }` or `{ ok: false, error, index }` |
| `deep-link` | One URL | The parse result of `src/deep-link.ts` |
| `credential-read` | `{ raw, apiOrigin, signedInUserId?, revokedDeviceSessionId? }` | `{ state, value or reason, desktopMayWrite?, mayDelete? }` |
| `exchange` | `{ request, response }` for one API call | `{ outcome: "ok" or an error code }` |
| `document` | `{ schema, value }` | `{ valid }` |
| `sse` | A gateway `text/event-stream` body | The assembled stream, or the first order violation |
| `lock-state` | `{ osLock, lockFile, writable }` | `{ state, write }` |
| `lock-file` | `{ file or text, path }` | `{ lockFilePath, content }` |
| `recents` | `{ file or text }` (`text: null` is a missing file) | `{ state, files or reason }` |
| `client-version` | `{ release }` or `{ compare: [a, b] }` | `{ header }` or `{ result }` |

Two fixtures are captures from upstream ONLYOFFICE Desktop Editors 10.0.0-169 on Linux x64: `fixtures/recents/linux-x64-10.0.0-169.xml` and `fixtures/locks/linux-x64-10.0.0-169.lock`. Every other value is synthetic; nothing here is a real credential.
