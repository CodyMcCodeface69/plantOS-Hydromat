# PlantOS Logging Backend — Hetzner Server Setup Plan

> **For the Claude Code instance running on the Hetzner server.** This is a
> self-contained implementation plan; you do **not** have access to the PlantOS
> firmware repo. Everything you need to know about the data feed is below.

## Context

A PlantOS hydroponic controller (ESP32-C6, on a home LAN behind NAT) publishes its
vitals over **MQTT** every 30 seconds. We need a durable, queryable, visualized
time-series store. This box is the always-on backend: it runs the **MQTT broker**,
ingests into **InfluxDB 2**, and serves **Grafana** dashboards.

The ESP32 connects **outbound** to this server over **MQTT/TLS on port 8883** (no
inbound ports are open on the home router — this server must be reachable from the
internet on 8883). Other consumers (a spare SD-card ESP32, a future Home Assistant)
will subscribe to the same broker later, so keep the broker a first-class service.

### The data feed (contract — do not change without changing firmware)

- **Broker auth user (publisher):** `plantos`
- **Topic:** `plantos/vitals` — retained=false, QoS 0, published every **30 s**
- **Payload:** a single JSON object. Numeric fields are **omitted when the sensor
  reads NaN** (sensor not ready); booleans are always present:

```json
{
  "ph": 6.12,
  "ec": 1450.0,
  "water_temp": 21.3,
  "air_temp": 24.1,
  "humidity": 55.2,
  "pressure": 1013.2,
  "water_high": false,
  "water_low": true,
  "water_empty": false
}
```

| field | unit | type |
|-------|------|------|
| `ph` | pH | float |
| `ec` | µS/cm | float |
| `water_temp` | °C | float |
| `air_temp` | °C | float |
| `humidity` | %RH | float |
| `pressure` | hPa | float |
| `water_high` / `water_low` / `water_empty` | level reached | bool |

> The firmware also auto-publishes per-entity ESPHome state topics under `plantos/…`
> (HA discovery is disabled). Ignore those; consume only `plantos/vitals`.

## Target architecture

```
                          Hetzner server (this box)
ESP32 ──MQTT/TLS:8883──▶ ┌──────────────────────────────────────────────┐
                         │ mosquitto  (1883 internal net, 8883 TLS ext)  │
                         │     │                                          │
                         │     └─▶ telegraf (mqtt_consumer, json_v2)      │
                         │             └─▶ influxdb 2  ─▶ grafana :3000   │
                         └──────────────────────────────────────────────┘
```

All four services run as Docker containers on one user-defined bridge network.
Mosquitto exposes **8883 TLS to the internet** for the ESP32, and **1883 only on the
internal docker network** for Telegraf (no TLS needed container-to-container).

## Deliverables (file tree to create)

```
~/plantos-logging/
  docker-compose.yml
  .env                      # secrets (gitignored): INFLUX_TOKEN, GF_ADMIN_PW, mqtt pw
  mosquitto/
    config/mosquitto.conf
    config/passwd           # generated via `mosquitto_passwd`
    certs/ca.crt ca.key server.crt server.key   # generated, see below
  telegraf/
    telegraf.conf
  grafana/
    provisioning/datasources/influxdb.yml
    provisioning/dashboards/dashboards.yml
    dashboards/plantos-vitals.json
```

## Implementation steps

### 1. TLS certificates for Mosquitto
The ESP32 verifies the broker against a CA cert. Generate a small self-signed CA and a
server cert whose **CN/SAN matches the public hostname/IP** the ESP32 will dial:

```bash
cd ~/plantos-logging/mosquitto/certs
# CA
openssl req -new -x509 -days 3650 -nodes -keyout ca.key -out ca.crt \
  -subj "/CN=PlantOS-CA"
# Server key + CSR (set SERVER_HOST to the public FQDN or IP)
SERVER_HOST=your.hetzner.host
openssl req -new -nodes -keyout server.key -out server.csr \
  -subj "/CN=${SERVER_HOST}" \
  -addext "subjectAltName=DNS:${SERVER_HOST}"   # use IP:1.2.3.4 if dialing by IP
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -days 3650 -out server.crt \
  -extfile <(printf "subjectAltName=DNS:${SERVER_HOST}")
```
> **Hand `ca.crt` back to the firmware owner** — its PEM goes into the firmware's
> `secrets.yaml` as `mqtt_ca_cert`. Keep `ca.key`/`server.key` private.

