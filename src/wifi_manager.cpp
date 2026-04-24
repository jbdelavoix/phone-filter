#include "wifi_manager.h"

#include <WiFi.h>
#include <cstdio>
#include <cstring>
#include <esp_mac.h>

#include "config.h"
#include "telephony_hw.h"

namespace {
bool g_soft_ap_setup = false;
}

bool wifi_soft_ap_setup_mode() { return g_soft_ap_setup; }

static void startSoftApOnly() {
  g_soft_ap_setup = true;
  WiFi.mode(WIFI_AP);
  uint8_t mac[6]{};
  // WiFi.macAddress() is often all-zero until the AP is up. SOFTAP MAC can also read as zero
  // before the AP starts; factory base MAC is stable on all supported IDF versions.
  esp_err_t mac_err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  const bool mac_zero = (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0;
  if (mac_err != ESP_OK || mac_zero) {
    (void)esp_efuse_mac_get_default(mac);
  }
  uint8_t tag = 0;
  for (int i = 0; i < 6; ++i) tag ^= mac[i];
  tag = static_cast<uint8_t>(tag * 0x9DU + mac[4] + mac[5]);
  char suffix[4];
  std::snprintf(suffix, sizeof(suffix), "%02X", static_cast<unsigned>(tag));
  String ap_name = String("PhoneFilter-") + suffix;
  WiFi.softAP(ap_name.c_str());

  Serial.print("Setup AP SSID: ");
  Serial.println(ap_name);
  Serial.print("Use http:// or https://");
  Serial.print(WiFi.softAPIP());
  Serial.println("/ (HTTP redirects to HTTPS; trust the dev certificate).");
}

static bool tryConnectStation(Config *config) {
  const char *ssid = config->wifi_ssid.c_str();
  if (strlen(ssid) == 0) return false;

  g_soft_ap_setup = false;
  WiFi.mode(WIFI_STA);
  delay(50);
  WiFi.disconnect(true, true);
  delay(50);

  if (config->wifi_password.empty()) {
    WiFi.begin(ssid);
  } else {
    WiFi.begin(ssid, config->wifi_password.c_str());
  }

  Serial.print("Connecting to Wi-Fi");
  const uint32_t start_ms = millis();
  while (WiFi.status() != WL_CONNECTED) {
    wifi_factory_reset_poll(config);
    if (millis() - start_ms >= telephony_hw::WIFI_STA_CONNECT_TIMEOUT_MS) {
      Serial.println("\nSTA connect timeout.");
      WiFi.disconnect(true, true);
      return false;
    }
    Serial.print(".");
    delay(400);
  }
  Serial.print("\nConnected. https://");
  Serial.print(WiFi.localIP());
  Serial.println("/");
  return true;
}

WifiBootstrapState wifi_bootstrap(Config *config) {
  if (config == nullptr) {
    startSoftApOnly();
    return WifiBootstrapState::ProvisioningPortal;
  }

  if (telephony_hw::WIFI_FACTORY_RESET_PIN >= 0) {
    pinMode(telephony_hw::WIFI_FACTORY_RESET_PIN, INPUT_PULLUP);
  }

  if (tryConnectStation(config)) return WifiBootstrapState::StationReady;

  startSoftApOnly();
  return WifiBootstrapState::ProvisioningPortal;
}

void wifi_factory_reset_poll(Config *config) {
  if (telephony_hw::WIFI_FACTORY_RESET_PIN < 0 || config == nullptr) return;

  static uint32_t low_since_ms = 0;
  static uint32_t last_progress_log_ms = 0;
  const int pin = telephony_hw::WIFI_FACTORY_RESET_PIN;
  const uint32_t now = millis();

  if (digitalRead(pin) == LOW) {
    if (low_since_ms == 0) {
      low_since_ms = now;
      last_progress_log_ms = 0;
      Serial.println("BOOT: hold ~5s to clear Wi-Fi (release to cancel).");
    } else if (now - low_since_ms >= telephony_hw::WIFI_FACTORY_RESET_HOLD_MS) {
      low_since_ms = 0;
      last_progress_log_ms = 0;
      Serial.println("Wi-Fi credentials cleared (factory reset gesture). Rebooting.");
      config->wifi_ssid.clear();
      config->wifi_password.clear();
      saveConfig(*config);
      delay(200);
      ESP.restart();
    } else if (now - low_since_ms >= telephony_hw::WIFI_FACTORY_RESET_HOLD_MS / 2 &&
               now - last_progress_log_ms >= 1000) {
      last_progress_log_ms = now;
      const uint32_t held = now - low_since_ms;
      Serial.printf("BOOT held %lus / %lus\n", static_cast<unsigned long>(held / 1000),
                    static_cast<unsigned long>(telephony_hw::WIFI_FACTORY_RESET_HOLD_MS / 1000));
    }
  } else {
    low_since_ms = 0;
    last_progress_log_ms = 0;
  }
}
