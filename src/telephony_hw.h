#ifndef TELEPHONY_HW_H
#define TELEPHONY_HW_H

#include <stdint.h>

namespace telephony_hw {

// Outgoing: old pulse dial phone -> DTMF generation
constexpr uint8_t OUTGOING_PULSE_IN_PIN = 27;
constexpr uint8_t DTMF_LOW_OUT_PIN = 14;
constexpr uint8_t DTMF_HIGH_OUT_PIN = 12;
constexpr uint8_t DTMF_LOW_CHANNEL = 0;
constexpr uint8_t DTMF_HIGH_CHANNEL = 1;

// Incoming: ring detect
constexpr uint8_t INCOMING_RING_PIN = 4;

// Relay
constexpr uint8_t RELAY_PIN = 26;

// Timing profile
constexpr uint32_t OUTGOING_PULSE_NUMBER_TIMEOUT_MS = 300;
constexpr uint32_t OUTGOING_INTER_DIGIT_GAP_MS = 180;
constexpr uint32_t DTMF_TONE_MS = 100;
constexpr uint32_t DTMF_GAP_MS = 70;
constexpr uint32_t RING_DEBOUNCE_MS = 30;

// DevKit: EN is the chip reset line (reboot only, no credential erase). BOOT is usually GPIO0 —
// hold it low for WIFI_FACTORY_RESET_HOLD_MS while the firmware runs to clear Wi-Fi and reboot into
// the setup AP. ESP32-C3 Mini etc. often use a different BOOT GPIO (e.g. 9): adjust if needed.
// Set to -1 to disable the gesture.
constexpr int WIFI_FACTORY_RESET_PIN = 0;
constexpr uint32_t WIFI_FACTORY_RESET_HOLD_MS = 5000;

// Max time to wait for STA association before opening the setup hotspot (ms).
constexpr uint32_t WIFI_STA_CONNECT_TIMEOUT_MS = 45000;

}  // namespace telephony_hw

#endif  // TELEPHONY_HW_H
