# PlantOS Desk Display — Firmware Plan (E-ink + SD Logger)

> **For the Claude Code instance running on the desk-display ESP32-C6 project.** This
> is a self-contained spec; you do **not** have access to the main PlantOS controller
> repo. The MQTT feed contract you consume is fully described below.

## Context

A second ESP32-C6 drives an **e-ink desk display** and has an **SD-card slot**. We want
it to double as a **local, offline log host**: it subscribes to the PlantOS vitals MQTT
feed, (1) shows current vitals on the e-ink panel, and (2) appends each reading to a
**CSV file on the SD card** as a redundant, cloud-independent record.

This complements the Hetzner backend (InfluxDB + Grafana) — same MQTT feed, a second
independent subscriber. Adding this device requires **no change** to the controller
firmware.

### The data feed (contract — provided by the PlantOS controller)

- **Broker:** Mosquitto on Hetzner — but on the **LAN** you may instead point at a
  local broker if one exists. Default: the same Hetzner broker, MQTT/TLS **8883**.
- **Subscribe topic:** `plantos/vitals`
- **Cadence:** one message every **30 s**
- **Payload:** JSON; numeric fields omitted when the source sensor reads NaN, booleans
  always present:

```json
{
  "ph": 6.12, "ec": 1450.0,
  "water_temp": 21.3, "air_temp": 24.1,
  "humidity": 55.2, "pressure": 1013.2,
  "water_high": false, "water_low": true, "water_empty": false
}
```

| field | unit | type |
|-------|------|------|
| `ph` | pH | float |
| `ec` | µS/cm | float |
| `water_temp` / `air_temp` | °C | float |
| `humidity` | %RH | float |
| `pressure` | hPa | float |
| `water_high` / `water_low` / `water_empty` | bool | level reached |

## Recommended approach: ESPHome (matches the existing PlantOS stack)

The main controller is ESPHome; staying in ESPHome keeps tooling, OTA, and secrets
handling consistent. ESPHome provides the three pieces needed natively:

- **MQTT subscribe with JSON:** `mqtt:` component + `on_json_message:` trigger.
- **E-ink rendering:** `display:` with the `waveshare_epaper` (or matching) platform
  and a `lambda:` to draw values; refresh on a slow `update_interval` (e-ink wear).
- **SD card CSV append:** the `sd_mmc_card` component (`sd_mmc_card.append_file`).

> If the chosen e-ink panel or SD interface isn't well-supported in ESPHome, fall back
> to **Arduino/PlatformIO** (PubSubClient + ArduinoJson + GxEPD2 + SD/SdFat). The data
> contract and logic below are identical either way.

## Architecture / data flow

```
plantos/vitals (MQTT/TLS) ──▶ on_json_message
                                  ├─▶ store latest values into globals
                                  ├─▶ append CSV row to /sd/plantos_YYYY-MM.csv
                                  └─▶ (throttled) trigger e-ink redraw
```

Keep the latest values in `globals:` so the display lambda can render them
independently of message arrival, and so a missing field just keeps the prior value.

## Implementation outline (ESPHome)

### 1. Core config
`wifi:`, `time:` (SNTP — needed for CSV timestamps), `ota:`, `logger:`. Put broker
host/user/pass/CA and WiFi creds in `secrets.yaml`.

### 2. MQTT subscribe
```yaml
mqtt:
  broker: !secret mqtt_broker
  port: 8883
  username: !secret mqtt_user          # a dedicated read-only user, e.g. "display"
  password: !secret mqtt_password
  certificate_authority: !secret mqtt_ca_cert
  discovery: false
  on_json_message:
    topic: plantos/vitals
    then:
      - lambda: |-
          if (x.containsKey("ph"))        id(g_ph)        = x["ph"];
          if (x.containsKey("ec"))        id(g_ec)        = x["ec"];
          if (x.containsKey("water_temp"))id(g_water_temp)= x["water_temp"];
          if (x.containsKey("air_temp"))  id(g_air_temp)  = x["air_temp"];
          if (x.containsKey("humidity"))  id(g_humidity)  = x["humidity"];
          if (x.containsKey("pressure"))  id(g_pressure)  = x["pressure"];
          id(g_water_high)  = x["water_high"];
          id(g_water_low)   = x["water_low"];
          id(g_water_empty) = x["water_empty"];
          id(g_have_data)   = true;
      - script.execute: log_to_sd
      - component.update: eink_display      # redraw (throttle if needed)
```
Declare matching `globals:` (`g_ph`, `g_ec`, … as `float`; booleans as `bool`;
`g_have_data` bool).

