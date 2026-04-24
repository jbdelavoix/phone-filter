#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

struct Config;

enum class WifiBootstrapState {
  StationReady,
  ProvisioningPortal,
};

// STA if wifi_ssid set and connect succeeds; otherwise open SoftAP (use full HTTPS UI on AP IP).
WifiBootstrapState wifi_bootstrap(Config *config);

// True while running the “join Wi‑Fi” setup path (no STA): allows browser clock sync + auto-reboot after Wi‑Fi save+delete credentials on reset gesture.
bool wifi_soft_ap_setup_mode();

void wifi_factory_reset_poll(Config *config);

#endif  // WIFI_MANAGER_H
