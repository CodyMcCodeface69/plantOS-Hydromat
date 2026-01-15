# ISR Alignment Crash Fix - ESP32-C6 (RISC-V)

**Date**: 2026-01-14 (Updated 2026-01-15)
**Issue**: Store address misaligned exception (MCAUSE: 0x00000007)
**Platform**: ESP32-C6 (RISC-V), ESPHome
**Status**: ⚠️ PARTIALLY FIXED (ISR logging removed, but did NOT resolve crash)

**UPDATE 2026-01-15**: The ISR logging fix was necessary for best practices, but **did NOT resolve the crash**. The actual root cause was **struct alignment in CriticalEventLog**. See `RISC-V_ALIGNMENT_FIX.md` for the complete fix.

## Problem Summary

The ESP32-C6 was experiencing recurring crashes with the following signature:

```
MCAUSE: 0x00000007 (Store address misaligned)
Abort PC: 0x420dcf97
RA: 0x40808482 (xQueueGiveFromISR)
Faulty Address (A1): 0x408264fe (NOT 4-byte aligned!)
Related Strings: "AutoFeedEnable", "Sensors", "Status", "logger"
```

## Root Cause

**ESPHome sensor callbacks can be triggered from ISR (Interrupt Service Routine) context**, particularly for:
- ADC sensors (analog readings)
- UART sensors (EZO pH sensor)
- I2C sensors (BME280, etc.)
- Dallas temperature sensors

The code was calling **ESP_LOGI/ESP_LOGD/ESP_LOGW inside these ISR callbacks**, which internally use **FreeRTOS queues** (`xQueueSendFromISR`). On RISC-V architecture (ESP32-C6), all queue operations require **4-byte aligned pointers**, but the logging system was passing misaligned data structures, causing the crash.

### Why It Crashed

1. Sensor interrupt fires (temperature sensor update)
2. ESPHome calls registered callback from ISR context
3. Callback executes `ESP_LOGI(TAG, "Temperature changed: %.1f°C", temp)`
4. ESP_LOGI internally calls `xQueueSendFromISR` to queue log message
5. Log message buffer has misaligned address (0x408264fe)
6. **RISC-V hardware exception** - Store address misaligned
7. System crashes and reboots

## Files Modified

### 1. `components/plantos_controller/controller.h`

**Added ISR-safe flags** (lines 341-348):

```cpp
// ========================================================================
// Sensor Change Flags (ISR-safe)
// ========================================================================
// These flags are set by sensor callbacks which may run in ISR context.
// Using volatile to ensure proper memory synchronization.

volatile bool temperature_changed_{false};  // Set by ISR callback, cleared by loop()
float last_temperature_{0.0f};              // Last temperature value received
```

### 2. `components/plantos_controller/controller.cpp`

**Old Code (CRASHES)**:
```cpp
hal_->onTemperatureChange([this](float temp) {
    if (status_logger_.isVerboseMode()) {
        ESP_LOGI(TAG, "Temperature changed: %.1f°C", temp);  // ❌ CRASHES IN ISR!
    }
});
```

**New Code (ISR-SAFE)** (lines 67-80):
```cpp
// Register temperature change callback (ISR-safe - no logging in callback!)
// Temperature sensor callbacks can be triggered from ISR context (ADC interrupts),
// so we must NOT call ESP_LOGI or any FreeRTOS queue operations here.
// Instead, we set a volatile flag and handle logging in the main loop.
if (hal_->hasTemperature()) {
    hal_->onTemperatureChange([this](float temp) {
        // ISR-SAFE: Only update volatile members, no logging!
        last_temperature_ = temp;
        temperature_changed_ = true;
    });
    ESP_LOGI(TAG, "Temperature sensor callback registered (ISR-safe)");
}
```

**Deferred Logging in Main Loop** (lines 116-125):
```cpp
// Handle temperature change notifications (ISR-safe deferred logging)
// The callback sets temperature_changed_ flag from ISR context,
// and we do the actual logging here in the main loop to avoid
// calling ESP_LOGI from ISR which causes alignment crashes on RISC-V.
if (temperature_changed_) {
    temperature_changed_ = false;  // Clear flag
    if (status_logger_.isVerboseMode()) {
        ESP_LOGI(TAG, "Temperature changed: %.1f°C", last_temperature_);
    }
}
```

