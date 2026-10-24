# Changelog

Every change after the freeze increments `VERSION`, recomputes `CONTRACT.sha256` (`npm run hash > CONTRACT.sha256`) and names the downstream units that rerun.

## 1.0.0 (2026-10-24)

Frozen after reconciling with P01 (upstream tag `v10.0.0.176` in every component). Supersedes the baseline in platform-api `docs/architecture/marketcanopy-office/contracts.md`; the differences are listed in `SPEC.md` section 14.

Consumers: P03 (identity values, scheme), P04 (launch, discovery, recents, locks, credential entries), P05 (deep links, sign-in, credential entries, bootstrap), P06 (identity routes, error codes), P07 (web session, connect page), P08 (bootstrap, AI session, gateway), P09 (native AI session method), P10 (Canopy local tools and lock states), P12 (install manifest).
