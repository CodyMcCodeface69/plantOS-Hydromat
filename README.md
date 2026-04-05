# plantOS — Hydromat Mk.1

ESPHome firmware for the **Hydromat Mk.1**, an ESP32-C6 based autonomous hydroponic controller. Manages pH correction, nutrient dosing, water levels, lighting, and aeration — fully automated, with a web UI for manual control.

## Features

- **Autonomous pH correction** — EZO pH UART sensor, timed acid dosing, air pump mixing
- **Nutrient dosing** — 3-pump feeding schedule (A/B/C nutrients), clock-triggered
- **Water management** — Fill and drain with 3-point capacitive level sensing (HIGH/LOW/EMPTY)
- **Shelly integration** — HTTP API control of grow lights and air pump (on/off patterns, sequences)
- **TDS/EC monitoring** — Conductivity tracking with temperature compensation
- **3-layer HAL architecture** — Controller → SafetyGate → Hardware abstraction
- **15-state FSM** — Full system lifecycle with LED visual feedback per state
- **Crash recovery** — NVS persistent state survives power loss
- **Web UI** — Real-time sensor data, manual overrides, maintenance mode
- **OTA updates** — Flash over WiFi, no USB needed after initial flash
- **Reproducible builds** — Nix flakes dev environment

## Hardware

| Component | Part | Interface |
|-----------|------|-----------|
| MCU | ESP32-C6-DevKitC-1 | — |
| pH sensor | Atlas Scientific EZO (UART) | GPIO20/21 |
| Temperature | DS18B20 waterproof probe | GPIO23 (1-Wire) |
| TDS/EC sensor | Analog TDS probe | GPIO0 (ADC) |
| Water level | 3x XKC-Y23-V capacitive | GPIO9/10/11 |
| Acid pump | Peristaltic pump | GPIO19 |
| Nutrient pumps | 3x peristaltic (A/B/C) | GPIO20/21/22 |
| Water valve | Solenoid | GPIO18 |
| Grow light + air pump | Shelly smart plug | HTTP API |
| Status LED | WS2812 RGB (built-in) | GPIO8 |

See `PINOUT.md` for full wiring details and `plantOS.yaml` for ESPHome configuration.

## Custom Components

| Component | Purpose |
|-----------|---------|
| `plantos_controller` | 15-state FSM orchestrating all system behavior + LED feedback |
| `plantos_hal` | Hardware abstraction layer (wraps ESPHome components) |
| `actuator_safety_gate` | Centralized actuator validation: debouncing, duration limits, soft-start/stop |
| `persistent_state_manager` | Critical event logging to NVS for power-loss recovery |
| `ezo_ph_uart` | Atlas Scientific EZO pH sensor driver over UART |
| `sensor_filter` | Sliding window averaging with outlier rejection |
| `calendar_manager` | 120-day grow cycle schedule |
| `wdt_manager` | Hardware watchdog with 10s timeout |
| `i2c_scanner` | I2C bus diagnostics and device validation |
| `sensor_dummy` | Simulated sensor for development/testing |
| `dummy_actuator_trigger` | SafetyGate test sequences |
| `psm_checker` | PSM crash recovery testing |

## Quick Start

### Prerequisites

- **Nix** (recommended): [nixos.org/download](https://nixos.org/download.html)
- **direnv** (optional): [direnv.net](https://direnv.net/docs/installation.html)

### Setup

```bash
# Clone the repo
git clone https://github.com/CodyMcCodeface69/plantOS-Hydromat.git
cd plantOS-Hydromat

# Enter dev environment (direnv or manual)
direnv allow
# or: nix develop

# Configure secrets
cp secrets.example.yaml secrets.yaml
# Edit secrets.yaml with your WiFi credentials and network settings

# Build and flash
task run
```

## Commands

| Command | Description |
|---------|-------------|
| `task build` | Compile firmware |
| `task flash` | Upload to MCU |
| `task run` | Build, flash, and stream logs |
| `task snoop` | Attach to logs only (no flash) |
| `task clean` | Clear build cache |

## Architecture

```
Unified Controller (FSM, 15 states)
        │
        ▼
Actuator Safety Gate (debounce, duration limits, soft-start)
        │
        ▼
HAL — Hardware Abstraction Layer
  ├── GPIO pumps/valves
  ├── EZO pH sensor (UART)
  ├── DS18B20 temperature (1-Wire)
  ├── TDS sensor (ADC)
  ├── Water level sensors (binary)
  ├── WS2812 RGB LED
  └── Shelly HTTP API (lights, air pump)
```

For full architecture details, FSM state diagram, and component API docs see [`CLAUDE.md`](CLAUDE.md).

## Troubleshooting

- **Build fails**: `task clean` then retry
- **WiFi not connecting**: Check `secrets.yaml`, or connect to `PlantOS-Fallback` AP and go to `http://192.168.4.1`
- **pH not reading**: Wait 5min after boot for sensor warmup; check UART wiring on GPIO20/21
- **Actuator not activating**: Check SafetyGate logs — maintenance mode may be on, or duration limit hit
