#include <Arduino.h>

#include "relay.h"

namespace {
uint8_t g_relay_pin = 0;
bool g_initialized = false;
}  // namespace

void relay_setup(uint8_t pin) {
  g_relay_pin = pin;
  pinMode(g_relay_pin, OUTPUT);
  g_initialized = true;
  relay_allow_call();
}

void relay_allow_call() {
  if (!g_initialized) return;
  digitalWrite(g_relay_pin, HIGH);
}

void relay_block_call() {
  if (!g_initialized) return;
  digitalWrite(g_relay_pin, LOW);
}
