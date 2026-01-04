#include "shelly_ble.h"

#ifdef USE_ESP32

#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <cstring>

namespace shelly_ble {

static const char* TAG = "shelly_ble";

void ShellyBLESwitch::setup() {
    ESP_LOGI(TAG, "Setting up Shelly BLE Switch (Socket %d)", socket_id_);
}

void ShellyBLESwitch::loop() {
    // Check for request timeout
    if (waiting_for_response_) {
        uint32_t now = esphome::millis();
        if (now - last_request_time_ > REQUEST_TIMEOUT_MS) {
            ESP_LOGW(TAG, "RPC request timeout - no response received");
            waiting_for_response_ = false;
        }
    }
}

void ShellyBLESwitch::dump_config() {
    ESP_LOGCONFIG(TAG, "Shelly BLE Switch:");
    LOG_SWITCH("", "Shelly BLE Switch", this);
    ESP_LOGCONFIG(TAG, "  Socket ID: %d", socket_id_);
    ESP_LOGCONFIG(TAG, "  Service Discovered: %s", service_discovered_ ? "YES" : "NO");
    ESP_LOGCONFIG(TAG, "  Characteristics Configured: %s", characteristics_configured_ ? "YES" : "NO");
}

void ShellyBLESwitch::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                          esp_ble_gattc_cb_param_t *param) {
    switch (event) {
        case ESP_GATTC_OPEN_EVT:
            if (param->open.status == ESP_GATT_OK) {
                ESP_LOGI(TAG, "BLE connection opened successfully");
                discover_characteristics_();
            } else {
                ESP_LOGW(TAG, "BLE connection failed: status=%d", param->open.status);
            }
            break;

        case ESP_GATTC_SEARCH_RES_EVT: {
            // Service found - check if it's the Shelly RPC service
            if (param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_128) {
                if (uuid_matches_(param->search_res.srvc_id.uuid.uuid.uuid128, SHELLY_SERVICE_UUID, 16)) {
                    service_handle_ = param->search_res.start_handle;
                    ESP_LOGI(TAG, "Found Shelly RPC service at handle 0x%04x", service_handle_);
                    service_discovered_ = true;
                }
            }
            break;
        }

        case ESP_GATTC_SEARCH_CMPL_EVT:
            ESP_LOGI(TAG, "GATT service discovery complete");
            if (service_discovered_) {
                // Discover characteristics using get_characteristic API
                ESP_LOGI(TAG, "Discovering characteristics...");

                // Get count of characteristics in the service
                uint16_t count = 0;
                esp_gatt_status_t status = esp_ble_gattc_get_attr_count(
                    this->parent_->get_gattc_if(),
                    this->parent_->get_conn_id(),
                    ESP_GATT_DB_CHARACTERISTIC,
                    service_handle_,
                    service_handle_ + 0xFF,  // End handle estimate
                    0,  // Invalid handle
                    &count
                );

                if (status == ESP_GATT_OK && count > 0) {
                    ESP_LOGI(TAG, "Found %d characteristics in service", count);

                    // Get all characteristics
                    esp_gattc_char_elem_t* char_elems = new esp_gattc_char_elem_t[count];
                    uint16_t read_count = count;

                    status = esp_ble_gattc_get_all_char(
                        this->parent_->get_gattc_if(),
                        this->parent_->get_conn_id(),
                        service_handle_,
                        service_handle_ + 0xFF,
                        char_elems,
                        &read_count,
                        0
                    );

                    if (status == ESP_GATT_OK) {
                        // Iterate through characteristics and match UUIDs
                        for (uint16_t i = 0; i < read_count; i++) {
                            auto& uuid = char_elems[i].uuid;
                            uint16_t char_handle = char_elems[i].char_handle;

                            if (uuid.len == ESP_UUID_LEN_128) {
                                if (uuid_matches_(uuid.uuid.uuid128, RPC_DATA_CHAR_UUID, 16)) {
                                    data_char_handle_ = char_handle;
                                    data_char_cccd_handle_ = char_handle + 2;  // CCCD typically at +2
                                    chars_found_ |= CHAR_DATA;
                                    ESP_LOGI(TAG, "Found RPC Data char at 0x%04x (CCCD: 0x%04x)",
                                            char_handle, data_char_cccd_handle_);
                                }
                                else if (uuid_matches_(uuid.uuid.uuid128, RPC_TX_CTRL_UUID, 16)) {
                                    tx_ctrl_char_handle_ = char_handle;
                                    chars_found_ |= CHAR_TX_CTRL;
                                    ESP_LOGI(TAG, "Found RPC TX Control char at 0x%04x", char_handle);
                                }
                                else if (uuid_matches_(uuid.uuid.uuid128, RPC_RX_CTRL_UUID, 16)) {
                                    rx_ctrl_char_handle_ = char_handle;
                                    rx_ctrl_cccd_handle_ = char_handle + 2;
                                    chars_found_ |= CHAR_RX_CTRL;
                                    ESP_LOGI(TAG, "Found RPC RX Control char at 0x%04x (CCCD: 0x%04x)",
                                            char_handle, rx_ctrl_cccd_handle_);
                                }
                            }
                        }
                    }

                    delete[] char_elems;

                    // Check if we found all required characteristics
                    if (chars_found_ == ALL_CHARS_FOUND) {
                        ESP_LOGI(TAG, "All characteristics found, enabling notifications...");
                        enable_notifications_();
                    } else {
                        ESP_LOGW(TAG, "Not all characteristics found (0x%02x/0x%02x)",
                                chars_found_, ALL_CHARS_FOUND);
                    }
                } else {
                    ESP_LOGW(TAG, "Failed to get characteristic count: status=%d", status);
                }
            } else {
                ESP_LOGW(TAG, "Shelly RPC service not found!");
            }
            break;

        case ESP_GATTC_REG_FOR_NOTIFY_EVT:
            ESP_LOGD(TAG, "Registered for notifications on handle 0x%04x", param->reg_for_notify.handle);
            if (param->reg_for_notify.status == ESP_GATT_OK) {
                // After registering, write to CCCD to enable notifications
                if (param->reg_for_notify.handle == rx_ctrl_char_handle_) {
                    write_cccd_(rx_ctrl_char_handle_, rx_ctrl_cccd_handle_);
                } else if (param->reg_for_notify.handle == data_char_handle_) {
                    write_cccd_(data_char_handle_, data_char_cccd_handle_);
                }
            }
            break;

        case ESP_GATTC_NOTIFY_EVT:
            // Handle incoming notifications (RPC responses)
            ESP_LOGD(TAG, "Notification received: handle=0x%04x, length=%d",
                     param->notify.handle, param->notify.value_len);

            // Dispatch to appropriate handler based on characteristic
            if (param->notify.handle == rx_ctrl_char_handle_) {
                handle_rx_control_notification_(param->notify.value, param->notify.value_len);
            } else if (param->notify.handle == data_char_handle_) {
                handle_data_notification_(param->notify.value, param->notify.value_len);
            }
            break;

        case ESP_GATTC_WRITE_CHAR_EVT:
            if (param->write.status != ESP_GATT_OK) {
                ESP_LOGW(TAG, "GATT write failed: handle=0x%04x, status=%d",
                         param->write.handle, param->write.status);
            } else {
                ESP_LOGD(TAG, "GATT write successful: handle=0x%04x", param->write.handle);

                // Check if this was a CCCD write (marks characteristics as configured)
                if ((param->write.handle == data_char_cccd_handle_ ||
                     param->write.handle == rx_ctrl_cccd_handle_) &&
                    !characteristics_configured_) {
                    characteristics_configured_ = true;
                    ESP_LOGI(TAG, "Shelly BLE switch ready (Socket %d)", socket_id_);
                }
            }
            break;

        case ESP_GATTC_DISCONNECT_EVT:
            ESP_LOGW(TAG, "BLE disconnected");
            service_discovered_ = false;
            characteristics_configured_ = false;
            chars_found_ = 0;
            waiting_for_response_ = false;
            break;

        default:
            break;
    }
}

