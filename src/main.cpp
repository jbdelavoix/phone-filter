#include <Arduino.h>
#include "LittleFS.h"
#include <esp_log.h>

#include "config.h"
#include "dtmf_tx.h"
#include "incoming_filter.h"
#include "outgoing_filter.h"
#include "pulse_dial.h"
#include "relay.h"
#include "telephony_hw.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "web_totp.h"

namespace {
Config g_config;
bool g_was_ring_high = false;
uint32_t g_last_ring_edge_ms = 0;
bool g_incoming_handled_current_ring = false;
bool g_last_outgoing_pulse_level_high = true;

void poll_wifi_factory_reset() {
  wifi_factory_reset_poll(&g_config);
}

std::vector<std::string> extractTotpSecrets(const Config &config) {
  std::vector<std::string> secrets;
  for (const auto &entry : config.totp_secrets) {
    if (entry.active && !entry.secret.empty()) secrets.push_back(entry.secret);
  }
  return secrets;
}

void process_outgoing() {
  // Keep inbound call handling responsive: never parse outgoing pulses while ring is active
  // or right after an incoming ring edge transition.
  const uint32_t now_ms = millis();
  if (g_was_ring_high || (now_ms - g_last_ring_edge_ms) < telephony_hw::RING_DEBOUNCE_MS) return;

  const bool pulse_level_high = digitalRead(telephony_hw::OUTGOING_PULSE_IN_PIN) == HIGH;
  const bool outgoing_activity_started = g_last_outgoing_pulse_level_high && !pulse_level_high;
  g_last_outgoing_pulse_level_high = pulse_level_high;
  if (!outgoing_activity_started) return;

  std::string pulse_number;
  if (!pulse_dial_read_number(&pulse_number, telephony_hw::OUTGOING_PULSE_NUMBER_TIMEOUT_MS, telephony_hw::OUTGOING_INTER_DIGIT_GAP_MS)) return;

  const std::string routed_number = outgoing_filter_apply_shortcut(g_config, pulse_number);
  if (!outgoing_filter_should_allow(g_config, routed_number)) {
    web_server_audit_event("telephony.outgoing.denied", "input=" + pulse_number + " routed=" + routed_number);
    Serial.printf("Pulse %s => DENY outgoing\n", pulse_number.c_str());
    return;
  }

  const bool sent = dtmf_tx_send_number(routed_number, telephony_hw::DTMF_TONE_MS, telephony_hw::DTMF_GAP_MS);
  web_server_audit_event(sent ? "telephony.outgoing.sent" : "telephony.outgoing.failed", "input=" + pulse_number + " routed=" + routed_number);
  Serial.printf("Pulse %s => DTMF %s (%s)\n", pulse_number.c_str(), routed_number.c_str(), sent ? "SENT" : "FAILED");
}

void process_incoming() {
  const bool ring_high = digitalRead(telephony_hw::INCOMING_RING_PIN) == HIGH;
  const uint32_t now_ms = millis();
  if (ring_high != g_was_ring_high) {
    g_last_ring_edge_ms = now_ms;
    g_was_ring_high = ring_high;
    if (!ring_high) g_incoming_handled_current_ring = false;
  }
  if (!ring_high) return;
  if ((now_ms - g_last_ring_edge_ms) < telephony_hw::RING_DEBOUNCE_MS) return;
  if (g_incoming_handled_current_ring) return;
  g_incoming_handled_current_ring = true;

  // Incoming caller number decode has been removed from the project scope.
  // Keep a synthetic marker so policy can still use wildcard rules.
  const std::string caller_number = "unknown";

  const bool allow = incoming_filter_should_allow(g_config, caller_number);
  if (allow) {
    web_server_audit_event("telephony.incoming.allow", "number=" + caller_number);
    relay_allow_call();
    Serial.printf("Incoming %s => ALLOW\n", caller_number.c_str());
  } else {
    web_server_audit_event("telephony.incoming.block", "number=" + caller_number);
    relay_block_call();
    Serial.printf("Incoming %s => BLOCK\n", caller_number.c_str());
  }
}
}

void setup()
{
  Serial.begin(115200);
  // Reduce noisy TLS handshake logs from probing clients.
  esp_log_level_set("esp-tls-mbedtls", ESP_LOG_NONE);
  // TLS errors: WARN shows esp_https_server / handshake failures; NONE hides "performing session handshake" spam.
  esp_log_level_set("esp_https_server", ESP_LOG_WARN);

  Serial.println("\n\n");
  Serial.println("  _                   _                ");
  Serial.println(" |_) |_   _  ._   _  |_ o | _|_  _  ._ ");
  Serial.println(" |   | | (_) | | (/_ |  | |  |_ (/_ |  ");
  Serial.println("\n\n");

  LittleFS.begin(true);

  loadConfig(&g_config);
  const WifiBootstrapState wifi_mode = wifi_bootstrap(&g_config);
  const bool ntp_ok =
      (wifi_mode == WifiBootstrapState::StationReady) ? web_totp_sync_time_with_ntp() : false;

  web_totp_set_secrets(extractTotpSecrets(g_config));
  relay_setup(telephony_hw::RELAY_PIN);
  pinMode(telephony_hw::INCOMING_RING_PIN, INPUT);
  pulse_dial_setup(telephony_hw::OUTGOING_PULSE_IN_PIN);
  pulse_dial_set_idle_poll(poll_wifi_factory_reset);
  g_last_outgoing_pulse_level_high = digitalRead(telephony_hw::OUTGOING_PULSE_IN_PIN) == HIGH;
  dtmf_tx_setup(telephony_hw::DTMF_LOW_OUT_PIN, telephony_hw::DTMF_HIGH_OUT_PIN, telephony_hw::DTMF_LOW_CHANNEL, telephony_hw::DTMF_HIGH_CHANNEL);
  web_server_setup(&g_config);
  if (wifi_mode == WifiBootstrapState::StationReady) {
    web_server_audit_event("device.startup", ntp_ok ? "ntp_sync=ok" : "ntp_sync=failed");
  } else {
    web_server_audit_event("device.startup", "mode=setup_ap");
  }
}

void loop()
{
  wifi_factory_reset_poll(&g_config);
  process_incoming();
  process_outgoing();
  web_server_audit_tick();
}
