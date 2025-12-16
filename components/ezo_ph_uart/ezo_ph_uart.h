#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace ezo_ph_uart {

/**
 * @brief EZO pH Sensor UART Component
 *
 * Communicates with Atlas Scientific EZO pH circuit via UART.
 * The EZO circuit uses ASCII text commands over UART (not binary protocol).
 *
 * Hardware Configuration:
 * - Baud Rate: 115200 (default for EZO carrier boards)
 * - Data Bits: 8
 * - Parity: None
 * - Stop Bits: 1
 *
 * Critical Timing:
 * - Must wait 300ms after sending command before reading response
 * - Commands must be terminated with carriage return (\r)
 *
 * Features:
 * - Three-point calibration (low, mid, high)
 * - Temperature compensation
 * - Stability detection via rolling standard deviation
 * - Range validation (pH -5.0 to 19.0)
 * - Automatic error recovery
 */
class EZOPHUARTComponent : public PollingComponent, public uart::UARTDevice {
 public:
  EZOPHUARTComponent() = default;

  // ============================================================================
  // Component Lifecycle Methods
  // ============================================================================

  /**
   * @brief Initialize the sensor on startup
   *
   * - Tests UART communication with device info command
   * - Disables continuous reading mode (we poll instead)
   * - Enables verbose response codes for better error handling
   */
  void setup() override;

  /**
   * @brief Poll sensor for pH reading (called at update_interval)
   *
   * - Sends temperature compensation if configured
   * - Requests single pH measurement
   * - Validates and publishes result
   */
  void update() override;

  /**
   * @brief Log component configuration
   */
  void dump_config() override;

  /**
   * @brief Setup priority for component initialization order
   */
  float get_setup_priority() const override { return setup_priority::DATA; }

  // ============================================================================
  // Sensor Configuration (called from Python code generation)
  // ============================================================================

  /**
   * @brief Set the pH sensor output
   * @param sensor Pointer to ESPHome sensor for publishing pH values
   */
  void set_ph_sensor(sensor::Sensor *sensor) { this->ph_sensor_ = sensor; }

  /**
   * @brief Set temperature compensation sensor
   * @param sensor Pointer to ESPHome sensor providing temperature in °C
   */
  void set_temperature_compensation(sensor::Sensor *sensor) { this->temp_sensor_ = sensor; }

  // ============================================================================
  // Calibration Methods (callable from ESPHome services)
  // ============================================================================

  /**
   * @brief Calibrate mid-point (usually pH 7.00)
   * @param ph_value The actual pH of the calibration buffer
   * @return true if calibration command succeeded
   */
  bool calibrate_mid(float ph_value);

  /**
   * @brief Calibrate low-point (usually pH 4.00)
   * @param ph_value The actual pH of the calibration buffer
   * @return true if calibration command succeeded
   */
  bool calibrate_low(float ph_value);

  /**
   * @brief Calibrate high-point (usually pH 10.00)
   * @param ph_value The actual pH of the calibration buffer
   * @return true if calibration command succeeded
   */
  bool calibrate_high(float ph_value);

  /**
   * @brief Clear all calibration data (factory reset calibration)
   */
  void calibrate_clear();

  /**
   * @brief Query and log current calibration status
   * Returns: 0, 1, 2, or 3 (number of calibration points completed)
   */
  void query_calibration_status();

  // ============================================================================
  // Advanced Features
  // ============================================================================

  /**
   * @brief Enable or disable the EZO circuit LED
   * @param enable True to turn LED on, false to turn off
   */
  void set_led(bool enable);

  /**
   * @brief Put the sensor into low-power sleep mode
   * Note: Sensor will not respond until power cycled or woken up
   */
  void enter_sleep_mode();

  /**
   * @brief Check if pH readings are stable
   * @return true if standard deviation is below threshold
   */
  bool is_stable();

  /**
   * @brief Check if sensor hardware is ready and responding
   * @return true if sensor is connected and responding to commands
   */
  bool is_sensor_ready() const { return sensor_ready_; }

  /**
   * @brief Take a single pH reading immediately (blocking)
   * @param value Output parameter for pH value
   * @return true if reading was successful
   *
   * WARNING: This method blocks for ~300ms while waiting for the sensor to respond.
   * Only use this for calibration sequences where blocking is acceptable.
   * For normal operation, use the update() polling mechanism instead.
   */
  bool take_single_reading(float &value);