### 2. Mosquitto config + users
`mosquitto/config/mosquitto.conf`:
```conf
persistence true
persistence_location /mosquitto/data/
log_dest stdout

# Internal plaintext listener for Telegraf (docker network only — never published)
listener 1883
allow_anonymous false
password_file /mosquitto/config/passwd

# External TLS listener for the ESP32 (published to the internet)
listener 8883
allow_anonymous false
password_file /mosquitto/config/passwd
cafile   /mosquitto/certs/ca.crt
certfile /mosquitto/certs/server.crt
keyfile  /mosquitto/certs/server.key
tls_version tlsv1.2
```
Create users (run once, inside the mosquitto image):
```bash
docker run --rm -v $PWD/mosquitto/config:/c eclipse-mosquitto:2 \
  mosquitto_passwd -c -b /c/passwd plantos  '<MQTT_PLANTOS_PW>'
docker run --rm -v $PWD/mosquitto/config:/c eclipse-mosquitto:2 \
  mosquitto_passwd    -b /c/passwd telegraf '<MQTT_TELEGRAF_PW>'
```

### 3. Telegraf — MQTT → InfluxDB
`telegraf/telegraf.conf`:
```toml
[agent]
  omit_hostname = true

[[inputs.mqtt_consumer]]
  servers  = ["tcp://mosquitto:1883"]
  topics   = ["plantos/vitals"]
  username = "telegraf"
  password = "${MQTT_TELEGRAF_PW}"
  data_format = "json_v2"
  [[inputs.mqtt_consumer.json_v2]]
    measurement_name = "vitals"
    # floats (absent ones are simply skipped, which is fine)
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "ph";         type = "float"; optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "ec";         type = "float"; optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "water_temp"; type = "float"; optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "air_temp";   type = "float"; optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "humidity";   type = "float"; optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "pressure";   type = "float"; optional = true
    # booleans
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "water_high";  type = "bool"; optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "water_low";   type = "bool"; optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "water_empty"; type = "bool"; optional = true

[[outputs.influxdb_v2]]
  urls         = ["http://influxdb:8086"]
  token        = "${INFLUX_TOKEN}"
  organization = "plantos"
  bucket       = "vitals"
```

### 4. docker-compose.yml
```yaml
services:
  mosquitto:
    image: eclipse-mosquitto:2
    restart: unless-stopped
    ports:
      - "8883:8883"          # TLS — exposed to internet for the ESP32
    volumes:
      - ./mosquitto/config:/mosquitto/config
      - ./mosquitto/certs:/mosquitto/certs:ro
      - mosquitto_data:/mosquitto/data
    networks: [plantos]

  influxdb:
    image: influxdb:2
    restart: unless-stopped
    environment:
      DOCKER_INFLUXDB_INIT_MODE: setup
      DOCKER_INFLUXDB_INIT_USERNAME: admin
      DOCKER_INFLUXDB_INIT_PASSWORD: ${INFLUX_ADMIN_PW}
      DOCKER_INFLUXDB_INIT_ORG: plantos
      DOCKER_INFLUXDB_INIT_BUCKET: vitals
      DOCKER_INFLUXDB_INIT_RETENTION: 104w           # ~2 years
      DOCKER_INFLUXDB_INIT_ADMIN_TOKEN: ${INFLUX_TOKEN}
    volumes:
      - influxdb_data:/var/lib/influxdb2
    networks: [plantos]
    # no host port needed; only Telegraf + Grafana reach it over the docker net

  telegraf:
    image: telegraf:1
    restart: unless-stopped
    depends_on: [mosquitto, influxdb]
    environment:
      MQTT_TELEGRAF_PW: ${MQTT_TELEGRAF_PW}
      INFLUX_TOKEN: ${INFLUX_TOKEN}
    volumes:
      - ./telegraf/telegraf.conf:/etc/telegraf/telegraf.conf:ro
    networks: [plantos]

  grafana:
    image: grafana/grafana:latest
    restart: unless-stopped
    depends_on: [influxdb]
    environment:
      GF_SECURITY_ADMIN_PASSWORD: ${GF_ADMIN_PW}
      GF_SERVER_ROOT_URL: "%(protocol)s://%(domain)s/"
    ports:
      - "127.0.0.1:3000:3000"   # bind to localhost; reach via SSH tunnel or reverse proxy
    volumes:
      - ./grafana/provisioning:/etc/grafana/provisioning
      - ./grafana/dashboards:/var/lib/grafana/dashboards
      - grafana_data:/var/lib/grafana
    networks: [plantos]

networks:
  plantos:
volumes:
  mosquitto_data:
  influxdb_data:
  grafana_data:
```