void ShellyBLESwitch::write_state(bool state) {
    if (!this->parent_->enabled) {
        ESP_LOGW(TAG, "Cannot write state - BLE client not connected");
        return;
    }

    if (!characteristics_configured_) {
        ESP_LOGW(TAG, "Cannot write state - characteristics not configured");
        return;
    }

    ESP_LOGI(TAG, "Setting socket %d to %s", socket_id_, state ? "ON" : "OFF");
    send_switch_set_(state);
}

void ShellyBLESwitch::discover_characteristics_() {
    ESP_LOGI(TAG, "Starting GATT service discovery for Shelly RPC service");

    // Search for Shelly RPC service
    esp_bt_uuid_t service_uuid;
    service_uuid.len = ESP_UUID_LEN_128;
    memcpy(service_uuid.uuid.uuid128, SHELLY_SERVICE_UUID, 16);

    esp_ble_gattc_search_service(this->parent_->get_gattc_if(),
                                 this->parent_->get_conn_id(),
                                 &service_uuid);

    // Service and characteristic handles will be populated in GATT event callbacks:
    // - ESP_GATTC_SEARCH_RES_EVT: Service found (populates service_handle_)
    // - ESP_GATTC_SEARCH_CMPL_EVT: Discovery complete (triggers characteristic enumeration)
    // - ESP_GATTC_GET_CHAR_EVT: Characteristic found (populates char handles)
}