### 3. SD-card CSV logging
```yaml
sd_mmc_card:
  id: sd_card
  # set the SPI/SDMMC pins for the actual board

script:
  - id: log_to_sd
    then:
      - lambda: |-
          // Monthly file so it stays manageable: plantos_2026-06.csv
          char path[40]; auto t = id(sntp_time).now();
          snprintf(path, sizeof(path), "/plantos_%04d-%02d.csv",
                   t.year, t.month);
          // Header is written once on boot (see on_boot below).
          char row[160];
          snprintf(row, sizeof(row),
            "%lld,%.2f,%.0f,%.2f,%.2f,%.1f,%.1f,%d,%d,%d\n",
            (long long)t.timestamp,
            id(g_ph), id(g_ec), id(g_water_temp), id(g_air_temp),
            id(g_humidity), id(g_pressure),
            id(g_water_high), id(g_water_low), id(g_water_empty));
          id(sd_card).append_file(path, (const uint8_t*)row, strlen(row));
```
- **CSV columns:** `unix_ts,ph,ec,water_temp,air_temp,humidity,pressure,water_high,water_low,water_empty`
- On boot, if this month's file doesn't exist, write the header row first.
- Only log once SNTP time is valid (guard on `id(sntp_time).now().is_valid()`), else
  timestamps are garbage.

### 4. E-ink display
```yaml
display:
  - platform: waveshare_epaper      # adjust to the actual panel/driver
    id: eink_display
    update_interval: never          # we trigger redraws from on_json_message
    lambda: |-
      it.printf(0, 0,  id(font_lg), "pH  %.2f", id(g_ph));
      it.printf(0, 30, id(font_lg), "EC  %.0f uS/cm", id(g_ec));
      it.printf(0, 60, id(font_md), "Water %.1f C  Air %.1f C",
                id(g_water_temp), id(g_air_temp));
      it.printf(0, 90, id(font_md), "RH %.0f%%  %.0f hPa",
                id(g_humidity), id(g_pressure));
      it.printf(0, 120,id(font_md), "Lvl H:%d L:%d E:%d",
                id(g_water_high), id(g_water_low), id(g_water_empty));
```
- E-ink: **throttle redraws** (e.g. at most every few minutes or only on meaningful
  change) to avoid ghosting/wear; full refresh periodically to clear ghosting.

## Open items / decisions to confirm on the device project
- **Exact e-ink panel** (model/driver, resolution, color) → sets the `display:` platform
  and layout.
- **SD interface** (SDMMC vs SPI) and the **GPIO pinout** for this specific board.
- **Broker target:** the Hetzner broker (8883/TLS, internet) vs a LAN-local broker if
  one is added later (plaintext 1883 acceptable on trusted LAN). Create a dedicated
  read-only MQTT user (`display`) on the broker rather than reusing `plantos`.
- **Display content / layout** and refresh cadence preferences.
- Whether the device should also render a small **trend** (it could keep a short ring
  buffer of recent values in RAM, or read back from its own SD CSV).

## Verification
1. Flash; confirm logs show MQTT connect + `Received message on plantos/vitals`.
2. Pull the SD card (or read over the web) → `plantos_YYYY-MM.csv` grows by one row
   per ~30 s, with a sane header and timestamps.
3. E-ink shows current pH/EC/temps and updates when new messages arrive.
4. Power-cycle → on reboot it reconnects, keeps appending to the same monthly file
   (header not duplicated), and repaints the panel once data arrives.
