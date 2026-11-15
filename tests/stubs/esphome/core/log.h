// Host stub for esphome/core/log.h. Logs are dropped unless PLANTOS_TEST_LOG=1.
#pragma once
#include <cstdarg>
#include <cstdio>

#include <string>
#include <vector>

namespace host {
extern bool log_enabled;
// When set, every log line ("[I][tag] text") is also appended here (tests that check log output)
extern std::vector<std::string> *log_sink;
void log_line(char level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
}  // namespace host

#define ESP_LOGE(tag, ...) ::host::log_line('E', tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) ::host::log_line('W', tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) ::host::log_line('I', tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) ::host::log_line('D', tag, __VA_ARGS__)
#define ESP_LOGV(tag, ...) ::host::log_line('V', tag, __VA_ARGS__)
#define ESP_LOGVV(tag, ...) ::host::log_line('V', tag, __VA_ARGS__)
#define ESP_LOGCONFIG(tag, ...) ::host::log_line('C', tag, __VA_ARGS__)

#define YESNO(b) ((b) ? "YES" : "NO")
#define ONOFF(b) ((b) ? "ON" : "OFF")
#define TRUEFALSE(b) ((b) ? "TRUE" : "FALSE")
