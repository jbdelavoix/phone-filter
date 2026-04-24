#include <Arduino.h>

#include "dtmf_tx.h"

// Arduino-esp32 3.x: pin-based LEDC (ledcAttach / ledcWrite by pin).
// 2.x: channel-based (ledcSetup + ledcAttachPin + ledcWrite by channel).
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#define PHONEFILTER_LEDC_V3 1
#endif

namespace {
uint8_t g_low_pin = 0;
uint8_t g_high_pin = 0;
bool g_initialized = false;
#if !defined(PHONEFILTER_LEDC_V3)
uint8_t g_low_channel = 0;
uint8_t g_high_channel = 0;
#endif

bool mapDigit(char digit, uint16_t *low_hz, uint16_t *high_hz) {
  if (low_hz == nullptr || high_hz == nullptr) return false;
  switch (digit) {
    case '1': *low_hz = 697; *high_hz = 1209; return true;
    case '2': *low_hz = 697; *high_hz = 1336; return true;
    case '3': *low_hz = 697; *high_hz = 1477; return true;
    case '4': *low_hz = 770; *high_hz = 1209; return true;
    case '5': *low_hz = 770; *high_hz = 1336; return true;
    case '6': *low_hz = 770; *high_hz = 1477; return true;
    case '7': *low_hz = 852; *high_hz = 1209; return true;
    case '8': *low_hz = 852; *high_hz = 1336; return true;
    case '9': *low_hz = 852; *high_hz = 1477; return true;
    case '0': *low_hz = 941; *high_hz = 1336; return true;
    case '*': *low_hz = 941; *high_hz = 1209; return true;
    case '#': *low_hz = 941; *high_hz = 1477; return true;
    default: return false;
  }
}
}  // namespace

void dtmf_tx_setup(uint8_t low_group_pin, uint8_t high_group_pin, uint8_t low_channel, uint8_t high_channel) {
  g_low_pin = low_group_pin;
  g_high_pin = high_group_pin;

#if defined(PHONEFILTER_LEDC_V3)
  (void)low_channel;
  (void)high_channel;
  ledcAttach(g_low_pin, 1000, 8);
  ledcAttach(g_high_pin, 1000, 8);
  ledcWrite(g_low_pin, 0);
  ledcWrite(g_high_pin, 0);
#else
  g_low_channel = low_channel;
  g_high_channel = high_channel;
  ledcSetup(g_low_channel, 1000, 8);
  ledcAttachPin(g_low_pin, g_low_channel);
  ledcSetup(g_high_channel, 1000, 8);
  ledcAttachPin(g_high_pin, g_high_channel);
  ledcWrite(g_low_channel, 0);
  ledcWrite(g_high_channel, 0);
#endif
  g_initialized = true;
}

bool dtmf_tx_send_digit(char digit, uint32_t tone_ms, uint32_t gap_ms) {
  if (!g_initialized) return false;

  uint16_t low_hz = 0;
  uint16_t high_hz = 0;
  if (!mapDigit(digit, &low_hz, &high_hz)) return false;

#if defined(PHONEFILTER_LEDC_V3)
  ledcWriteTone(g_low_pin, low_hz);
  ledcWriteTone(g_high_pin, high_hz);
  delay(tone_ms);
  ledcWriteTone(g_low_pin, 0);
  ledcWriteTone(g_high_pin, 0);
#else
  ledcWriteTone(g_low_channel, low_hz);
  ledcWriteTone(g_high_channel, high_hz);
  delay(tone_ms);
  ledcWriteTone(g_low_channel, 0);
  ledcWriteTone(g_high_channel, 0);
#endif
  delay(gap_ms);
  return true;
}

bool dtmf_tx_send_number(const std::string &number, uint32_t tone_ms, uint32_t gap_ms) {
  if (!g_initialized || number.empty()) return false;

  for (char c : number) {
    if (!dtmf_tx_send_digit(c, tone_ms, gap_ms)) return false;
  }
  return true;
}
