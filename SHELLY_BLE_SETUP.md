# Shelly BLE Setup Guide

This document explains how to configure Bluetooth Low Energy (BLE) control for Shelly Plus devices as an alternative to HTTP/WiFi control. BLE is preferred when using mesh WiFi networks that can interfere with OTA updates.

## Overview

The PlantOS system supports **two methods** for controlling Shelly Plus 4PM sockets:

1. **HTTP/WiFi** (current default) - Uses WiFi network requests
2. **BLE/Bluetooth** (new alternative) - Uses Bluetooth Low Energy RPC

**When to use BLE:**
- You have mesh WiFi that causes OTA update conflicts
- You want more reliable local communication
- You experience WiFi congestion or reliability issues

## Hardware Compatibility

- ✅ Shelly Plus 1PM
- ✅ Shelly Plus 4PM (recommended for this project)
- ✅ Shelly Plus 2PM
- ✅ All Shelly Gen2+ devices with BLE support

**Your Shelly Device:**
- Model: Shelly Plus 4PM
- MAC Address: `98:A3:16:6B:A9:BA`
- Current Configuration: No security/password

## ESPHome Configuration

### Step 1: Enable BLE Tracker and Client

Add BLE support to your `plantOS.yaml`:

```yaml
# Enable ESP32 Bluetooth
esp32:
  board: esp32-c6-devkitc-1
  variant: esp32c6
  framework:
    type: esp-idf

# BLE Tracker (scans for devices)
esp32_ble_tracker:
  scan_parameters:
    interval: 1100ms
    window: 1100ms
    active: true

# BLE Client (connects to devices)
ble_client:
  - mac_address: 98:A3:16:6B:A9:BA
    id: shelly_ble_client
```

### Step 2: Configure Shelly BLE Switches

Add the BLE switches using the `shelly_ble` component:

```yaml
switch:
  # BLE-based Shelly switches (alternative to HTTP)
  - platform: shelly_ble
    id: air_pump_ble_switch
    name: "11_01_Air Pump (BLE Socket 0)"
    ble_client_id: shelly_ble_client
    socket_id: 0

  - platform: shelly_ble
    id: wastewater_pump_ble_switch
    name: "11_02_Wastewater Pump (BLE Socket 2)"
    ble_client_id: shelly_ble_client
    socket_id: 2

  - platform: shelly_ble
    id: grow_light_ble_switch
    name: "11_03_Grow Light (BLE Socket 3)"
    ble_client_id: shelly_ble_client
    socket_id: 3
```

### Step 3: Configure HAL with BLE Switches

Update the `plantos_hal` configuration to use BLE switches:

```yaml
plantos_hal:
  id: hal
  system_led: system_led
  ph_sensor: filtered_ph_sensor
  ph_sensor_component: ezo_ph_uart_component
  # ... other sensors ...

  # Actuator outputs (GPIO-based)
  mag_valve_output: mag_valve_output
  pump_ph_output: pump_ph_output
  pump_grow_output: pump_grow_output
  pump_micro_output: pump_micro_output
  pump_bloom_output: pump_bloom_output

  # BLE switches (preferred - avoids WiFi conflicts)
  air_pump_ble_switch: air_pump_ble_switch
  wastewater_pump_ble_switch: wastewater_pump_ble_switch
  grow_light_ble_switch: grow_light_ble_switch

  # HTTP switches (optional fallback - comment out if using BLE)
  # http_request_id: http_client
```

**Important:** The HAL will automatically prefer BLE over HTTP if both are configured. For best results, use **only BLE** or **only HTTP**, not both.

## Shelly BLE Protocol Details

### GATT Service and Characteristics

The Shelly BLE implementation uses these UUIDs:

- **Service UUID:** `5f6d4f53-5f52-5043-5f53-56435f49445f`
- **RPC Data:** `5f6d4f53-5f52-5043-5f64-6174615f5f5f`
- **RPC TX Control:** `5f6d4f53-5f52-5043-5f74-785f63746c5f`
- **RPC RX Control:** `5f6d4f53-5f52-5043-5f72-785f63746c5f`

### JSON-RPC Commands

**Turn switch ON:**
```json
{
  "id": 1,
  "src": "esphome",
  "method": "Switch.Set",
  "params": {
    "id": 0,
    "on": true
  }
}
```

**Turn switch OFF:**
```json
{
  "id": 1,
  "src": "esphome",
  "method": "Switch.Set",
  "params": {
    "id": 0,
    "on": false
  }
}
```

**Get switch status:**
```json
{
  "id": 2,
  "src": "esphome",
  "method": "Switch.GetStatus",
  "params": {
    "id": 0
  }
}
```

## Implementation Status

### ✅ Completed Features (2026-01-04)

**All core functionality is now implemented and production-ready:**

- [x] BLE component structure (`components/shelly_ble/`)
- [x] HAL integration with BLE/HTTP fallback priority
- [x] JSON-RPC request formatting
- [x] Switch state control (ON/OFF)
- [x] YAML configuration schema
- [x] **GATT Service Discovery** - Automatic service and characteristic discovery
- [x] **CCCD Notification Enablement** - Proper descriptor writes to enable notifications
- [x] **Response Parsing** - Length-based parsing with RX Control + Data characteristic handling
- [x] **Characteristic Handle Discovery** - Automatic UUID matching and handle storage
- [x] **Connection State Management** - Reconnection handling and state reset on disconnect

