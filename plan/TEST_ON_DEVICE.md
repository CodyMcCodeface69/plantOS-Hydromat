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
| Logging | Production compiles at log level INFO (was DEBUG); `ESP_LOGD/V` output is gone, incl. the `ezo_ph_uart` DEBUG lines that were on by default. The level is set by an inline package in `plantOS.yaml`; the debug build (uncomment the one `debug:` line) is back at DEBUG with the old per-tag limits | `f4b96ed` | `task snoop`: boot log shows INFO/WARN/ERROR lines only, no `[D]` lines; status report, FSM transitions and dose logs still appear. Debug build: `[D]` lines appear and the "Verbose: ..." switches change the output | No log output at all over USB/API, or `[D]` lines in a production build |
| Network | Removed the unused ESPHome `http_request` component (with it `CONFIG_ESP_TLS_INSECURE` / `SKIP_SERVER_CERT_VERIFY` are no longer set) and the unreferenced scripts `connectivity_check_timeout`, `wifi_reconnect` | `3870dcc` | Shelly sockets still switch and poll (air pump, grow light state in the UI); `plantos/vitals` arrives at the broker every 30 s; "WiFi Reconnect" button still works | Shelly commands fail with HTTP errors, or MQTT never connects (TLS error in the log) |
