#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/ble_client/ble_client.h"

#ifdef USE_ESP32

namespace shelly_ble {

/**
 * Shelly BLE Switch Component
 *
 * Provides BLE control for individual sockets on Shelly Gen2+ devices using JSON-RPC over GATT.
 *
 * Shelly BLE Protocol:
 * - Service UUID: 5f6d4f53-5f52-5043-5f53-56435f49445f
 * - RPC Data Characteristic: 5f6d4f53-5f52-5043-5f64-6174615f5f5f
 * - RPC TX Control Characteristic: 5f6d4f53-5f52-5043-5f74-785f63746c5f
 * - RPC RX Control Characteristic: 5f6d4f53-5f52-5043-5f72-785f63746c5f
 *
 * Communication Flow:
 * 1. Connect to BLE device
 * 2. Discover GATT service and characteristics
 * 3. To send command:
 *    a. Write data length (4-byte big-endian) to TX Control characteristic
 *    b. Write JSON-RPC payload to Data characteristic
 *    c. Read response length from RX Control characteristic
 *    d. Read JSON-RPC response from Data characteristic
 *
 * Example RPC: {"id":1,"src":"esphome","method":"Switch.Set","params":{"id":0,"on":true}}
 */
class ShellyBLESwitch : public esphome::switch_::Switch,
                        public esphome::Component,
                        public esphome::ble_client::BLEClientNode {
public:
    ShellyBLESwitch() = default;

    // Component lifecycle
    void setup() override;
    void loop() override;
    void dump_config() override;
    void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                            esp_ble_gattc_cb_param_t *param) override;
    float get_setup_priority() const override { return esphome::setup_priority::DATA; }

    // Configuration
    void set_socket_id(uint8_t socket_id) { socket_id_ = socket_id; }
    uint8_t get_socket_id() const { return socket_id_; }

protected:
    // Switch interface implementation
    void write_state(bool state) override;

    static constexpr const char* TAG = "shelly_ble";

    // Shelly GATT UUIDs (converted to ESP-IDF format)
    // Service: 5f6d4f53-5f52-5043-5f53-56435f49445f
    static constexpr uint8_t SHELLY_SERVICE_UUID[16] = {
        0x5f, 0x6d, 0x4f, 0x53, 0x5f, 0x52, 0x50, 0x43,
        0x5f, 0x53, 0x56, 0x43, 0x5f, 0x49, 0x44, 0x5f
    };

    // RPC Data: 5f6d4f53-5f52-5043-5f64-6174615f5f5f
    static constexpr uint8_t RPC_DATA_CHAR_UUID[16] = {
        0x5f, 0x6d, 0x4f, 0x53, 0x5f, 0x52, 0x50, 0x43,
        0x5f, 0x64, 0x61, 0x74, 0x61, 0x5f, 0x5f, 0x5f
    };

    // RPC TX Control: 5f6d4f53-5f52-5043-5f74-785f63746c5f
    static constexpr uint8_t RPC_TX_CTRL_UUID[16] = {
        0x5f, 0x6d, 0x4f, 0x53, 0x5f, 0x52, 0x50, 0x43,
        0x5f, 0x74, 0x78, 0x5f, 0x63, 0x74, 0x6c, 0x5f
    };

    // RPC RX Control: 5f6d4f53-5f52-5043-5f72-785f63746c5f
    static constexpr uint8_t RPC_RX_CTRL_UUID[16] = {
        0x5f, 0x6d, 0x4f, 0x53, 0x5f, 0x52, 0x50, 0x43,
        0x5f, 0x72, 0x78, 0x5f, 0x63, 0x74, 0x6c, 0x5f
    };

    // Configuration
    uint8_t socket_id_{0};  // Socket ID on Shelly device (0-3 for Plus 4PM)

    // GATT characteristic handles (populated after service discovery)
    uint16_t service_handle_{0};
    uint16_t data_char_handle_{0};
    uint16_t data_char_cccd_handle_{0};  // Client Characteristic Configuration Descriptor
    uint16_t tx_ctrl_char_handle_{0};
    uint16_t rx_ctrl_char_handle_{0};
    uint16_t rx_ctrl_cccd_handle_{0};

    // Connection state
    bool service_discovered_{false};
    bool characteristics_configured_{false};
    uint8_t chars_found_{0};  // Bitmask tracking which characteristics have been found
    static constexpr uint8_t CHAR_DATA = 0x01;
    static constexpr uint8_t CHAR_TX_CTRL = 0x02;
    static constexpr uint8_t CHAR_RX_CTRL = 0x04;
    static constexpr uint8_t ALL_CHARS_FOUND = 0x07;

    // RPC state
    uint32_t rpc_request_id_{1};
    bool waiting_for_response_{false};
    uint32_t last_request_time_{0};
    static constexpr uint32_t REQUEST_TIMEOUT_MS = 5000;

    // Response buffer
    std::vector<uint8_t> response_buffer_;
    uint32_t expected_response_length_{0};

    // Internal methods
    void discover_characteristics_();
    void enable_notifications_();
    void write_cccd_(uint16_t char_handle, uint16_t cccd_handle);
    void send_switch_set_(bool state);
    void send_rpc_request_(const std::string& json_rpc);
    void handle_notification_(const uint8_t* data, uint16_t length);
    void handle_rx_control_notification_(const uint8_t* data, uint16_t length);
    void handle_data_notification_(const uint8_t* data, uint16_t length);
    std::string build_switch_set_json_(uint32_t request_id, uint8_t socket_id, bool state);
    bool uuid_matches_(const uint8_t* uuid1, const uint8_t* uuid2, size_t len);
};

}  // namespace shelly_ble

#endif  // USE_ESP32