void ShellyBLESwitch::enable_notifications_() {
    ESP_LOGI(TAG, "Enabling notifications for RPC data and RX control characteristics");

    // Register for notifications on RX Control characteristic (receives response length)
    if (rx_ctrl_char_handle_ != 0) {
        esp_ble_gattc_register_for_notify(this->parent_->get_gattc_if(),
                                          this->parent_->get_remote_bda(),
                                          rx_ctrl_char_handle_);
        ESP_LOGD(TAG, "Registering for RX Control notifications (handle 0x%04x)", rx_ctrl_char_handle_);
    } else {
        ESP_LOGW(TAG, "RX Control characteristic handle not set - cannot enable notifications");
    }

    // Register for notifications on Data characteristic (receives response JSON)
    if (data_char_handle_ != 0) {
        esp_ble_gattc_register_for_notify(this->parent_->get_gattc_if(),
                                          this->parent_->get_remote_bda(),
                                          data_char_handle_);
        ESP_LOGD(TAG, "Registering for Data notifications (handle 0x%04x)", data_char_handle_);
    } else {
        ESP_LOGW(TAG, "Data characteristic handle not set - cannot enable notifications");
    }

    // After registration succeeds, CCCD writes will be triggered in ESP_GATTC_REG_FOR_NOTIFY_EVT handler
}

void ShellyBLESwitch::write_cccd_(uint16_t char_handle, uint16_t cccd_handle) {
    // Write 0x0001 (little-endian) to CCCD to enable notifications
    uint8_t notify_enable[2] = {0x01, 0x00};

    esp_err_t err = esp_ble_gattc_write_char_descr(
        this->parent_->get_gattc_if(),
        this->parent_->get_conn_id(),
        cccd_handle,
        sizeof(notify_enable),
        notify_enable,
        ESP_GATT_WRITE_TYPE_RSP,  // Request write response
        ESP_GATT_AUTH_REQ_NONE
    );

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Writing CCCD enable (0x0001) to handle 0x%04x for char 0x%04x",
                 cccd_handle, char_handle);
    } else {
        ESP_LOGW(TAG, "Failed to write CCCD: error %d", err);
    }
}

void ShellyBLESwitch::send_switch_set_(bool state) {
    // Build JSON-RPC request
    uint32_t request_id = rpc_request_id_++;
    std::string json_rpc = build_switch_set_json_(request_id, socket_id_, state);

    ESP_LOGI(TAG, "Sending RPC: %s", json_rpc.c_str());

    send_rpc_request_(json_rpc);

    // Optimistically update switch state
    // Response will confirm actual state
    this->publish_state(state);
}

void ShellyBLESwitch::send_rpc_request_(const std::string& json_rpc) {
    if (!this->parent_->enabled) {
        ESP_LOGW(TAG, "Cannot send RPC - BLE not connected");
        return;
    }

    // Convert JSON string to byte array
    std::vector<uint8_t> data(json_rpc.begin(), json_rpc.end());
    uint32_t length = data.size();

    // Pack length as 4-byte big-endian integer
    uint8_t length_bytes[4] = {
        static_cast<uint8_t>((length >> 24) & 0xFF),
        static_cast<uint8_t>((length >> 16) & 0xFF),
        static_cast<uint8_t>((length >> 8) & 0xFF),
        static_cast<uint8_t>(length & 0xFF)
    };

    // Write length to TX Control characteristic
    if (tx_ctrl_char_handle_ != 0) {
        esp_ble_gattc_write_char(this->parent_->get_gattc_if(),
                                this->parent_->get_conn_id(),
                                tx_ctrl_char_handle_,
                                4,  // length of length_bytes
                                length_bytes,
                                ESP_GATT_WRITE_TYPE_NO_RSP,
                                ESP_GATT_AUTH_REQ_NONE);

        ESP_LOGD(TAG, "Wrote length (%d bytes) to TX Control characteristic", length);
    } else {
        ESP_LOGW(TAG, "TX Control characteristic handle not set");
    }

    // Small delay as recommended by Shelly protocol
    esphome::delay(10);

    // Write JSON-RPC data to Data characteristic
    if (data_char_handle_ != 0) {
        // Split into chunks if needed (BLE typically has 20-byte MTU default)
        const size_t CHUNK_SIZE = 20;
        size_t offset = 0;

        while (offset < data.size()) {
            size_t chunk_len = std::min(CHUNK_SIZE, data.size() - offset);

            esp_ble_gattc_write_char(this->parent_->get_gattc_if(),
                                    this->parent_->get_conn_id(),
                                    data_char_handle_,
                                    chunk_len,
                                    &data[offset],
                                    ESP_GATT_WRITE_TYPE_NO_RSP,
                                    ESP_GATT_AUTH_REQ_NONE);

            offset += chunk_len;
            ESP_LOGD(TAG, "Wrote %d bytes to Data characteristic (offset=%d)", chunk_len, offset);

            if (offset < data.size()) {
                esphome::delay(5);  // Small delay between chunks
            }
        }
    } else {
        ESP_LOGW(TAG, "Data characteristic handle not set");
    }

    // Mark as waiting for response
    waiting_for_response_ = true;
    last_request_time_ = esphome::millis();
}

