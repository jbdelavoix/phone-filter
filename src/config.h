#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>

struct ShortcutRule {
    std::string trigger;
    std::string replacement;
};

struct TotpSecretEntry {
    std::string name;
    std::string secret;
    bool active = true;
};

struct Config
{
    std::string wifi_ssid;
    std::string wifi_password;
    std::vector<std::string> incoming_blacklist;
    std::vector<std::string> incoming_whitelist;
    std::vector<std::string> outgoing_blacklist;
    std::vector<std::string> outgoing_whitelist;
    std::vector<ShortcutRule> outgoing_shortcuts;
    std::vector<TotpSecretEntry> totp_secrets;
    std::string https_cert_pem;
    std::string https_key_pem;
};

void loadConfig(Config *config);

void saveConfig(Config &config);

#endif // CONFIG_H