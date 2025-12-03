#pragma once
#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include <vector>
#include <string>

// Forward declaration
class CentralStatusLogger;

namespace esphome {
namespace i2c_scanner {

/**
 * I2CScanner: I²C Bus Scanner with Critical Device Validation
 *
 * ============================================================================
 * COMPONENT OVERVIEW
 * ============================================================================
 *
 * This component scans the I²C bus for connected devices and validates the
 * presence of critical devices required by the PlantOS system. It provides
 * comprehensive diagnostic information via serial logging.
 *
 * ============================================================================
 * I²C ADDRESSING
 * ============================================================================
 *
 * I²C uses 7-bit addressing:
 * - Valid addresses: 0x01 to 0x77 (1 to 119 decimal)
 * - Reserved addresses:
 *   - 0x00: General call address
 *   - 0x78-0x7F: Reserved for future use and 10-bit addressing
 *
 * Common PlantOS device addresses:
 * - 0x48: ADS1115 16-bit ADC (A/D converter for analog sensors)
 * - 0x20: MCP23017 16-bit GPIO expander (digital I/O expansion)
 * - 0x76: BME280 environmental sensor (temp/humidity/pressure)
 * - 0x68: DS3231 RTC (real-time clock for timekeeping)
 *
 * ============================================================================
 * SCAN METHODOLOGY
 * ============================================================================
 *
 * The scanner uses ESPHome's I²C bus interface to probe each address:
 * 1. Send I²C START condition + address + WRITE bit
 * 2. Check for ACK (acknowledgment) from device
 * 3. Send I²C STOP condition
 * 4. Record result (ACK = device present, NAK = no device)
 *
 * WHY THIS WORKS:
 * - All I²C devices must respond to their address with ACK
 * - Unoccupied addresses return NAK (no acknowledgment)
 * - This is safe and non-destructive (no data is written)
 *
 * ============================================================================
 * CRITICAL DEVICE VALIDATION
 * ============================================================================
 *
 * The component maintains a list of critical devices that must be present
 * for PlantOS to function correctly. During scan:
 * - If critical device is found: Log confirmation
 * - If critical device is missing: Log ERROR with device name
 *
 * This provides immediate feedback about missing hardware during development
 * and deployment, preventing silent failures.
 *
 * ============================================================================
 * SCAN MODES
 * ============================================================================
 *
 * 1. BOOT-TIME SCAN (scan_on_boot: true)
 *    - Runs once during setup()
 *    - Ideal for hardware validation during startup
 *    - No ongoing overhead
 *
 * 2. PERIODIC SCAN (scan_interval: 30s)
 *    - Runs repeatedly at configured interval
 *    - Useful for detecting hot-plug events or hardware failures
 *    - Minimal overhead (scan completes in ~128ms for all addresses)
 *
 * 3. DISABLED (scan_on_boot: false, scan_interval: 0s)
 *    - No automatic scanning
 *    - Can still be triggered manually via scanI2CBus() method
 */
class I2CScanner : public Component, public i2c::I2CDevice {
 public:
  /**
   * setup() - Initialize the component and perform boot-time scan
   *
   * Called once at boot. If scan_on_boot is enabled, performs initial
   * I²C bus scan to detect all connected devices.
   */
  void setup() override;

  /**
   * loop() - Non-blocking periodic scan execution
   *
   * Called continuously by ESPHome's main event loop (~1000 Hz).
   * If scan_interval > 0, checks elapsed time and runs scan when due.
   */
  void loop() override;

  /**
   * dump_config() - Log component configuration
   *
   * Called during startup to display component settings in log output.
   * Shows scan settings and critical device list.
   */
  void dump_config() override;

  /**
   * scanI2CBus() - Perform I²C bus scan
   *
   * PUBLIC INTERFACE
   *
   * Scans all valid I²C addresses (0x01 to 0x77) and logs:
   * - List of found device addresses (hex format)
   * - Total device count
   * - Critical device validation results (found/missing)
   *
   * This function can be called manually or is triggered automatically
   * based on configuration (scan_on_boot, scan_interval).
   *
   * OUTPUT EXAMPLE:
   * [I2CScanner] I²C Bus Scan Results:
   * [I2CScanner] Found devices at addresses:
   * [I2CScanner]   0x20 (MCP23017 GPIO Expander) ✓
   * [I2CScanner]   0x48 (ADS1115 ADC) ✓
   * [I2CScanner]   0x3C
   * [I2CScanner] Total devices found: 3
   * [I2CScanner] ERROR: Critical device BME280 Sensor (0x76) NOT found!
   */
  void scanI2CBus();

