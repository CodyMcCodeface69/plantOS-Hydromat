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
| MQTT / TLS | mbedTLS reduced to TLS 1.2 **client only**; curves secp192r1/224r1/192k1/224k1/256k1 and brainpool 256/384/512 removed (kept: secp256r1, secp384r1, secp521r1, Curve25519); static ECDH-ECDSA / ECDH-RSA key exchange removed (kept: ECDHE-ECDSA, ECDHE-RSA, RSA); PEM write and CRL/CSR parsing removed | `c63a159` | After boot the log shows the MQTT client connected; `plantos/vitals` arrives at the Hetzner broker every 30 s (desk display / Grafana update). WiFi still joins (WPA2/WPA3 use the same crypto library) | `mqtt` log shows TLS handshake errors (`mbedtls_ssl_handshake returned -0x...`) or repeated reconnects; no vitals in Grafana. Fix: remove the "Flash diet: TLS" block in `plantOS.yaml` and rebuild |
| WiFi / MQTT | sdkconfig: WPA-Enterprise, OWE, SAE-PK and WPA3 for the fallback AP switched off (the station keeps WPA2 + WPA3-SAE; the fallback AP `PlantOS-Fallback` stays, ESPHome runs it as WPA2-PSK); MQTT WebSocket transports switched off (MQTT uses plain TLS on 8883) | `ea53ffa` | Device joins the home WiFi after boot and after a router restart; `plantos/vitals` arrives every 30 s. Optional: with the router off for > 1 min the `PlantOS-Fallback` AP appears and accepts `debug123` | WiFi never connects (`wifi` WARN lines with auth failures), or the fallback AP rejects the password. Fix: remove the "Flash diet: MQTT and WiFi" block in `plantOS.yaml` |
