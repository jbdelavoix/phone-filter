#include <ArduinoJson.h>
#include <LittleFS.h>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"

namespace {
// Dev-only self-signed material so HTTPS can start with an empty/missing LittleFS config.
// Replace for any deployment beyond a local bench.
const char kDefaultHttpsCertPem[] =
R"(-----BEGIN CERTIFICATE-----
MIIC+jCCAeICCQCE25Vzxsoa1TANBgkqhkiG9w0BAQsFADA/MRowGAYDVQQDDBFw
aG9uZWZpbHRlci5sb2NhbDEUMBIGA1UECgwLUGhvbmVGaWx0ZXIxCzAJBgNVBAYT
AkZSMB4XDTI2MDQyNDE1MTcyMFoXDTM2MDQyMTE1MTcyMFowPzEaMBgGA1UEAwwR
cGhvbmVmaWx0ZXIubG9jYWwxFDASBgNVBAoMC1Bob25lRmlsdGVyMQswCQYDVQQG
EwJGUjCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMU4iBnjVP5Y9gzK
cRIFZcRL4tm9Nk9wqxI+WTcJ0yBjb60YGQZave8ISyrTHLQpoWcsCLwCT4Avmbro
uJo0I52MHrG5ClJe0r0G1MSB+9WVZkdzE1VPXM70gCOVcDViA/0hjiUQL6pjtIqO
SK6PINc+j8Fr9zdlUughabNY1Dsa0l3SHUA0v0LlGCPNFDejbBIQ+YMIKKuOh7sS
46VjYTHS5x+I5nTHbQen6RVEkj15BGEL5vbuXdoWIsRFeFVBGVlTyp5j6m7HEomQ
IEmdUMn3/nutttyAiTT9bqfR2TdhT+ky2fv3FKhLB+5WESizx+0Jez4x9OVeMTZL
Miue8UsCAwEAATANBgkqhkiG9w0BAQsFAAOCAQEAF/DTEg6ttKqnjAgblUO175qo
55YgYcZ5zoeZzIc4UbN/0naHRT5PtcXsHMc/tNYs/txRrQHr/t4ZGUk+9GmScg1k
PWjiHrfFSicFcvCB00/btRczJjAXY5tIF/2Drelya0EOjk98UCd00sL+m4/QoY/X
M5ZrlNZaIEGxJ2AGzKxqXg1QgWwjf+AambGyHcoR82fDwGeSbqzCQQp7BAtchCi+
8EWGB+nhIjV4EIiKtAac8JUOt+TK6YwDsqNtIoXoHaJEwQTX7tJmZD8GzcHM5X0K
Iun8M+8ZP5t03BgeAHibLwHIr9rJkFxBe+0gpPS2stP0cPnA4IG6M5+o6RaQjQ==
-----END CERTIFICATE-----)";

