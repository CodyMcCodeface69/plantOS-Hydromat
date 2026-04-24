#pragma once
// One-shot UART helper to send a raw command to the EZO pH sensor
// when it is still in UART mode (before it has been switched to I2C).
//
// Usage: ezo_send_uart_cmd("I2C,97\r");
//   → tells EZO to switch to I2C mode at address 97 (0x61) and self-reset.
//
// GPIO assignment matches original UART config: TX=GPIO18, RX=GPIO19.
// Uses UART_NUM_1 to avoid conflict with the USB-SERIAL-JTAG logger.

#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>

static const char *EZO_UART_SWITCH_TAG = "ezo_switch";

inline void ezo_send_uart_cmd(const char *cmd) {
    const uart_port_t PORT  = UART_NUM_1;
    const int TX_PIN        = 18;
    const int RX_PIN        = 19;

    // Silently uninstall in case driver is already loaded
    uart_driver_delete(PORT);

    uart_config_t cfg = {};
    cfg.baud_rate  = 9600;
    cfg.data_bits  = UART_DATA_8_BITS;
    cfg.parity     = UART_PARITY_DISABLE;
    cfg.stop_bits  = UART_STOP_BITS_1;
    cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;

    esp_err_t err;
    err = uart_param_config(PORT, &cfg);
    if (err != ESP_OK) { ESP_LOGE(EZO_UART_SWITCH_TAG, "uart_param_config failed: %d", err); return; }

    err = uart_set_pin(PORT, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) { ESP_LOGE(EZO_UART_SWITCH_TAG, "uart_set_pin failed: %d", err); return; }

    err = uart_driver_install(PORT, 256, 0, 0, NULL, 0);
    if (err != ESP_OK) { ESP_LOGE(EZO_UART_SWITCH_TAG, "uart_driver_install failed: %d", err); return; }

    int written = uart_write_bytes(PORT, cmd, strlen(cmd));
    ESP_LOGI(EZO_UART_SWITCH_TAG, "Sent %d bytes via UART1 (GPIO18 TX): %s", written, cmd);

    uart_wait_tx_done(PORT, pdMS_TO_TICKS(500));
    uart_driver_delete(PORT);
}
