// Host stub for esphome/components/uart/uart.h: UARTDevice is a scriptable fake line.
// RX bytes are queued with the fake millis() at which they "arrive"; available() only sees
// bytes that have arrived. Every write is logged with its time, and an optional responder
// (the fake EZO circuit) can queue answers to each complete command.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "esphome/core/hal.h"

namespace esphome {
namespace uart {

class UARTDevice {
 public:
  virtual ~UARTDevice() = default;

  int available() {
    int n = 0;
    for (auto &b : rx_)
      if (b.first <= millis()) n++;
    return n;
  }
  bool read_byte(uint8_t *data) {
    if (rx_.empty() || rx_.front().first > millis()) return false;
    *data = rx_.front().second;
    rx_.pop_front();
    return true;
  }
  void write_array(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
      char c = static_cast<char>(data[i]);
      if (c == '\r') {
        tx_lines.push_back({millis(), tx_pending_});
        std::string cmd = tx_pending_;
        tx_pending_.clear();
        if (responder) responder(cmd);
      } else {
        tx_pending_ += c;
      }
    }
  }
  void flush() {}  // ESP-IDF: waits for TX done; nothing to wait for here

  // --- test side ---
  struct TxLine {
    uint32_t at_ms;
    std::string cmd;
  };
  std::vector<TxLine> tx_lines;
  std::function<void(const std::string &)> responder;
  // Queue `text` to arrive at fake time `at_ms` (all bytes at once)
  void host_rx(uint32_t at_ms, const std::string &text) {
    // keep the queue ordered by arrival time
    for (char c : text) {
      auto it = rx_.end();
      while (it != rx_.begin() && std::prev(it)->first > at_ms) --it;
      rx_.insert(it, {at_ms, static_cast<uint8_t>(c)});
    }
  }
  size_t host_rx_left() const { return rx_.size(); }

 private:
  std::deque<std::pair<uint32_t, uint8_t>> rx_;
  std::string tx_pending_;
};

}  // namespace uart
}  // namespace esphome
