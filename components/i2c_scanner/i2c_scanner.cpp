#include "i2c_scanner.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "../controller/CentralStatusLogger.h"

namespace esphome {
namespace i2c_scanner {

static const char *const TAG = "I2CScanner";

void I2CScanner::setup() {
  ESP_LOGI(TAG, "I²C Scanner component initialized");

  // Perform boot-time scan if enabled
  if (scan_on_boot_ && !boot_scan_done_) {
    ESP_LOGI(TAG, "Performing boot-time I²C bus scan...");
    scanI2CBus();
    boot_scan_done_ = true;
  }

  // Initialize timing for periodic scans
  last_scan_time_ = millis();
}

void I2CScanner::loop() {
  // Only run periodic scans if interval > 0
  if (scan_interval_ == 0) {
    return;
  }

  // Non-blocking timing check
  uint32_t now = millis();
  uint32_t elapsed = now - last_scan_time_;

  if (elapsed >= scan_interval_) {
    scanI2CBus();
    last_scan_time_ = now;
  }
}

void I2CScanner::dump_config() {
  ESP_LOGCONFIG(TAG, "I²C Scanner:");
  LOG_I2C_DEVICE(this);
  ESP_LOGCONFIG(TAG, "  Scan on boot: %s", scan_on_boot_ ? "YES" : "NO");

  if (scan_interval_ > 0) {
    ESP_LOGCONFIG(TAG, "  Scan interval: %.1f seconds", scan_interval_ / 1000.0f);
  } else {
    ESP_LOGCONFIG(TAG, "  Scan interval: DISABLED (boot-time only)");
  }

  if (!critical_devices_.empty()) {
    ESP_LOGCONFIG(TAG, "  Critical devices (%d):", critical_devices_.size());
    for (const auto &device : critical_devices_) {
      ESP_LOGCONFIG(TAG, "    - 0x%02X: %s", device.address, device.name.c_str());
    }
  } else {
    ESP_LOGCONFIG(TAG, "  Critical devices: NONE");
  }
}

void I2CScanner::scanI2CBus() {
  // Only log detailed output if verbose mode is enabled
  if (verbose_) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "I²C Bus Scan Starting...");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Scanning addresses 0x%02X to 0x%02X...", I2C_ADDRESS_MIN, I2C_ADDRESS_MAX);
  }

  // Storage for found devices
  std::vector<uint8_t> found_devices;

  // Scan all valid I²C addresses
  for (uint8_t address = I2C_ADDRESS_MIN; address <= I2C_ADDRESS_MAX; address++) {
    if (checkDevicePresence(address)) {
      found_devices.push_back(address);
    }
  }

  // ===== RESULTS SECTION =====
  if (verbose_) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Scan Complete!");
    ESP_LOGI(TAG, "========================================");

    if (found_devices.empty()) {
      ESP_LOGW(TAG, "No I²C devices found on the bus.");
      ESP_LOGW(TAG, "Check your wiring and pull-up resistors (typically 4.7kΩ on SDA/SCL).");
    } else {
      ESP_LOGI(TAG, "Found %d device(s) at the following addresses:", found_devices.size());

      // List all found devices with critical device annotations
      for (uint8_t address : found_devices) {
        std::string device_name;
        if (isCriticalDevice(address, device_name)) {
          // Critical device found - log with checkmark
          ESP_LOGI(TAG, "  → 0x%02X (%s) ✓", address, device_name.c_str());
        } else {
          // Unknown device
          ESP_LOGI(TAG, "  → 0x%02X", address);
        }
      }

      ESP_LOGI(TAG, "Total devices found: %d", found_devices.size());
    }

    // ===== CRITICAL DEVICE VALIDATION =====
    if (!critical_devices_.empty()) {
      ESP_LOGI(TAG, "========================================");
      ESP_LOGI(TAG, "Critical Device Validation");
      ESP_LOGI(TAG, "========================================");

      bool all_critical_found = true;

      for (const auto &critical : critical_devices_) {
        // Check if critical device was found during scan
        bool found = false;
        for (uint8_t address : found_devices) {
          if (address == critical.address) {
            found = true;
            break;
          }
        }

        if (found) {
          ESP_LOGI(TAG, "✓ FOUND:     %s (0x%02X)", critical.name.c_str(), critical.address);
        } else {
          ESP_LOGE(TAG, "✗ NOT FOUND: %s (0x%02X) - CRITICAL DEVICE MISSING!",
                   critical.name.c_str(), critical.address);
          all_critical_found = false;
        }
      }

      ESP_LOGI(TAG, "========================================");
      if (all_critical_found) {
        ESP_LOGI(TAG, "✓ All critical devices present!");
      } else {
        ESP_LOGE(TAG, "✗ One or more critical devices MISSING!");
        ESP_LOGE(TAG, "  System may not function correctly.");
        ESP_LOGE(TAG, "  Check device connections and addresses.");
      }
    }

    ESP_LOGI(TAG, "========================================");
  }

  // ===== REPORT TO CENTRAL STATUS LOGGER =====
  if (status_logger_ != nullptr) {
    // Build device list for status logger
    std::vector<I2CDeviceInfo> devices;

    // Add all found devices
    for (uint8_t address : found_devices) {
      std::string device_name;
      bool is_critical = isCriticalDevice(address, device_name);

      if (is_critical) {
        devices.push_back(I2CDeviceInfo(address, device_name, true, true));
      } else {
        // Unknown device - just show address
        char addr_str[16];
        snprintf(addr_str, sizeof(addr_str), "Unknown (0x%02X)", address);
        devices.push_back(I2CDeviceInfo(address, std::string(addr_str), true, false));
      }
    }

    // Add missing critical devices
    for (const auto &critical : critical_devices_) {
      bool found = false;
      for (uint8_t address : found_devices) {
        if (address == critical.address) {
          found = true;
          break;
        }
      }

      if (!found) {
        devices.push_back(I2CDeviceInfo(critical.address, critical.name, false, true));
      }
    }

    // Report to status logger
    status_logger_->updateI2CHardwareStatus(devices);
  }
}

void I2CScanner::add_critical_device(uint8_t address, const std::string &name) {
  // Validate address range
  if (address < I2C_ADDRESS_MIN || address > I2C_ADDRESS_MAX) {
    ESP_LOGW(TAG, "Invalid I²C address 0x%02X for critical device '%s' (valid range: 0x%02X-0x%02X)",
             address, name.c_str(), I2C_ADDRESS_MIN, I2C_ADDRESS_MAX);
    return;
  }

  // Add to critical devices list
  critical_devices_.push_back({address, name});
  ESP_LOGD(TAG, "Registered critical device: %s at address 0x%02X", name.c_str(), address);
}

bool I2CScanner::checkDevicePresence(uint8_t address) {
  // ESPHome I2C interface method to probe device
  // This sends START + ADDRESS + WRITE, checks for ACK, sends STOP
  // Returns true if device acknowledged (ACK), false otherwise (NAK)

  // Set the address for this transaction
  this->set_i2c_address(address);

  // Attempt to write 0 bytes (just probe for ACK)
  // write() returns ErrorCode::ERROR_OK if device acknowledged
  auto err = this->write(nullptr, 0);

  return err == i2c::ERROR_OK;
}

bool I2CScanner::isCriticalDevice(uint8_t address, std::string &name) {
  for (const auto &device : critical_devices_) {
    if (device.address == address) {
      name = device.name;
      return true;
    }
  }
  return false;
}

} // namespace i2c_scanner
} // namespace esphome