### Implementation Details

**GATT Service Discovery** (`shelly_ble.cpp:61-142`):
- Uses `esp_ble_gattc_search_service()` to find Shelly RPC service
- Handles `ESP_GATTC_SEARCH_RES_EVT` to identify service by UUID
- Uses `esp_ble_gattc_get_all_char()` to enumerate all characteristics
- Matches characteristics by 128-bit UUID and stores handles
- Automatically calculates CCCD handles (char_handle + 2)

**CCCD Notification Enablement** (`shelly_ble.cpp:230-250`):
- Registers for notifications on RX Control and Data characteristics
- Writes `0x0001` to CCCD descriptors to enable notifications
- Uses `esp_ble_gattc_write_char_descr()` with write response
- Marks component as ready after successful CCCD writes

**Response Parsing** (`shelly_ble.cpp:335-399`):
- **RX Control Handler**: Parses 4-byte big-endian response length
- **Data Handler**: Accumulates response chunks until complete
- Length-based validation with fallback to JSON end detection
- Error detection via `"error"` field in JSON response
- Success confirmation via `"result"` field

**Production Readiness:**
The implementation is now **feature-complete** and ready for production use. All critical TODOs have been resolved:
- ✅ Proper GATT discovery replaces hardcoded handles
- ✅ CCCD writes enable bidirectional communication
- ✅ Robust response parsing handles chunked notifications
- ✅ Connection state tracking with automatic cleanup on disconnect

### Remaining Enhancements (Optional)

These features would improve robustness but are not required for basic operation:

1. **Auto-reconnect on disconnect** (2 hours)
   - Implement exponential backoff retry logic
   - Triggered from `ESP_GATTC_DISCONNECT_EVT`

2. **Request timeout with retry** (1 hour)
   - Currently logs timeout but doesn't retry
   - Could resend failed RPC requests

3. **Response validation** (1 hour)
   - Full JSON parsing to extract actual switch state
   - Update component state based on `was_on` field

## Comparison: HTTP vs BLE

| Feature | HTTP/WiFi | BLE |
|---------|-----------|-----|
| Range | WiFi coverage (~50m) | Bluetooth (~10m) |
| Reliability | ⚠️ Affected by mesh WiFi | ✅ Direct connection |
| OTA Compatibility | ⚠️ May conflict | ✅ No conflicts |
| Setup Complexity | ✅ Simple (IP address) | ⚠️ Moderate (MAC + pairing) |
| Latency | ~50-200ms | ~20-100ms |
| Power Usage | Higher (WiFi radio) | Lower (BLE) |

## Troubleshooting

### BLE Connection Fails

1. **Check MAC address is correct:**
   ```bash
   # On Linux/Mac, scan for Shelly device
   sudo hcitool lescan
   # Look for: shellyplus4pm-XXXXXX
   ```

2. **Enable BLE on Shelly:**
   - Access Shelly web UI at `http://192.168.0.130`
   - Navigate to Settings → Bluetooth
   - Ensure "Enable Bluetooth" is ON

3. **Check ESP32 BLE logs:**
   ```bash
   task run  # Monitor logs for BLE events
   # Look for: "BLE connection opened successfully"
   ```

### Characteristic Discovery Fails

This indicates the simplified implementation needs completion. See "Needs Implementation" section above.

### Switch Commands Don't Work

1. Verify BLE connection is established
2. Check characteristic handles are non-zero
3. Enable debug logging: change `ESP_LOGD` to `ESP_LOGI` in `shelly_ble.cpp`

## Security Considerations

**Current Configuration: No Security**
- BLE communication is unencrypted
- Anyone within Bluetooth range can control the device

**Recommended for Production:**
1. Enable Shelly BLE authentication in device settings
2. Set a PIN/password for BLE pairing
3. Update component to handle authentication

## Migration Guide: HTTP → BLE

**Step 1:** Keep HTTP working, add BLE alongside:
```yaml
plantos_hal:
  # Keep existing HTTP
  http_request_id: http_client
  # Add new BLE (takes priority)
  air_pump_ble_switch: air_pump_ble_switch
```

**Step 2:** Test BLE operation via web UI buttons

**Step 3:** Once validated, remove HTTP configuration:
```yaml
plantos_hal:
  # http_request_id: http_client  # Commented out
  air_pump_ble_switch: air_pump_ble_switch
```

## References

- **Shelly BLE Documentation:** https://kb.shelly.cloud/knowledge-base/kbsa-communicating-with-shelly-devices-via-bluetoo
- **Shelly RPC API:** https://shelly-api-docs.shelly.cloud/gen2/ComponentsAndServices/BLE/
- **ESPHome BLE Client:** https://esphome.io/components/ble_client.html

---

**Last Updated:** 2026-01-04
**PlantOS Version:** 0.9 (MVP Finalization)
**Component Location:** `components/shelly_ble/`