### 3. `components/sensor_filter/sensor_filter.h`

**Added ISR-safe flags** (lines 120-129):

```cpp
// ISR-safe deferred logging flags
// Sensor callbacks may run in ISR context, so we cannot log there.
// Instead, we set these flags and handle logging in loop().
volatile bool has_new_reading_{false};
volatile bool has_nan_reading_{false};
volatile bool has_filtered_value_{false};
float last_raw_value_{0.0f};
int last_buffer_count_{0};
float last_filtered_value_{0.0f};
int last_readings_count_{0};
```

**Changed loop() from empty to active** (line 100):

```cpp
void loop() override;  // Now implements ISR-safe deferred logging
```

### 4. `components/sensor_filter/sensor_filter.cpp`

**Removed ALL logging from callback** (lines 48-112):

```cpp
void SensorFilter::on_sensor_update(float value) {
  // ISR-SAFE DESIGN:
  // This callback may be called from ISR context (sensor ADC/UART interrupts).
  // We CANNOT use ESP_LOG* functions here as they use FreeRTOS queues which
  // can cause alignment crashes on RISC-V (ESP32-C6).
  // Instead, we set volatile flags and defer logging to loop().

  if (std::isnan(value)) {
    has_nan_reading_ = true;  // Flag for deferred logging
    return;
  }

  averager_->addReading(value);

  // Store values for deferred logging in loop()
  last_raw_value_ = value;
  last_buffer_count_ = averager_->getCount();
  has_new_reading_ = true;

  if (averager_->isReady()) {
    float filtered_value = averager_->getRobustAverage();
    publish_state(filtered_value);

    // Store for deferred logging
    last_filtered_value_ = filtered_value;
    last_readings_count_ = window_size_;
    has_filtered_value_ = true;
  }
}
```

**Added loop() for deferred logging** (lines 114-146):

```cpp
void SensorFilter::loop() {
  // ISR-safe deferred logging.
  // Sensor callbacks (on_sensor_update) can be called from ISR context,
  // where logging is unsafe. We handle all logging here in the main loop.

  if (has_nan_reading_) {
    has_nan_reading_ = false;
    ESP_LOGW(TAG, "Received NaN from source sensor - skipping");
  }

  if (has_new_reading_) {
    has_new_reading_ = false;
    ESP_LOGD(TAG, "Raw reading: %.2f  [Buffer: %d/%d]",
             last_raw_value_, last_buffer_count_, window_size_);
  }

  if (has_filtered_value_) {
    has_filtered_value_ = false;
    ESP_LOGI(TAG, "Robust average calculated: %.2f (from %d readings, %.0f%% outlier rejection)",
             last_filtered_value_, last_readings_count_, reject_percentage_ * 2 * 100);
    ESP_LOGD(TAG, "Buffer reset. Collecting next window...");
  }
}
```

## Verification Steps

1. **Build the firmware**:
   ```bash
   cd ~/plantOS-testlab
   task build
   ```
   ✅ Compiles successfully

2. **Flash to ESP32-C6**:
   ```bash
   task flash
   ```

3. **Monitor for crashes**:
   ```bash
   task snoop
   ```
   Look for:
   - No more "Store address misaligned" exceptions
   - Temperature sensor callbacks working correctly
   - Sensor filter logging appearing in main loop context

4. **Test temperature changes**:
   - Enable verbose mode via web UI
   - Trigger temperature changes (heat/cool the sensor)
   - Verify logging appears without crashes

## RISC-V ISR Safety Rules

### ❌ NEVER Do This in ISR Callbacks

```cpp
// BAD: Logging from ISR
hal_->onTemperatureChange([this](float temp) {
    ESP_LOGI(TAG, "Temperature: %.1f", temp);  // CRASHES on RISC-V!
});

// BAD: String operations
hal_->onPhChange([this](float ph) {
    std::string msg = "pH: " + std::to_string(ph);  // CRASHES!
});

// BAD: Complex objects with misaligned members
struct MyData {
    uint8_t flag;
    float value;  // Misaligned on RISC-V!
};
```

### ✅ ALWAYS Do This Instead