const char kDefaultHttpsKeyPem[] =
R"(-----BEGIN RSA PRIVATE KEY-----
MIIEpgIBAAKCAQEAxTiIGeNU/lj2DMpxEgVlxEvi2b02T3CrEj5ZNwnTIGNvrRgZ
Blq97whLKtMctCmhZywIvAJPgC+Zuui4mjQjnYwesbkKUl7SvQbUxIH71ZVmR3MT
VU9czvSAI5VwNWID/SGOJRAvqmO0io5Iro8g1z6PwWv3N2VS6CFps1jUOxrSXdId
QDS/QuUYI80UN6NsEhD5gwgoq46HuxLjpWNhMdLnH4jmdMdtB6fpFUSSPXkEYQvm
9u5d2hYixEV4VUEZWVPKnmPqbscSiZAgSZ1Qyff+e6223ICJNP1up9HZN2FP6TLZ
+/cUqEsH7lYRKLPH7Ql7PjH05V4xNksyK57xSwIDAQABAoIBAQClUns8gEVqJPa0
yCq6eY3SW+6gqazSMNcmpt8wdOrnVpNx3SZ1HjHjIuen0WpZWCB1lQMJX0J3aO+M
L7M3iIdmdOPyBPQzEkvDqutGgtOxOdiQAlXtiGZ7EZgsvANgrHY9hhha11OtmIDq
ONWShmZ4KHhnInMMm8JA1ml9rvPRSnOyOgyceE6T44thYyqpeb49fy7QfSL4xTOT
P3ftdeBflYC2Ozxe/LPxgHsC+7q7EjukMLNEcQGoGXDeMIokNln4udxPRCov8+Fu
PyJ+PZtT61VdIJG0VVxZmtioh51JArDlFamdo9ScK7rsyDJhyUsQ7ocCsBW/Xk64
Lv5w1hl5AoGBAPM4oIDJ3JAcJ1dAEz8+ddjpyCuGOenMv3fNLcacxLqmKv4RDKiD
Yr/ijkYslG9p8t7wrgCNaSDDg5bonXSyjzTERfIZcUIeu3cGctyKA3+mEkgivbDM
YBjUgVlRnztwdChZIjy5Q/OeXvIHATfctxbKNkLb+r6yOkds5AOEpDoFAoGBAM+V
MMvkj3bS9xxYQMgrbtes15T+eC4YXx7zXY+uXQWENvLisSXAoJMmsRzf4v//zWlC
9ZTu+AswjQPjlxaEvlffPq+TtnyoiUTM62jmYnRz4/RGbckPmF4AzWpspwVkLJvN
+TfqftDoS38IaoHXGG2hxYGoyNS+2kcAMM2bOE8PAoGBALdjoCnbivIiOEuiZaDP
ML6Qb7zZpXszRb9INtbFx9RQjKQrKNc60c+LPOmOnZFwWo9c/GYwOe9ZXDQCSw79
v9ryjybfpjVLxOAXPa4qZj7ucmRvxYW0ZFT0jl9RmvWPchYmNxmAO8tKQ57MR0/e
nTKS390DzwnQiv7mDPrWHZodAoGBAKZaQQik2fO3jSDB+OhxJhrhAML+SwFltpTv
IwOKEDHjisWKtbwzanuCfl1NbnUJYmwApR39g/ozpk5/jQ0WxBNXbLz3+z5bMeZY
8i7Wsf/w/7U627BNNyXeLsAi4paHeNhDcH1HBuoTvqzG6dOztHfnNIh74rXbXMGZ
fYLH6ZAxAoGBAMk7C45Lu0v2tZ4Kx2qDXAAdwH2Aeb8nJs6DIQq04h4Dks1vZypf
pXoCM21innlGyMpfG4DLIHD3MzFVzkaMcUSjFdVo6z0JtCzs9YTJNGmQ74QH6lmL
4a2nTKSWn/29ZgOGuuWDHQaex04mbiZwpmgGgEsdOfWsXVmGlca8XaLH
-----END RSA PRIVATE KEY-----)";

void setDefaultConfig(Config *config) {
  config->wifi_ssid.clear();
  config->wifi_password.clear();
  config->incoming_blacklist.clear();
  config->incoming_whitelist.clear();
  config->outgoing_blacklist.clear();
  config->outgoing_whitelist.clear();
  config->outgoing_shortcuts.clear();
  config->totp_secrets.clear();
  config->https_cert_pem = kDefaultHttpsCertPem;
  config->https_key_pem = kDefaultHttpsKeyPem;
}
}  // namespace

