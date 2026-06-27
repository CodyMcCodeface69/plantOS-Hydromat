# PlantOS Debug-Log Capture — Hetzner Server Setup Plan

> **For the Claude Code instance running on the Hetzner server.** This is a
> self-contained implementation plan; you do **not** have access to the PlantOS
> firmware repo. Everything you need to know about the data feed is below.
>
> This **extends** the existing `~/plantos-logging/` Docker stack from
> `HETZNER_LOGGING_SETUP.md` (mosquitto + telegraf + influxdb + grafana). It adds
> **one container** that persists the device's textual debug log. If that stack does
> not exist yet, set it up first — this plan reuses its mosquitto broker and network.

## Context

Two separate data streams come off the PlantOS controller (ESP32-C6, home LAN behind
NAT, dials **outbound** to this server's MQTT broker over TLS:8883):

| stream | topic | content | handled by |
|--------|-------|---------|------------|
| **vitals** | `plantos/vitals` | numeric JSON snapshot every 30 s | the existing telegraf → influxdb → grafana pipeline |
| **debug log** | `plantos/debug` | the raw textual ESPHome log lines (`[I]`, `[D]`, `[W]`, `[E]` …) | **this plan** |

ESPHome's MQTT component auto-publishes the live log stream to
`<topic_prefix>/debug` = **`plantos/debug`**. No firmware change is needed; the device
already publishes there. We just need a durable subscriber + rotation on this box.

### The data feed (contract — do not change without changing firmware)

- **Topic:** `plantos/debug` — retained=false, QoS 0
- **Payload:** one **plain-text log line** per message (UTF-8, may contain ANSI color
  codes), e.g.
  ```
  [I][plantos_controller:214]: State -> IDLE
  [W][safety_gate:090]: AcidPump command rejected: debounce
  ```
- **Cadence:** bursty — follows the firmware `logger:` level (roughly INFO by default;
  more if verbose mode is toggled). Expect anywhere from a few lines/min to bursts of
  dozens/sec during state transitions.
- The broker is the **same mosquitto** already running in the stack. The subscriber
  connects **container-to-container over the internal plaintext listener
  `mosquitto:1883`** — no TLS, no internet exposure for this part.

## Target architecture

```
                          Hetzner server (this box)
ESP32 ──MQTT/TLS:8883──▶ ┌──────────────────────────────────────────────────────┐
                         │ mosquitto                                              │
                         │   ├─ plantos/vitals ─▶ telegraf ─▶ influxdb ─▶ grafana │  (existing)
                         │   └─ plantos/debug ──▶ mqtt-debug-logger               │  (NEW)
                         │                              │                         │
                         │                              ▼                         │
                         │                    /logs/plantos-debug.log             │
                         │                              ▲                         │
                         │                         logrotate  (NEW sidecar)       │
                         └──────────────────────────────────────────────────────┘
```

Two new containers on the existing `plantos` docker network:
- **`mqtt-debug-logger`** — subscribes to `plantos/debug`, prepends an ISO-8601
  timestamp to every line, appends to `/logs/plantos-debug.log`.
- **`logrotate`** — rotates that file daily, compresses, keeps a bounded history.

Both write to a shared named volume `plantos_debug_logs`.

## Deliverables (files to add to `~/plantos-logging/`)

```
~/plantos-logging/
  docker-compose.yml          # add the two services + the volume (below)
  debuglog/
    capture.sh                # subscriber loop (below)
    logrotate.conf            # rotation policy (below)
  mosquitto/config/passwd     # add a read-only `debuglog` user (command below)
  mosquitto/config/acl        # optional: restrict debuglog to subscribe plantos/debug
```

## Implementation steps

### 1. Create a read-only broker user for the subscriber

Reuse the existing passwd file in the mosquitto config volume. Add a dedicated
subscribe-only user so the capture container never needs the publisher credentials:

```bash
cd ~/plantos-logging
docker run --rm -v "$PWD/mosquitto/config:/c" eclipse-mosquitto:2 \
  mosquitto_passwd -b /c/passwd debuglog '<MQTT_DEBUGLOG_PW>'
```

Add `MQTT_DEBUGLOG_PW=<strong>` to the existing `.env`.

*(Optional, recommended)* If you maintain a mosquitto ACL file, restrict the user to
read-only on the debug topic. `mosquitto/config/acl`:

```conf
user debuglog
topic read plantos/debug
```

…and ensure `mosquitto.conf` references it (`acl_file /mosquitto/config/acl`). Reload
mosquitto after changing users/ACLs: `docker compose restart mosquitto`.

### 2. Subscriber script

`debuglog/capture.sh` — uses `mosquitto_sub` (already in the `eclipse-mosquitto`
image). `-F '%I %p'` prepends an ISO-8601 timestamp (`%I`) to the payload (`%p`). The
outer loop makes it resilient to broker restarts (it also has `restart: unless-stopped`
as a backstop).

```sh
#!/bin/sh
set -eu

BROKER_HOST="${BROKER_HOST:-mosquitto}"
BROKER_PORT="${BROKER_PORT:-1883}"
TOPIC="${TOPIC:-plantos/debug}"
OUT="${OUT:-/logs/plantos-debug.log}"

mkdir -p "$(dirname "$OUT")"
echo "$(date -u +%FT%TZ) [capture] starting: ${BROKER_HOST}:${BROKER_PORT} ${TOPIC} -> ${OUT}" >> "$OUT"

while true; do
  # -F '%I %p' => "2026-06-27T15:00:00+0000 <log line>"
  mosquitto_sub \
    -h "$BROKER_HOST" -p "$BROKER_PORT" \
    -u "$MQTT_USER" -P "$MQTT_PW" \
    -t "$TOPIC" -q 0 \
    -F '%I %p' >> "$OUT" || true

  echo "$(date -u +%FT%TZ) [capture] connection lost, retrying in 5s" >> "$OUT"
  sleep 5
done
```

Make it executable: `chmod +x debuglog/capture.sh`.

> ANSI color codes: ESPHome log lines may contain escape sequences. They are harmless
> in the file and `grep`-able. If you want them stripped, change the `mosquitto_sub`
> line to pipe through `sed`:
> `... -F '%I %p' | sed -u 's/\x1b\[[0-9;]*m//g' >> "$OUT"`.

### 3. Rotation policy

`debuglog/logrotate.conf`:

```conf
/logs/plantos-debug.log {
    daily
    rotate 30
    maxsize 50M
    compress
    delaycompress
    missingok
    notifempty
    copytruncate
}
```

`copytruncate` is important: the subscriber keeps the file handle open and appends, so
we rotate by copy-then-truncate rather than rename. Daily, keep ~30 days, also force a
rotate if a single day exceeds 50 MB.

### 4. Add the two services + volume to `docker-compose.yml`

Append to the existing `services:` block, and add the volume under `volumes:`:

```yaml
  mqtt-debug-logger:
    image: eclipse-mosquitto:2
    restart: unless-stopped
    depends_on: [mosquitto]
    environment:
      BROKER_HOST: mosquitto
      BROKER_PORT: "1883"
      TOPIC: plantos/debug
      OUT: /logs/plantos-debug.log
      MQTT_USER: debuglog
      MQTT_PW: ${MQTT_DEBUGLOG_PW}
    entrypoint: ["/bin/sh", "/capture.sh"]
    volumes:
      - ./debuglog/capture.sh:/capture.sh:ro
      - plantos_debug_logs:/logs
    networks: [plantos]

  logrotate:
    image: blacklabelops/logrotate:latest
    restart: unless-stopped
    environment:
      LOGROTATE_INTERVAL: daily
      LOGROTATE_CRONSCHEDULE: "0 2 * * *"   # 02:00 daily
    volumes:
      - plantos_debug_logs:/logs
      - ./debuglog/logrotate.conf:/etc/logrotate.d/plantos:ro
    networks: [plantos]
```

Add to the existing `volumes:` map:

```yaml
volumes:
  # ... existing (mosquitto_data, influxdb_data, grafana_data) ...
  plantos_debug_logs:
```

> If you'd rather not pull `blacklabelops/logrotate`, the alternative is to drop
> `debuglog/logrotate.conf` into the **host** `/etc/logrotate.d/` pointed at the
> volume's host path (`docker volume inspect plantos_debug_logs` → `Mountpoint`) and
> rely on the host's logrotate cron. The sidecar keeps everything inside compose, which
> is preferred here.

### 5. Firewall / exposure

**Nothing new to open.** The subscriber talks to mosquitto over the internal docker
network (`1883`), and the device already publishes via the existing `8883` TLS
listener. Do not expose `1883` or the log file to the internet.

## Verification (end-to-end)

1. `docker compose up -d mqtt-debug-logger logrotate`
2. `docker compose ps` → both `Up`; `docker logs mqtt-debug-logger` shows no auth errors.
3. **Lines are landing:**
   ```bash
   docker compose exec mqtt-debug-logger tail -f /logs/plantos-debug.log
   ```
   → timestamped log lines appear (cadence depends on device activity; trigger a state
   change or enable verbose mode on the device to force traffic).
4. **Independent broker check** (from any host with the CA cert):
   ```bash
   mosquitto_sub --cafile ca.crt -h <hetzner-host> -p 8883 \
     -u plantos -P '<pw>' -t 'plantos/debug' -v
   ```
   → same lines stream live.
5. **Rotation works:** `docker compose exec logrotate logrotate -f /etc/logrotate.d/plantos`
   → a `plantos-debug.log.1` (and `.gz` after the next run) appears; the live file keeps
   growing without interruption (`copytruncate`).
6. **Resilience:** `docker compose restart mosquitto` → the capture container logs
   "connection lost, retrying" then resumes; gaps during the outage are expected and
   acceptable.

## Operational notes

- **Disk:** at INFO level the debug stream is modest; `rotate 30` + `compress` keeps it
  bounded (tens of MB compressed for a month is typical). If verbose mode is left on for
  long stretches, lower `rotate` or `maxsize`.
- **Searching:** `zgrep -h 'ERROR\|\[E\]' /logs/plantos-debug.log*` across rotated files.
- **Optional next step:** if you later want the debug log queryable in Grafana
  alongside vitals, ship the file into **Loki** (add a `promtail` sidecar tailing
  `/logs/plantos-debug.log`) and add a Loki datasource. Out of scope for this plan —
  the flat rotated file is the deliverable here.

## Hand-off back to firmware owner

- Nothing required on the firmware side — `plantos/debug` is published automatically by
  the existing `mqtt:` config. Confirm only that the device's `topic_prefix` is still
  `plantos` (it is, per the contract above). If verbose/quiet logging is desired, that
  is controlled by the firmware `logger:` level, not by this server.
