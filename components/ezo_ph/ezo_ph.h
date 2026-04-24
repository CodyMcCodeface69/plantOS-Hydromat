#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome {
namespace ezo_ph {

/**
 * EZO pH Sensor Component
 *
 * Communicates with Atlas Scientific EZO pH circuit via I2C.
 * The EZO circuit uses ASCII text commands over I2C (not binary registers).
 *
 * Critical timing: Must wait 300ms after write before reading response.
 */
class EZOPHComponent : public PollingComponent, public i2c::I2CDevice {
 public:
  EZOPHComponent() = default;

  // Component lifecycle
  void setup() override;
  void update() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Sensor configuration
  void set_ph_sensor(sensor::Sensor *sensor) { this->ph_sensor_ = sensor; }
  void set_temperature_compensation(sensor::Sensor *sensor) { this->temp_sensor_ = sensor; }

  // Calibration methods (callable from ESPHome services)
  void calibrate_mid(float ph_value);
  void calibrate_low(float ph_value);
  void calibrate_high(float ph_value);
  void calibrate_clear();
  void query_calibration_status();

  // Advanced features
  void set_led(bool enable);
  void enter_sleep_mode();
  bool is_stable();

  // Interface compatibility with ezo_ph_uart (used by HAL and Controller)
  bool is_sensor_ready() const { return this->sensor_ready_; }
  void set_verbose(bool /*enable*/) {}  // No-op: I2C has no verbose mode
  bool take_single_reading(float &value);
  float get_last_reading() const { return this->last_ph_value_; }
  bool send_temperature_compensation(float temperature);

  // Communication mode switching (web UI backup buttons)
  // Sends "Baud,9600" over I2C → EZO self-resets in UART mode at 9600 baud.
  void switch_to_uart_mode();

 protected:
  // I2C communication helpers
  bool send_command_(const char *cmd);
  bool read_response_(char *buffer, size_t len);
  bool parse_ph_value_(const char *response, float &value);
  bool check_response_code_(const char *response);
  void wait_blocking_();  // Blocking 300ms wait — only used in calibration paths

  // Non-blocking state machine helpers
  void start_ph_read_();
  void handle_error_(const char *msg);

  // Stability tracking
  void update_stability_buffer_(float value);
  float calculate_std_deviation_();

  // Member variables
  sensor::Sensor *ph_sensor_{nullptr};
  sensor::Sensor *temp_sensor_{nullptr};

  bool sensor_ready_{false};
  uint8_t error_count_{0};
  float last_ph_value_{0.0f};

  // Non-blocking state machine
  enum class UpdateState : uint8_t { IDLE, WAITING_TEMP_COMP, WAITING_READ };
  UpdateState update_state_{UpdateState::IDLE};
  uint32_t response_wait_start_{0};

  // Stability detection
  static constexpr size_t STABILITY_BUFFER_SIZE = 10;
  float stability_buffer_[STABILITY_BUFFER_SIZE];
  size_t stability_index_{0};
  size_t stability_count_{0};

  // Timing constants
  static constexpr uint32_t RESPONSE_DELAY_MS = 300;  // Critical: 300ms delay for EZO processing
  static constexpr uint8_t RESPONSE_BUFFER_SIZE = 32;
  static constexpr uint8_t MAX_ERRORS = 5;

  // pH range validation
  static constexpr float PH_MIN = -5.0f;
  static constexpr float PH_MAX = 19.0f;
  static constexpr float STABILITY_THRESHOLD = 0.05f;  // pH units
};

}  // namespace ezo_ph
}  // namespace esphome
