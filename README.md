# plantOS — Hydromat Mk.1

ESPHome firmware for the **Hydromat Mk.1**, an ESP32-C6 controller that runs a hydroponic tank
on its own: pH correction, EC-based nutrient feeding, water fill and drain, reservoir change,
night mode and grow-light scheduling over a 120-day grow calendar. A web UI on the device allows
manual control.

## Hardware

| Part | Interface |
|---|---|
| Waveshare ESP32-C6-DEV-KIT-N8 | |
| Atlas Scientific EZO pH circuit | UART, GPIO18 / 19 |
| DS18B20 water temperature | 1-Wire, GPIO3 |
| KS0429 TDS/EC probe | ADC, GPIO0 |
| 3× XKC-Y23-V water level (HIGH / LOW / EMPTY) | GPIO17 / 23 / 22 |
| BME280 air temperature, humidity, pressure | I2C, GPIO20 / 21 |
| Water valve, acid pump, nutrient pumps A / B / C | PWM outputs, GPIO1 / 4 / 5 / 6 / 7 |
| Air pump, wastewater pump, grow light | Shelly Plus 4PM over HTTP |
| Status LED (WS2812, on the board) | GPIO8 |

Full pin table: [`PINOUT.md`](PINOUT.md). PCB files: `Hardware/`.

## Architecture

```
plantos_controller     state machine (22 states), all business logic, LED behaviors
        │
actuator_safety_gate   checks every actuator command (max durations, duplicate commands)
        │
plantos_hal            the only code that touches hardware (GPIO/PWM, sensors, LED, Shelly HTTP)
```

The custom components are in `components/`; the device configuration is `plantOS.yaml`.

## Build

The firmware needs **ESPHome 2025.4.2** and [`task`](https://taskfile.dev).

```bash
git clone https://github.com/CodyMcCodeface69/plantOS-Hydromat.git
cd plantOS-Hydromat

# Environment, one of:
nix develop                      # or: direnv allow
source .venv/bin/activate        # a Python venv with esphome==2025.4.2

cp secrets.example.yaml secrets.yaml   # then fill in WiFi, MQTT and network values

task config_validate   # check plantOS.yaml
task build             # compile
task run               # build + flash over USB + logs (OTA=true flashes over WiFi)
task snoop             # logs only
task clean             # clear the build cache
```

`secrets.yaml` is gitignored. Never commit it.

## Documentation

| File | Content |
|---|---|
| [`AGENTS.md`](AGENTS.md) (`CLAUDE.md` is a symlink to it) | Architecture, coding rules, component inventory, known issues |
| [`FSMINFO.md`](FSMINFO.md) | The state machine: states, transitions, triggers, timeouts. Authoritative. |
| [`PINOUT.md`](PINOUT.md) | Pin table and wiring notes |
| [`docs/README.md`](docs/README.md) | Index of the topic docs with their trust level |
| [`TODO.md`](TODO.md) | Open items, maintained by hand |
| [`CHANGELOG.md`](CHANGELOG.md) | History up to 0.9.1 (December 2025); after that, see `git log` |

## Troubleshooting

- **Build fails after a toolchain or ESPHome change**: `task clean`, then build again.
- **WiFi does not connect**: check `secrets.yaml`, or join the fallback access point
  `PlantOS-Fallback` and open `http://192.168.4.1`.
- **An actuator does not switch**: look for ActuatorSafetyGate messages in the log (duration limit
  reached, or the command matched the current state). The Shelly-driven actuators also need the
  "Shelly Integration" switch on.
- **Crash or reboot**: `task decode -- 0x...` resolves crash addresses; `docs/LESSONS.md` lists
  the known causes on this chip.
