# Test on device

The overhaul sprint (Nov 2026) is build-only: nothing was flashed, because the device was out of reach.
Every change that could behave differently on real hardware gets a row here, so the first flash can be
checked step by step. Add a row in the same commit as the change. Pure deletions of unreferenced files,
comment fixes and doc changes need no row.

**Rollback:** the production firmware on the device was built from commit `99a181e`. If the new firmware
misbehaves, run `git checkout 99a181e && task build && task flash` (or `task run OTA=true`), then return
with `git checkout main`.

| Area | Change | Commit | How to check | Failure sign |
|---|---|---|---|---|