  /**
   * Configuration setters (called by generated code from Python)
   */

  /**
   * set_scan_on_boot() - Enable/disable boot-time scan
   *
   * @param enable If true, scan runs once during setup()
   */
  void set_scan_on_boot(bool enable) { scan_on_boot_ = enable; }

  /**
   * set_scan_interval() - Set periodic scan interval
   *
   * @param interval Milliseconds between scans (0 = disabled)
   */
  void set_scan_interval(uint32_t interval) { scan_interval_ = interval; }

  /**
   * add_critical_device() - Register a critical device for validation
   *
   * @param address I²C address (7-bit, 0x01 to 0x77)
   * @param name Human-readable device name for logging
   *
   * Critical devices are checked during each scan. If missing, an ERROR
   * is logged with the device name for easy troubleshooting.
   */
  void add_critical_device(uint8_t address, const std::string &name);

  /**
   * set_status_logger() - Inject CentralStatusLogger reference
   *
   * @param logger Pointer to CentralStatusLogger for reporting scan results
   *
   * If set, scan results will be reported to the status logger for
   * inclusion in the unified system status report.
   */
  void set_status_logger(CentralStatusLogger *logger) { status_logger_ = logger; }

  /**
   * set_verbose() - Enable/disable detailed scan logging
   *
   * @param verbose If true, log detailed scan results to serial (default)
   *                If false, only report to status logger (silent mode)
   */
  void set_verbose(bool verbose) { verbose_ = verbose; }

 private:
  /**
   * Critical device structure
   *
   * Stores address and name of devices that must be present for
   * PlantOS to function correctly.
   */
  struct CriticalDevice {
    uint8_t address;     // I²C address (7-bit)
    std::string name;    // Human-readable device name
  };

  // ===== Configuration =====

  /**
   * Enable boot-time scan (default: true)
   *
   * If true, scanI2CBus() is called once in setup().
   */
  bool scan_on_boot_{true};

  /**
   * Periodic scan interval in milliseconds (default: 0 = disabled)
   *
   * If > 0, scanI2CBus() runs repeatedly at this interval.
   * If 0, only boot-time scan occurs (if enabled).
   */
  uint32_t scan_interval_{0};

  /**
   * List of critical devices to validate during scan
   *
   * Each scan checks if these devices are present and logs errors
   * for any missing devices.
   */
  std::vector<CriticalDevice> critical_devices_;

  // ===== Timing =====

  /**
   * Timestamp (millis()) of the last scan execution
   *
   * Used for non-blocking periodic scanning.
   */
  uint32_t last_scan_time_{0};

  /**
   * Flag to prevent duplicate boot-time scans
   *
   * Set to true after first setup() scan completes.
   */
  bool boot_scan_done_{false};

  /**
   * Pointer to CentralStatusLogger for reporting scan results
   *
   * If nullptr, scan results are only logged to serial.
   * If set, results are also reported to the status logger.
   */
  CentralStatusLogger *status_logger_{nullptr};

  /**
   * Verbose mode flag
   *
   * If true (default): Log detailed scan results to serial
   * If false: Silent mode - only report to status logger
   */
  bool verbose_{true};

  // ===== I²C Addressing Constants =====

  /**
   * First valid I²C address (7-bit addressing)
   *
   * 0x00 is reserved for general call, so scanning starts at 0x01.
   */
  static constexpr uint8_t I2C_ADDRESS_MIN = 0x01;

  /**
   * Last valid I²C address (7-bit addressing)
   *
   * 0x78-0x7F are reserved, so scanning ends at 0x77.
   */
  static constexpr uint8_t I2C_ADDRESS_MAX = 0x77;

  // ===== Helper Functions =====

  /**
   * checkDevicePresence() - Check if device responds at address
   *
   * @param address I²C address to probe (7-bit)
   * @return true if device acknowledged, false otherwise
   *
   * Attempts I²C communication at the specified address by:
   * 1. Setting the address on the I²C bus
   * 2. Attempting a write transaction
   * 3. Checking for ACK
   *
   * This is safe and non-destructive (no data is actually written).
   */
  bool checkDevicePresence(uint8_t address);

  /**
   * isCriticalDevice() - Check if address is a critical device
   *
   * @param address I²C address to check
   * @param name Output parameter to receive device name if found
   * @return true if address matches a critical device, false otherwise
   */
  bool isCriticalDevice(uint8_t address, std::string &name);
};

} // namespace i2c_scanner
} // namespace esphome