void loadConfig(Config *config) {
  if (config == nullptr) return;

  setDefaultConfig(config);

  File file = LittleFS.open("/config.json", "r");
  if (!file) {
    saveConfig(*config);
    return;
  }

  DynamicJsonDocument doc(12288);
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    saveConfig(*config);
    return;
  }

  if (doc["wifi_ssid"].is<const char*>()) {
    config->wifi_ssid = doc["wifi_ssid"].as<std::string>();
  }
  if (doc["wifi_password"].is<const char*>()) {
    config->wifi_password = doc["wifi_password"].as<std::string>();
  }

  config->incoming_blacklist.clear();
  JsonArray blacklist = doc["incoming_blacklist"].as<JsonArray>();
  if (!blacklist.isNull()) {
    for (auto v : blacklist) {
      config->incoming_blacklist.push_back(v.as<std::string>());
    }
  }

  config->incoming_whitelist.clear();
  JsonArray whitelist = doc["incoming_whitelist"].as<JsonArray>();
  if (!whitelist.isNull()) {
    for (auto v : whitelist) {
      config->incoming_whitelist.push_back(v.as<std::string>());
    }
  }

  config->outgoing_whitelist.clear();
  JsonArray allowed = doc["outgoing_whitelist"].as<JsonArray>();
  if (!allowed.isNull()) {
    for (auto v : allowed) {
      config->outgoing_whitelist.push_back(v.as<std::string>());
    }
  }
  config->outgoing_blacklist.clear();
  JsonArray outgoing_blacklist = doc["outgoing_blacklist"].as<JsonArray>();
  if (!outgoing_blacklist.isNull()) {
    for (auto v : outgoing_blacklist) {
      config->outgoing_blacklist.push_back(v.as<std::string>());
    }
  }

  config->outgoing_shortcuts.clear();
  JsonArray shortcuts = doc["outgoing_shortcuts"].as<JsonArray>();
  if (!shortcuts.isNull()) {
    for (JsonVariant v : shortcuts) {
      const char *trigger = v["trigger"] | "";
      const char *replacement = v["replacement"] | "";
      if (trigger[0] == '\0' || replacement[0] == '\0') continue;
      config->outgoing_shortcuts.push_back({std::string(trigger), std::string(replacement)});
    }
  }

  config->totp_secrets.clear();
  JsonArray secrets = doc["totp_secrets"].as<JsonArray>();
  if (!secrets.isNull()) {
    for (auto v : secrets) {
      if (v.is<JsonObject>()) {
        const char *name = v["name"] | "";
        const char *secret = v["secret"] | "";
        const bool active = v["active"] | true;
        if (name[0] != '\0' && secret[0] != '\0') {
          TotpSecretEntry entry;
          entry.name = std::string(name);
          entry.secret = std::string(secret);
          entry.active = active;
          config->totp_secrets.push_back(entry);
        }
      } else if (v.is<const char*>()) {
        std::string secret = v.as<std::string>();
        if (!secret.empty()) {
          const std::string name = "Token " + std::to_string(config->totp_secrets.size() + 1);
          TotpSecretEntry entry;
          entry.name = name;
          entry.secret = secret;
          entry.active = true;
          config->totp_secrets.push_back(entry);
        }
      }
    }
  }

  const bool has_https_fields = doc.containsKey("https_cert_pem") && doc.containsKey("https_key_pem");
  if (doc["https_cert_pem"].is<const char*>()) {
    config->https_cert_pem = doc["https_cert_pem"].as<std::string>();
  }
  if (doc["https_key_pem"].is<const char*>()) {
    config->https_key_pem = doc["https_key_pem"].as<std::string>();
  }
  if (config->https_cert_pem.empty()) config->https_cert_pem = kDefaultHttpsCertPem;
  if (config->https_key_pem.empty()) config->https_key_pem = kDefaultHttpsKeyPem;
  if (config->https_key_pem.find("BEGIN PRIVATE KEY") != std::string::npos &&
      config->https_key_pem.find("BEGIN RSA PRIVATE KEY") == std::string::npos &&
      config->https_key_pem.find("BEGIN EC PRIVATE KEY") == std::string::npos) {
    config->https_cert_pem = kDefaultHttpsCertPem;
    config->https_key_pem = kDefaultHttpsKeyPem;
    saveConfig(*config);
  }
  if (config->https_key_pem.find("BEGIN EC PRIVATE KEY") != std::string::npos) {
    config->https_cert_pem = kDefaultHttpsCertPem;
    config->https_key_pem = kDefaultHttpsKeyPem;
    saveConfig(*config);
  }
  if (!has_https_fields) saveConfig(*config);
}

void saveConfig(Config &config) {
  DynamicJsonDocument doc(12288);

  doc["wifi_ssid"] = config.wifi_ssid.c_str();
  doc["wifi_password"] = config.wifi_password.c_str();

  JsonArray bl = doc.createNestedArray("incoming_blacklist");
  for (auto &n : config.incoming_blacklist) bl.add(n);

  JsonArray wl = doc.createNestedArray("incoming_whitelist");
  for (auto &n : config.incoming_whitelist) wl.add(n);

  JsonArray allowed = doc.createNestedArray("outgoing_whitelist");
  for (auto &n : config.outgoing_whitelist) allowed.add(n);

  JsonArray outgoing_blacklist = doc.createNestedArray("outgoing_blacklist");
  for (auto &n : config.outgoing_blacklist) outgoing_blacklist.add(n);

  JsonArray shortcuts = doc.createNestedArray("outgoing_shortcuts");
  for (auto &rule : config.outgoing_shortcuts) {
    JsonObject obj = shortcuts.createNestedObject();
    obj["trigger"] = rule.trigger.c_str();
    obj["replacement"] = rule.replacement.c_str();
  }

  JsonArray secrets = doc.createNestedArray("totp_secrets");
  for (auto &entry : config.totp_secrets) {
    JsonObject obj = secrets.createNestedObject();
    obj["name"] = entry.name.c_str();
    obj["secret"] = entry.secret.c_str();
    obj["active"] = entry.active;
  }

  doc["https_cert_pem"] = config.https_cert_pem.c_str();
  doc["https_key_pem"] = config.https_key_pem.c_str();

  File file = LittleFS.open("/config.json", "w");
  if (!file) return;

  serializeJson(doc, file);
  file.close();
}
