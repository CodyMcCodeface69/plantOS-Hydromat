# Custom web UI: assessment

Written 2026-11-14 for workstream 4 (`plan/04-ui-eval.md`). The device was not reachable, so nothing here
was tested on hardware. "Verified" means read in the installed ESPHome 2025.4.2 source
(`.venv/lib/python3.12/site-packages/esphome/`, paths below are relative to `components/`) or in
`plantOS.yaml`. Everything else is marked **(assumption)**.

## Recommendation

**Build the Shelly-style page and serve it from the ESP at its own path (`/app`), next to the ESPHome
page, not instead of it.** Until the next USB/OTA flash is possible, use the same file from the laptop
or phone (option b), which already works against today's firmware without any firmware change.
Keep the ESPHome v3 page as the service and debug view (calibration, pump tests, PWM).

Why: the page is one 68 kB file (21 kB gzipped), 8 % of the free app flash. Served from the device it
has no CORS or mixed-content problem, works offline in the LAN, and can later sit behind
`web_server: auth:`, which an externally hosted page cannot (see Security).

## What the three options are

| | (a) page served by the ESP | (b) page hosted elsewhere | (c) stay on ESPHome v3 |
|---|---|---|---|
| Firmware change | yes (small) | none | YAML only |
| Flash | +21 kB, or −55 kB if it replaces v3 | 0 | 0 |
| Works offline in the LAN | yes | from a file or PWA: yes. From Hetzner: no | yes |
| Breaks on ESPHome upgrade | REST/SSE contract only | REST/SSE contract only | no (keep `local: true`) |
| Works with `auth:` enabled | yes | **no** | yes |
| Effort | 1 to 2 days | 0.5 day (done: the prototype) | 2 to 3 hours |

## Flash cost (verified numbers)

- App partition 1,835,008 B, used 1,583,438 B (86.3 %, baseline in `plan/LOG.md`), free 251,570 B.
- The embedded v3 frontend that `local: true` adds is **76,654 B** (`INDEX_GZ` in
  `web_server/server_index_v3.h`, already gzipped).
- The prototype is **68,042 B raw, 21,012 B gzipped** (`gzip -9`), including the 12 kB mock device,
  which a production page would drop. A finished page with a few more screens stays under 30 kB
  gzipped **(assumption)**. That is no reason to wait for the 8 MB layout.

How ESPHome 2025.4.2 can embed a page (verified in `web_server/__init__.py` and `web_server.cpp`):

- `js_include` / `css_include` read a local file at build time, gzip it into flash and serve it at the
  fixed paths `/0.js` and `/0.css` with `Content-Encoding: gzip`. There is no option for an HTML file
  and no other path.
- Without `local`, `/` is a ~150 B shell: `<script type=module src=/0.js>` (if `js_include`),
  `<esp-app>`, then `<script src="{js_url}">`. `js_url: ""` leaves the remote script out. So
  `version: 3`, `js_url: ""`, `js_include: ui/app.js` makes the custom page **the** page at `/`
  (the JS builds the DOM). It saves 55 kB, but it removes the ESPHome page.
- With `local: true`, `/` serves `INDEX_GZ` directly and never references `/0.js`. Both cannot be the
  index at the same time.
- `version` only selects the bundle (1, 2 or 3). The device pages are routed only at `/`, `/0.css`,
  `/0.js`, `/events` and `/<domain>/<id>[/<action>]`.

To have **both pages**, add one small component (a role name such as `device_page`) that registers an
`AsyncWebHandler` on `web_server_base` for `GET /app` and sends a gzipped `index.html` from flash, the
same way `handle_js_request` does. About 60 lines of C++ and 30 lines of Python **(assumption)**, +21 kB flash.
It touches no actuator, so it does not conflict with the 3-layer rule.

## CORS, Private Network Access, mixed content (for option b)

Verified:

- Every response, including `/events`, carries `Access-Control-Allow-Origin: *`
  (`web_server_base/web_server_base.h:91`, applied in `web_server_idf/web_server_idf.cpp:184` and `:321`).
- `enable_private_network_access` defaults to true (`web_server/__init__.py`), and `plantOS.yaml` does
  not change it. An `OPTIONS` request that has `Access-Control-Request-Private-Network` gets 200 with
  `Access-Control-Allow-Private-Network: true` plus name and MAC headers (`web_server.cpp:305-314`).
  It does **not** send `Access-Control-Allow-Methods` or `-Headers`.
- Any other `OPTIONS` (an ordinary CORS preflight) is not handled: `canHandle()` only accepts GET/POST
  for entity URLs (`web_server.cpp:1838-1970`). So a cross-origin page may only send **simple requests**:
  no `Authorization` header, no JSON body, no custom headers.
- A POST needs a `Content-Length` header; a body is only read when it is form-urlencoded
  (`web_server_idf.cpp:70-83`). Parameters come from the body or the URL query, URL-decoded
  (`web_server_idf.cpp:232-250`, `utils.cpp:72-89`).

The prototype therefore sends `fetch(url, {method: 'POST'})` with parameters in the query and nothing
else. Tested against a local fake of the API from a `file://` page in Chrome: no preflight is sent and
Chrome adds `Content-Length: 0`. Other browsers do the same per the Fetch standard **(assumption)**.

