# plantOS

ESPHome-based firmware for ESP32-C6 microcontrollers, implementing a plant monitoring and control system with custom sensor integration and LED visual feedback.

## Features

- 🌱 Modular sensor architecture with custom ESPHome components
- 💡 Finite state machine controller with RGB LED status indicators
- 🔄 Real-time monitoring via web interface and OTA updates
- 🛠️ Reproducible development environment using Nix flakes

## Hardware

- **MCU**: ESP32-C6-DevKitC-1 ([Product Page](https://www.espressif.com/en/products/socs/esp32-c6) | [User Guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/))
- **Built-in LED**: WS2812 RGB on GPIO 8
- **Framework**: ESP-IDF via ESPHome

See `plantOS.yaml` for complete hardware configuration details.

## Quick Start

### Prerequisites

- **Nix** (recommended): [Install Nix](https://nixos.org/download.html)
- **direnv** (optional but recommended): [Install direnv](https://direnv.net/docs/installation.html)

### Setup

```bash
# Clone and enter directory
cd plantOS

# Allow direnv (if using direnv)
direnv allow

# Or manually enter Nix development shell
nix develop

# Create secrets file from template
cp secrets.example.yaml secrets.yaml
# Edit secrets.yaml with your WiFi credentials

# Build and flash
task run
```

## Development Environment

The project uses **Nix flakes** for reproducible builds with all dependencies included:
- ESPHome
- Python 3
- ESP-IDF toolchain
- Task runner

**Nix Resources:**
- [Nix Downloads](https://nixos.org/download.html)
- [Nix Flakes](https://nixos.wiki/wiki/Flakes)
- [direnv + Nix](https://direnv.net/docs/hook.html)

## Common Commands

All commands managed via [Task](https://taskfile.dev/):

| Command | Description |
|---------|-------------|
| `task build` | Compile firmware |
| `task flash` | Upload to MCU |
| `task run` | Build, flash, and attach to logs |
| `task clean` | Clean build cache |

See `Taskfile.yml` for implementation details.

## Project Structure

```
plantOS/
├── plantOS.yaml           # Main ESPHome config (hardware, components, integrations)
├── secrets.yaml          # WiFi credentials (gitignored)
├── secrets.example.yaml  # Template for secrets
├── components/           # Custom ESPHome components
│   ├── sensor_dummy/    # Demo polling sensor
│   └── controller/      # FSM-based LED controller
├── flake.nix            # Nix development environment
├── Taskfile.yml         # Task automation
└── CLAUDE.md            # Detailed architecture documentation
```

**For detailed architecture**, component specifications, and development patterns, see [`CLAUDE.md`](CLAUDE.md).

## Custom Components

### sensor_dummy
Polling sensor that cycles values 0-100. See `components/sensor_dummy/` for implementation.

### controller
Finite state machine with LED feedback (init → calibration → ready → error states). See `components/controller/` for implementation details.

Usage examples in `plantOS.yaml`.

## Essential Documentation Links

### ESPHome
- [Homepage](https://esphome.io/)
- [Getting Started (CLI)](https://esphome.io/guides/getting_started_command_line.html)
- [Custom Components Guide](https://esphome.io/custom/custom_component.html)
- [API Reference](https://esphome.io/api/)
- [Configuration Types](https://esphome.io/guides/configuration-types.html)

### ESP32-C6
- [ESP32-C6 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf)
- [ESP32-C6 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-c6_technical_reference_manual_en.pdf)
- [DevKitC-1 User Guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/)
- [DevKitC-1 Schematics](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html#hardware-reference)

### ESP-IDF Framework
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/)
- [ESP-IDF API Reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/index.html)

### Development Tools
- [Task (Taskfile)](https://taskfile.dev/)
- [Nix Package Manager](https://nixos.org/manual/nix/stable/)
- [Nix Flakes Reference](https://nixos.wiki/wiki/Flakes)
- [direnv Documentation](https://direnv.net/)

### Components & Libraries
- [WS2812 Addressable LEDs](https://cdn-shop.adafruit.com/datasheets/WS2812.pdf)
- [ESPHome Light Component](https://esphome.io/components/light/index.html)
- [ESPHome Sensor Component](https://esphome.io/components/sensor/index.html)

## Development Workflow

1. **Modify hardware/components**: Edit `plantOS.yaml`
2. **Create custom components**: Add to `components/` directory (see ESPHome custom components guide)
3. **Test changes**: `task run` to build, flash, and monitor logs
4. **Access web interface**: Navigate to device IP (shown in logs) on port 80
5. **Debug**: Monitor serial output and ESPHome logs

## Troubleshooting

- **Build failures**: Run `task clean` to clear cache
- **Flash issues**: Check USB serial port permissions
- **WiFi connection**: Verify credentials in `secrets.yaml`
- **Nix environment**: Ensure `direnv allow` or manually `nix develop`
