#include <Arduino.h>

#include "pulse_dial.h"

namespace {
uint8_t g_pulse_pin = 0;
bool g_initialized = false;
uint32_t g_min_pulse_interval_us = 7000;
void (*g_idle_poll)(void) = nullptr;
}  // namespace

void pulse_dial_setup(uint8_t pulse_pin) {
  g_pulse_pin = pulse_pin;
  pinMode(g_pulse_pin, INPUT_PULLUP);
  g_initialized = true;
}

void pulse_dial_set_idle_poll(void (*fn)(void)) {
  g_idle_poll = fn;
}

static bool read_one_digit(char *digit, uint32_t timeout_ms, uint32_t gap_ms) {
  if (digit == nullptr || !g_initialized) return false;

  const uint32_t started = millis();
  uint32_t last_fall = 0;
  uint8_t pulses = 0;
  bool last_level = digitalRead(g_pulse_pin) == HIGH;
  uint32_t last_fall_us = 0;

  while ((millis() - started) < timeout_ms) {
    const bool level = digitalRead(g_pulse_pin) == HIGH;

    if (last_level && !level) {
      const uint32_t now_us = micros();
      if (last_fall_us == 0 || (now_us - last_fall_us) >= g_min_pulse_interval_us) {
        ++pulses;
        last_fall = millis();
        last_fall_us = now_us;
      }
    }
    last_level = level;

    if (pulses > 0 && (millis() - last_fall) > gap_ms) {
      if (pulses == 10) {
        *digit = '0';
      } else if (pulses >= 1 && pulses <= 9) {
        *digit = static_cast<char>('0' + pulses);
      } else {
        return false;
      }
      return true;
    }
    if (g_idle_poll) g_idle_poll();
    delay(2);
  }

  return false;
}

bool pulse_dial_read_number(std::string *number, uint32_t timeout_ms, uint32_t inter_digit_gap_ms) {
  if (number == nullptr || !g_initialized) return false;
  number->clear();

  const uint32_t started = millis();
  uint32_t remaining = timeout_ms;

  while (remaining > 0) {
    char digit = '\0';
    if (!read_one_digit(&digit, remaining, inter_digit_gap_ms)) break;
    number->push_back(digit);

    const uint32_t elapsed = millis() - started;
    if (elapsed >= timeout_ms) break;
    remaining = timeout_ms - elapsed;
  }

  return !number->empty();
}
