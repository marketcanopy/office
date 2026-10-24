# Account module tests

Tests for Office's Market Canopy account module (platform-api ADR 0524), the
plain C++ in
`branding/marketcanopy/add/desktop-sdk/ChromiumBasedEditors/lib/src/marketcanopy/`.
They build without CEF or an ONLYOFFICE checkout. `.github/workflows/account-tests.yml`
runs them on Windows, macOS and Linux.

```
cmake -S tests/account -B "$TMPDIR/account-tests"
cmake --build "$TMPDIR/account-tests"
ctest --test-dir "$TMPDIR/account-tests" -V
```

- `fixture_tests` runs every `contracts/fixtures` file that has a native reader
  (deep links, grant reads, client versions, envelopes, bootstraps, the AI
  session results) and expects what `contracts/test/fixtures.test.ts` expects.
- `trace_tests` drives `CAccount` through a scripted environment
  (`fake_environment.h`). Each request is checked against its fixture's
  request, and each answer is that fixture's response. It prints one trace per
  scenario: standalone sign-in, a desktop-provisioned connection, rotation,
  revocation, wrong-GUID refusal, the AI session, update required,
  organizations, memory-only, offline, and a busy lock. After each scenario it
  checks that no payload, log line or trace line holds a token, a code or a
  verifier.
- `os_store_tests` writes the real OS credential store and takes the real lock.
  It runs only with `MC_OS_STORE_TESTS=1`, under a test service name. On Linux
  it needs an unlocked Secret Service, and the workflow shows how to start one.
  `MC_OS_STORE_EXPECT=unavailable` checks the memory-only fallback instead (point `DBUS_SESSION_BUS_ADDRESS` at a bus that does not exist).