`.env` (gitignore it):
```
INFLUX_TOKEN=<long-random-token>
INFLUX_ADMIN_PW=<strong>
GF_ADMIN_PW=<strong>
MQTT_TELEGRAF_PW=<strong>
```

### 5. Grafana provisioning
- `grafana/provisioning/datasources/influxdb.yml`: InfluxDB datasource, **query
  language Flux**, URL `http://influxdb:8086`, org `plantos`, default bucket `vitals`,
  token `${INFLUX_TOKEN}` (use `$__file` or env via `secureJsonData.token`).
- `grafana/provisioning/dashboards/dashboards.yml`: point Grafana at
  `/var/lib/grafana/dashboards`.
- `grafana/dashboards/plantos-vitals.json`: a starter dashboard with time-series
  panels for **pH**, **EC**, **water_temp + air_temp**, **humidity/pressure**, and a
  state-timeline for the three water-level booleans. Example Flux query per panel:
  ```flux
  from(bucket: "vitals")
    |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
    |> filter(fn: (r) => r._measurement == "vitals" and r._field == "ph")
    |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
  ```

### 6. Firewall / exposure
```bash
ufw allow 22/tcp
ufw allow 8883/tcp        # MQTT TLS — the only service exposed to the internet
ufw enable
```
Grafana is bound to `127.0.0.1:3000` — reach it via `ssh -L 3000:localhost:3000 …`,
or front it with Caddy/nginx + a real cert if you want public HTTPS access. **Do not**
expose InfluxDB or the 1883 listener.

## Verification (end-to-end)
1. `docker compose up -d`; `docker compose ps` all healthy; check `docker logs telegraf`
   for "Connected to MQTT".
2. **Broker reachability from the internet:** from a laptop,
   `mosquitto_sub --cafile ca.crt -h your.hetzner.host -p 8883 -u plantos -P <pw> -t 'plantos/vitals' -v`
   → a JSON line should appear every 30 s once the ESP32 is publishing.
3. **InfluxDB has data:** in the InfluxDB UI (or `influx query`), run the Flux above →
   non-empty series for `ph`, `ec`, temps.
4. **Grafana:** open the PlantOS Vitals dashboard → live time-series render.
5. **Resilience:** `docker compose restart mosquitto` — the ESP32 reconnects on its own
   and ingestion resumes (gaps are expected during the outage; this is acceptable for
   vitals at 30 s cadence).

## Hand-off back to firmware owner
- The **public hostname/IP** + port (8883) → firmware `mqtt_broker`.
- The **`plantos` MQTT password** → firmware `mqtt_password`.
- The **`ca.crt` PEM** → firmware `mqtt_ca_cert`.

## Open items / decisions to confirm on the server
- Public FQDN vs bare IP for the broker (drives the cert SAN in step 1).
- Whether to add MQTT ACLs (restrict `plantos` user to publish-only on `plantos/#`).
- Retention (`104w` default above) and downsampling tasks if disk becomes a concern.
- Optional: Let's Encrypt for Grafana behind a reverse proxy.