```cpp
// GOOD: Set volatile flag, defer logging to loop()
volatile bool temp_changed_{false};
float last_temp_{0.0f};

hal_->onTemperatureChange([this](float temp) {
    last_temp_ = temp;
    temp_changed_ = true;  // ISR-safe!
});

void loop() {
    if (temp_changed_) {
        temp_changed_ = false;
        ESP_LOGI(TAG, "Temperature: %.1f", last_temp_);  // Safe in main loop
    }
}
```

### ISR-Safe Operations

**Allowed in ISR**:
- Setting volatile flags
- Simple arithmetic on aligned variables
- Reading/writing properly aligned primitive types (uint32_t, float, etc.)
- Calling ISR-safe FreeRTOS functions (`xQueueSendFromISR`, but with aligned data!)

**FORBIDDEN in ISR**:
- `ESP_LOG*` macros (use FreeRTOS queues internally)
- `std::string` operations (heap allocation)
- Complex object construction (alignment issues)
- Blocking operations (`delay`, `vTaskDelay`, etc.)
- Non-ISR FreeRTOS functions (`xQueueSend`, `xTaskCreate`, etc.)

## Why RISC-V Is Stricter Than Xtensa

**ESP32 (Xtensa)**: Allows misaligned memory access (with performance penalty)
**ESP32-C6 (RISC-V)**: **Hardware exception on misaligned access** - instant crash!

RISC-V enforces strict 4-byte alignment for 32-bit operations. This is by design for performance and simplicity, but requires more careful coding.

## Testing Checklist

- [ ] Build succeeds without warnings
- [ ] Flash to device successfully
- [ ] No crashes during boot sequence
- [ ] Temperature sensor updates work (verbose mode)
- [ ] pH sensor updates work (if enabled)
- [ ] Sensor filter logging appears correctly
- [ ] 24-hour stability test (no crashes)
- [ ] Multiple sensor update cycles without issues

## Related Documentation

- RISC-V Alignment Requirements: https://riscv.org/wp-content/uploads/2017/05/riscv-spec-v2.2.pdf
- ESP-IDF ISR Guidelines: https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/system/intr_alloc.html
- FreeRTOS Queue API: https://www.freertos.org/a00118.html

## Commit Message Template

```
Fix RISC-V alignment crash in sensor callbacks (ESP32-C6)

- Remove ESP_LOG* calls from ISR-triggered sensor callbacks
- Add volatile flags for deferred logging in main loop
- Fix PlantOSController temperature callback (ISR-safe)
- Fix SensorFilter on_sensor_update callback (ISR-safe)
- Implement loop() deferred logging in SensorFilter

Resolves "Store address misaligned" (MCAUSE: 0x00000007) crashes
caused by FreeRTOS queue operations with misaligned pointers in
ISR context on RISC-V architecture.

Components modified:
- plantos_controller (controller.h, controller.cpp)
- sensor_filter (sensor_filter.h, sensor_filter.cpp)
```

---

**Author**: Claude Code
**Platform**: ESP32-C6 (RISC-V)
**ESPHome Version**: 2025.4.2
**Framework**: ESP-IDF 5.1.5

---

## Update: Actual Root Cause Found

**Date**: 2026-01-15

The ISR logging fixes implemented in this document **did not resolve the crash**. While removing ESP_LOG calls from ISR callbacks is **best practice** and prevents potential issues, the actual crash was caused by:

**Misaligned struct fields in `CriticalEventLog`**

The struct had `int64_t timestampSec` at offset 32 (after a 32-byte char array), which violated RISC-V's strict 8-byte alignment requirement. When NVS operations (using FreeRTOS queues internally) accessed this field during `psm->loadState("AutoFeedEnable")`, it caused the "Store address misaligned" exception.

### The Complete Fix

**See `RISC-V_ALIGNMENT_FIX.md` for the complete solution**, which involved:

1. **Reordering struct members** (largest to smallest alignment)
2. **Adding explicit alignment attribute** (`__attribute__((aligned(8)))`)
3. **Understanding RISC-V alignment requirements**

### What This Document Achieved

Even though this ISR fix didn't resolve the crash, it improved code quality by:
- ✅ Preventing potential future ISR-related crashes
- ✅ Following embedded systems best practices (no logging in ISR)
- ✅ Implementing deferred logging pattern correctly
- ✅ Making sensor callbacks ISR-safe

**Recommendation**: Keep both fixes in the codebase. The ISR-safe callbacks are correct design, and the struct alignment fix resolves the actual crash.