Limits that source reading cannot settle **(assumptions, to check on site)**:

- **HTTPS page → `http://192.168.0.201`** is mixed content and is blocked. A page on the Hetzner server
  behind TLS, or an installed PWA (a service worker needs HTTPS), therefore cannot talk to the device.
  The ESP-IDF web server here has no TLS option.
- A plain-HTTP public page calling a private address is subject to the browser's Private Network Access
  rules; Chrome's current "local network access" permission prompt may apply.
- What works without any of that: the file opened from disk, a plain-HTTP page inside the LAN, or the
  page served by the ESP. Remote use (away from home) needs a VPN into the LAN with every option.

## Maintenance across ESPHome upgrades

- (a) and (b) depend on the REST/SSE contract only: the `/events` event names (`ping`, `state`, `log`),
  the `id` format `<domain>-<object_id>`, and the action URLs. 2025.4 state events have no `domain`
  field (see `docs/WEBUI_PERFORMANCE_CLEANUP_PLAN.md`); newer versions add one and are moving towards
  name-based ids **(assumption)**. The prototype accepts both forms and keeps every entity name in one
  table, so an upgrade means re-checking that table.
- Object ids are generated at build time as `sanitize(snake_case(name))` (`cpp_helpers.py:105`,
  `helpers.py:412-422`): lower-case, space → `_`, anything outside `[a-zA-Z0-9_-]` → `_`. Hyphens stay
  and underscores are not collapsed: `pH K-Factor (live)` → `ph_k-factor__live_`,
  `Calendar Day +1` → `calendar_day__1`. **Renaming an entity in the YAML changes its URL** and breaks
  the page silently. Put a comment at the top of `plantOS.yaml` once a custom page is in use.
- (a) via the `/app` handler also depends on the internal `AsyncWebHandler` C++ interface. (c) has no
  extra maintenance, but the "frontend must match the firmware" issue in `AGENTS.md` stays.

## Security

State today (verified by key name in `plantOS.yaml`, values not read):

- `web_server:` has **no `auth:`**. Anyone in the LAN can read everything and press every button.
- `api:` has **no `encryption:`** key and no password. `ota:` (`platform: esphome`) has **no password**:
  anyone in the LAN can flash firmware. This is the largest hole and is independent of the UI question.
- Web OTA (`/update`) is off: it is not supported on ESP-IDF in 2025.4.2 (`validate_ota`).
- The action URLs do not check the HTTP method: `GET /button/<id>/press` and
  `GET /switch/<id>/turn_on` execute (`web_server.cpp:464-486`, `515-530`). Together with
  `Access-Control-Allow-Origin: *`, any web page open in a browser inside the LAN can trigger
  "Empty Tank" or "Emergency Stop" with an image tag. Browsers' private-network rules may block
  this from public sites **(assumption)**; nothing on the device does.

What each option changes:

- (a) Adds no new exposure. It is the only custom-UI option compatible with `auth:` (same origin, the
  browser's Basic-auth prompt covers page, REST and `/events`).
- (b) Adds no exposure by itself, but it **locks the device into "no auth"**: with `auth:` set, every
  handler including `/events` and the preflight is wrapped by the auth check
  (`web_server_base.cpp:22-28`), `EventSource` cannot send an `Authorization` header, and a `fetch`
  with one needs a preflight that the device does not answer.
- `auth:` on 2025.4.2 is HTTP Basic over plain HTTP (`web_server_idf.cpp:192-222`). It stops casual
  access and the cross-site trick above, not someone sniffing the WiFi.

## Effort and what the prototype already shows

`prototype/index.html` is the proposed page (see `README.md`). It runs in mock mode now and against
the real API later. Open points found while building it:

- **The calendar targets are not published.** No entity carries today's pH band or EC target, so the
  gauges fall back to a fixed 5.5 to 6.5 band on the current firmware. Suggestion for step 3.5: put them
  into the planned status text sensor, e.g. `Haupt · Day 69 · Bloom (pH 6.0-6.4, EC 2.20)`. The
  prototype already parses that form.
- Refusals are invisible. A press returns 200 even when the FSM refuses the operation; the reason only
  appears in the log. Step 3.1 plans a "UI-visible reason": a text sensor "Last Refusal" would let the
  page show it as a message.
- The preset entity names in the prototype's table are proposals (`Function Nutrients` etc.). Whatever
  3.5 really names them, only the table changes.

## Next steps

1. Owner: open the prototype in mock mode and decide whether this is the direction (5 minutes).
2. Back in the LAN: open it against the device, compare every value with the ESPHome page, note
   wrong mappings. Check on the phone as well (file opened from storage).
3. Phase 3.5: publish targets and the refusal reason (see above); settle the entity names.
4. If the direction is accepted: add the `/app` handler component, strip the mock from the embedded
   copy, add a link between the two pages. One build, +21 kB.
5. Independently of the UI: set an `ota:` password and `api: encryption:`. Add `web_server: auth:`
   once the page is served from the device. All three need a flash and belong in `TEST_ON_DEVICE.md`.
6. Not recommended: hosting the page on Hetzner (mixed content, no offline use, no auth), and
   replacing the ESPHome page at `/` (the calibration and test entities would lose their UI).