void ShellyBLESwitch::handle_rx_control_notification_(const uint8_t* data, uint16_t length) {
    // RX Control characteristic sends the response length as a 4-byte big-endian integer
    if (length >= 4) {
        expected_response_length_ = (static_cast<uint32_t>(data[0]) << 24) |
                                   (static_cast<uint32_t>(data[1]) << 16) |
                                   (static_cast<uint32_t>(data[2]) << 8) |
                                   static_cast<uint32_t>(data[3]);

        ESP_LOGD(TAG, "RX Control: expecting %u bytes of response data", expected_response_length_);

        // Clear response buffer for new response
        response_buffer_.clear();
    } else {
        ESP_LOGW(TAG, "RX Control notification too short: %d bytes", length);
    }
}

void ShellyBLESwitch::handle_data_notification_(const uint8_t* data, uint16_t length) {
    // Accumulate notification data (may arrive in multiple chunks)
    response_buffer_.insert(response_buffer_.end(), data, data + length);

    ESP_LOGD(TAG, "Data notification: received %d bytes, total %d/%d bytes",
             length, response_buffer_.size(), expected_response_length_);

    // Check if we have received all expected data
    if (expected_response_length_ > 0 && response_buffer_.size() >= expected_response_length_) {
        // Try to parse as JSON
        std::string json_str(response_buffer_.begin(), response_buffer_.end());

        ESP_LOGI(TAG, "Received complete RPC response (%d bytes): %s",
                 json_str.length(), json_str.c_str());

        // Parse JSON response to check for errors
        // Example success: {"id":1,"src":"shellyplus4pm-XXXXXX","result":{"was_on":false}}
        // Example error: {"id":1,"src":"shellyplus4pm-XXXXXX","error":{"code":-105,"message":"Method not found"}}

        bool has_result = json_str.find("\"result\"") != std::string::npos;
        bool has_error = json_str.find("\"error\"") != std::string::npos;

        if (has_error) {
            ESP_LOGW(TAG, "RPC returned error response");
        } else if (has_result) {
            ESP_LOGD(TAG, "RPC succeeded");
        } else {
            ESP_LOGW(TAG, "Unexpected JSON response format");
        }

        // Clear buffer and reset state
        response_buffer_.clear();
        expected_response_length_ = 0;
        waiting_for_response_ = false;
    } else if (expected_response_length_ == 0) {
        // Fallback: if we didn't get RX Control length, use JSON end detection
        std::string json_str(response_buffer_.begin(), response_buffer_.end());

        if (!json_str.empty() && json_str.back() == '}') {
            ESP_LOGI(TAG, "Received RPC response (fallback mode): %s", json_str.c_str());

            response_buffer_.clear();
            waiting_for_response_ = false;
        } else {
            ESP_LOGD(TAG, "Incomplete response (fallback mode), waiting for more data...");
        }
    }
}

std::string ShellyBLESwitch::build_switch_set_json_(uint32_t request_id, uint8_t socket_id, bool state) {
    // Build JSON-RPC request manually to avoid ArduinoJson dependency
    char buffer[128];
    snprintf(buffer, sizeof(buffer),
             "{\"id\":%u,\"src\":\"esphome\",\"method\":\"Switch.Set\",\"params\":{\"id\":%u,\"on\":%s}}",
             request_id, socket_id, state ? "true" : "false");
    return std::string(buffer);
}

bool ShellyBLESwitch::uuid_matches_(const uint8_t* uuid1, const uint8_t* uuid2, size_t len) {
    // Compare two UUIDs byte-by-byte
    return memcmp(uuid1, uuid2, len) == 0;
}

}  // namespace shelly_ble

#endif  // USE_ESP32