  /**
   * @brief Get the last pH reading from the stability buffer
   * @return Most recent pH value, or 0.0 if no readings available
   */
  float get_last_reading() const;

  /**
   * @brief Calculate average of last N readings in stability buffer
   * @param count Number of readings to average (max STABILITY_BUFFER_SIZE)
   * @return Average pH value, or 0.0 if insufficient data
   */
  float get_average_reading(size_t count) const;

  /**
   * @brief Enable continuous reading mode
   *
   * In continuous mode, the sensor automatically outputs readings every second
   * without needing to send "R" command each time.
   * Note: This will interfere with normal polling mode operation.
   */
  void enable_continuous_reading();

  /**
   * @brief Disable continuous reading mode
   *
   * Returns sensor to command mode where it only outputs readings
   * when explicitly requested via "R" command. This is the default mode.
   */
  void disable_continuous_reading();

 protected:
  // ============================================================================
  // UART Communication Helpers
  // ============================================================================

  /**
   * @brief Send ASCII command to EZO circuit via UART
   * @param cmd Command string (will be terminated with \r)
   * @return true if command was sent successfully
   */
  bool send_command_(const char *cmd);

  /**
   * @brief Read ASCII response from EZO circuit via UART
   * @param buffer Buffer to store response
   * @param len Maximum buffer length
   * @return true if response was read successfully
   */
  bool read_response_(char *buffer, size_t len);

  /**
   * @brief Parse pH value from ASCII response
   * @param response Response string from sensor
   * @param value Output parameter for parsed pH value
   * @return true if parsing was successful
   */
  bool parse_ph_value_(const char *response, float &value);

  /**
   * @brief Check response code for errors
   * @param response Response string from sensor
   * @return true if response indicates success (*OK)
   *
   * Response codes:
   * - *OK: Command successful
   * - *ER: Command error
   * - *OV: Overvoltage warning
   * - *UV: Undervoltage warning
   * - *RS: Device reset detected
   */
  bool check_response_code_(const char *response);

  /**
   * @brief Wait for EZO circuit to process command
   *
   * Critical timing requirement: EZO circuits need 300ms to process
   * most commands before they can return a valid response.
   */
  void wait_for_response_();

  // ============================================================================
  // Stability Tracking Helpers
  // ============================================================================

  /**
   * @brief Add pH value to stability tracking buffer
   * @param value pH value to add
   */
  void update_stability_buffer_(float value);

  /**
   * @brief Calculate standard deviation of recent pH readings
   * @return Standard deviation in pH units
   */
  float calculate_std_deviation_();

  // ============================================================================
  // Member Variables
  // ============================================================================

  // Sensor pointers (set via configuration)
  sensor::Sensor *ph_sensor_{nullptr};       ///< pH output sensor
  sensor::Sensor *temp_sensor_{nullptr};     ///< Temperature compensation input

  // State tracking
  bool sensor_ready_{false};                 ///< True if sensor initialized successfully
  uint8_t error_count_{0};                   ///< Consecutive error counter for fault detection
  bool continuous_mode_active_{false};       ///< True if continuous reading mode is enabled

  // Stability detection buffer (rolling window for standard deviation)
  static constexpr size_t STABILITY_BUFFER_SIZE = 10;  ///< Number of readings to track
  float stability_buffer_[STABILITY_BUFFER_SIZE];      ///< Circular buffer of pH readings
  size_t stability_index_{0};                          ///< Current write position in buffer
  size_t stability_count_{0};                          ///< Number of valid readings in buffer

  // Timing and buffer constants
  static constexpr uint32_t RESPONSE_DELAY_MS = 300;   ///< Required delay for EZO processing
  static constexpr uint8_t RESPONSE_BUFFER_SIZE = 32;  ///< Maximum response length
  static constexpr uint8_t MAX_ERRORS = 5;             ///< Max errors before marking sensor not ready

  // pH validation range (EZO circuits support extended range)
  static constexpr float PH_MIN = -5.0f;               ///< Minimum valid pH reading
  static constexpr float PH_MAX = 19.0f;               ///< Maximum valid pH reading
  static constexpr float STABILITY_THRESHOLD = 0.05f;  ///< Stability detection threshold (pH units)
};

}  // namespace ezo_ph_uart
}  // namespace esphome
