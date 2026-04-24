#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <algorithm>
#include <array>
#include <deque>
#include <esp_http_server.h>
#include <esp_https_server.h>
#include <esp_idf_version.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <stdio.h>
#include <cstring>
#include <string>
#include <sys/time.h>
#include <time.h>

#include "config.h"
#include "wifi_manager.h"
#include "web_totp.h"

namespace {
Config *g_config = nullptr;
httpd_handle_t g_https_server = nullptr;
httpd_handle_t g_http_redirect_server = nullptr;
std::string g_session_token;
std::string g_session_token_name;
constexpr size_t kMaxAuditEvents = 120;
constexpr uint32_t kAuditFlushIntervalMs = 3000;
const char *kAuditLogFilePath = "/audit_log.json";

struct AuditEvent {
  std::string ts;
  std::string action;
  std::string detail;
};

std::deque<AuditEvent> g_audit_events;
bool g_audit_dirty = false;
uint32_t g_last_audit_flush_ms = 0;

void saveAuditEventsToFs() {
  DynamicJsonDocument doc(24576);
  JsonArray arr = doc.to<JsonArray>();
  for (const auto &event : g_audit_events) {
    JsonObject obj = arr.createNestedObject();
    obj["ts"] = event.ts.c_str();
    obj["action"] = event.action.c_str();
    obj["detail"] = event.detail.c_str();
  }
  File file = LittleFS.open(kAuditLogFilePath, "w");
  if (!file) return;
  serializeJson(doc, file);
  file.close();
  g_audit_dirty = false;
  g_last_audit_flush_ms = millis();
}

void loadAuditEventsFromFs() {
  g_audit_events.clear();
  File file = LittleFS.open(kAuditLogFilePath, "a+");
  if (!file) return;
  if (file.size() == 0) {
    file.close();
    return;
  }
  file.seek(0, SeekSet);
  DynamicJsonDocument doc(24576);
  if (deserializeJson(doc, file) || !doc.is<JsonArray>()) {
    file.close();
    return;
  }
  file.close();
  for (JsonVariant v : doc.as<JsonArray>()) {
    if (!v.is<JsonObject>()) continue;
    const char *ts = v["ts"] | "";
    const char *action = v["action"] | "";
    const char *detail = v["detail"] | "";
    if (action[0] == '\0') continue;
    g_audit_events.push_back({std::string(ts), std::string(action), std::string(detail)});
    if (g_audit_events.size() > kMaxAuditEvents) g_audit_events.pop_front();
  }
}

std::string utcNowIso() {
  time_t now = time(nullptr);
  struct tm info;
  gmtime_r(&now, &info);
  char iso[32];
  strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &info);
  return std::string(iso);
}

bool isCriticalAuditAction(const std::string &action) {
  if (action == "server.start" || action == "device.startup") return true;
  if (action == "auth.login.success" || action == "auth.logout") return true;
  if (action == "firmware.ota.success" || action == "firmware.ota.failed") return true;
  if (action == "wifi.config.updated" || action == "totp.time_provisioned") return true;
  if (action == "incoming.blacklist.add" || action == "incoming.whitelist.add") return true;
  if (action == "outgoing.whitelist.add" || action == "outgoing.blacklist.add" || action == "outgoing.shortcut.add") return true;
  return false;
}

void auditLog(const std::string &action, const std::string &detail = "") {
  if (g_audit_events.size() >= kMaxAuditEvents) g_audit_events.pop_front();
  g_audit_events.push_back({utcNowIso(), action, detail});
  g_audit_dirty = true;
  if (isCriticalAuditAction(action)) saveAuditEventsToFs();
}

void persistConfig() {
  if (g_config == nullptr) return;
  saveConfig(*g_config);
  std::vector<std::string> secrets;
  for (const auto &entry : g_config->totp_secrets) {
    if (entry.active && !entry.secret.empty()) secrets.push_back(entry.secret);
  }
  web_totp_set_secrets(secrets);
}

std::string generateSessionToken() {
  char buf[17];
  uint32_t a = esp_random();
  uint32_t b = esp_random();
  snprintf(buf, sizeof(buf), "%08lx%08lx", static_cast<unsigned long>(a), static_cast<unsigned long>(b));
  return std::string(buf);
}

std::string jsonEscape(const std::string &in) {
  std::string out;
  out.reserve(in.size() + 32);
  for (char c : in) {
    switch (c) {
      case '\"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(c); break;
    }
  }
  return out;
}

void jsonEscapeAppend(std::string *out, const std::string &in) {
  if (out == nullptr) return;
  for (char c : in) {
    switch (c) {
      case '\"': *out += "\\\""; break;
      case '\\': *out += "\\\\"; break;
      case '\n': *out += "\\n"; break;
      case '\r': *out += "\\r"; break;
      case '\t': *out += "\\t"; break;
      default: out->push_back(c); break;
    }
  }
}

const char *httpStatusText(int code) {
  switch (code) {
    case 200: return "200 OK";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 409: return "409 Conflict";
    case 500: return "500 Internal Server Error";
    case 503: return "503 Service Unavailable";
    default: return "500 Internal Server Error";
  }
}

esp_err_t sendRaw(httpd_req_t *req, int code, const char *type, const std::string &body) {
  httpd_resp_set_status(req, httpStatusText(code));
  httpd_resp_set_type(req, type);
  return httpd_resp_send(req, body.c_str(), body.size());
}

esp_err_t sendJson(httpd_req_t *req, int code, const std::string &json) {
  httpd_resp_set_status(req, httpStatusText(code));
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json.c_str(), json.size());
}

esp_err_t sendJsonError(httpd_req_t *req, int code, const char *err) {
  String payload = String("{\"error\":\"") + err + "\"}";
  return sendJson(req, code, payload.c_str());
}

bool readRequestBody(httpd_req_t *req, std::string *out) {
  if (out == nullptr) return false;
  out->clear();
  int remaining = req->content_len;
  if (remaining <= 0) return true;
  out->reserve(remaining);
  char buf[512];
  while (remaining > 0) {
    int received = httpd_req_recv(req, buf, std::min<int>(remaining, sizeof(buf)));
    if (received <= 0) return false;
    out->append(buf, received);
    remaining -= received;
  }
  return true;
}

bool parseJsonBody(httpd_req_t *req, DynamicJsonDocument *doc, size_t cap = 12288) {
  if (doc == nullptr) return false;
  std::string body;
  if (!readRequestBody(req, &body)) return false;
  DynamicJsonDocument tmp(cap);
  if (deserializeJson(tmp, body.data(), body.size())) return false;
  *doc = std::move(tmp);
  return true;
}

std::string headerValue(httpd_req_t *req, const char *name) {
  size_t len = httpd_req_get_hdr_value_len(req, name);
  if (len == 0) return "";
  std::vector<char> buf(len + 1, '\0');
  if (httpd_req_get_hdr_value_str(req, name, buf.data(), buf.size()) != ESP_OK) return "";
  return std::string(buf.data());
}

bool hasValidSession(httpd_req_t *req) {
  if (g_session_token.empty()) return false;

  const std::string token_header = headerValue(req, "X-Session-Token");
  return !token_header.empty() && token_header == g_session_token;
}

bool requireAuth(httpd_req_t *req) {
  if (hasValidSession(req)) return true;
  sendJsonError(req, 401, "unauthorized");
  return false;
}

// Wi-Fi read/write without TOTP only on the setup SoftAP (no SSID / STA failed / after factory Wi-Fi clear).
bool requireWifiConfigAccess(httpd_req_t *req) {
  if (wifi_soft_ap_setup_mode()) return true;
  if (hasValidSession(req)) return true;
  sendJsonError(req, 401, "wifi changes require login when not on setup hotspot");
  return false;
}

bool requireOtaSessionOrSetupAp(httpd_req_t *req) {
  if (hasValidSession(req)) return true;
  if (wifi_soft_ap_setup_mode()) return true;
  sendJsonError(req, 401, "unauthorized");
  return false;
}

std::string generateBase32Secret(size_t bytes_len) {
  static constexpr char kBase32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  std::string secret;
  std::array<uint8_t, 32> random_bytes{};
  if (bytes_len > random_bytes.size()) bytes_len = random_bytes.size();
  for (size_t i = 0; i < bytes_len; ++i) random_bytes[i] = static_cast<uint8_t>(esp_random() & 0xFF);
  uint32_t buffer = 0;
  int bits_left = 0;
  for (size_t i = 0; i < bytes_len; ++i) {
    buffer = (buffer << 8) | random_bytes[i];
    bits_left += 8;
    while (bits_left >= 5) {
      bits_left -= 5;
      secret.push_back(kBase32Alphabet[(buffer >> bits_left) & 0x1F]);
    }
  }
  if (bits_left > 0) secret.push_back(kBase32Alphabet[(buffer << (5 - bits_left)) & 0x1F]);
  return secret;
}

std::string urlEncode(const std::string &value) {
  static const char *hex = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(value.size() * 3);
  for (unsigned char c : value) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded.push_back(static_cast<char>(c));
    } else {
      encoded.push_back('%');
      encoded.push_back(hex[(c >> 4) & 0x0F]);
      encoded.push_back(hex[c & 0x0F]);
    }
  }
  return encoded;
}

void appendTotpCreationPayload(JsonDocument *doc, const std::string &name, const std::string &secret) {
  const std::string issuer = "PhoneFilter";
  const std::string otpauth_uri =
      "otpauth://totp/" + urlEncode(issuer + ":" + name) + "?secret=" + urlEncode(secret) + "&issuer=" + urlEncode(issuer) + "&digits=6&period=30";
  (*doc)["name"] = name.c_str();
  (*doc)["otpauth_uri"] = otpauth_uri.c_str();
}

esp_err_t sendFsFile(httpd_req_t *req, const char *path, const char *content_type) {
  if (!LittleFS.exists(path)) return sendJsonError(req, 404, "file not found");
  File file = LittleFS.open(path, "r");
  if (!file) return sendJsonError(req, 404, "file not found");
  httpd_resp_set_type(req, content_type);
  char buf[1024];
  while (file.available()) {
    size_t n = file.readBytes(buf, sizeof(buf));
    if (n == 0) break;
    if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
      file.close();
      return ESP_FAIL;
    }
  }
  file.close();
  return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t handle_blacklist_get(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  StaticJsonDocument<512> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (auto &n : g_config->incoming_blacklist) arr.add(n);
  String res;
  serializeJson(doc, res);
  return sendJson(req, 200, res.c_str());
}

esp_err_t handle_blacklist_add(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  DynamicJsonDocument doc(512);
  if (!parseJsonBody(req, &doc, 512) || !doc["number"].is<const char*>()) return sendJsonError(req, 400, "invalid payload");
  std::string num = doc["number"].as<std::string>();
  g_config->incoming_blacklist.push_back(num);
  persistConfig();
  auditLog("incoming.blacklist.add", "number=" + num);
  return sendJson(req, 200, "{\"ok\":true}");
}

esp_err_t handle_blacklist_remove(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  DynamicJsonDocument doc(512);
  if (!parseJsonBody(req, &doc, 512) || !doc["number"].is<const char*>()) return sendJsonError(req, 400, "invalid payload");
  std::string num = doc["number"].as<std::string>();
  g_config->incoming_blacklist.erase(
      std::remove(g_config->incoming_blacklist.begin(), g_config->incoming_blacklist.end(), num), g_config->incoming_blacklist.end());
  persistConfig();
  return sendJson(req, 200, "{\"ok\":true}");
}

esp_err_t handle_whitelist_get(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  StaticJsonDocument<512> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (auto &n : g_config->incoming_whitelist) arr.add(n);
  String res;
  serializeJson(doc, res);
  return sendJson(req, 200, res.c_str());
}

esp_err_t handle_whitelist_add(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  DynamicJsonDocument doc(512);
  if (!parseJsonBody(req, &doc, 512) || !doc["number"].is<const char*>()) return sendJsonError(req, 400, "invalid payload");
  std::string num = doc["number"].as<std::string>();
  g_config->incoming_whitelist.push_back(num);
  persistConfig();
  auditLog("incoming.whitelist.add", "number=" + num);
  return sendJson(req, 200, "{\"ok\":true}");
}

esp_err_t handle_whitelist_remove(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  DynamicJsonDocument doc(512);
  if (!parseJsonBody(req, &doc, 512) || !doc["number"].is<const char*>()) return sendJsonError(req, 400, "invalid payload");
  std::string num = doc["number"].as<std::string>();
  g_config->incoming_whitelist.erase(
      std::remove(g_config->incoming_whitelist.begin(), g_config->incoming_whitelist.end(), num), g_config->incoming_whitelist.end());
  persistConfig();
  return sendJson(req, 200, "{\"ok\":true}");
}

esp_err_t handle_outgoing_whitelist_get(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  StaticJsonDocument<512> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (auto &p : g_config->outgoing_whitelist) arr.add(p);
  String res;
  serializeJson(doc, res);
  return sendJson(req, 200, res.c_str());
}

esp_err_t handle_outgoing_blacklist_get(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  StaticJsonDocument<512> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (auto &p : g_config->outgoing_blacklist) arr.add(p);
  String res;
  serializeJson(doc, res);
  return sendJson(req, 200, res.c_str());
}

esp_err_t handle_outgoing_whitelist_add(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  DynamicJsonDocument doc(512);
  if (!parseJsonBody(req, &doc, 512) || !doc["prefix"].is<const char*>()) return sendJsonError(req, 400, "invalid payload");
  std::string prefix = doc["prefix"].as<std::string>();
  g_config->outgoing_whitelist.push_back(prefix);
  persistConfig();
  auditLog("outgoing.whitelist.add", "prefix=" + prefix);
  return sendJson(req, 200, "{\"ok\":true}");
}

esp_err_t handle_outgoing_blacklist_add(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  DynamicJsonDocument doc(512);
  if (!parseJsonBody(req, &doc, 512) || !doc["prefix"].is<const char*>()) return sendJsonError(req, 400, "invalid payload");
  std::string prefix = doc["prefix"].as<std::string>();
  g_config->outgoing_blacklist.push_back(prefix);
  persistConfig();
  auditLog("outgoing.blacklist.add", "prefix=" + prefix);
  return sendJson(req, 200, "{\"ok\":true}");
}

esp_err_t handle_outgoing_whitelist_remove(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  DynamicJsonDocument doc(512);
  if (!parseJsonBody(req, &doc, 512) || !doc["prefix"].is<const char*>()) return sendJsonError(req, 400, "invalid payload");
  std::string prefix = doc["prefix"].as<std::string>();
  g_config->outgoing_whitelist.erase(
      std::remove(g_config->outgoing_whitelist.begin(), g_config->outgoing_whitelist.end(), prefix), g_config->outgoing_whitelist.end());
  persistConfig();
  return sendJson(req, 200, "{\"ok\":true}");
}

esp_err_t handle_outgoing_blacklist_remove(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  DynamicJsonDocument doc(512);
  if (!parseJsonBody(req, &doc, 512) || !doc["prefix"].is<const char*>()) return sendJsonError(req, 400, "invalid payload");
  std::string prefix = doc["prefix"].as<std::string>();
  g_config->outgoing_blacklist.erase(
      std::remove(g_config->outgoing_blacklist.begin(), g_config->outgoing_blacklist.end(), prefix), g_config->outgoing_blacklist.end());
  persistConfig();
  return sendJson(req, 200, "{\"ok\":true}");
}

esp_err_t handle_outgoing_shortcuts_get(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  StaticJsonDocument<1024> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (auto &rule : g_config->outgoing_shortcuts) {
    JsonObject obj = arr.createNestedObject();
    obj["trigger"] = rule.trigger.c_str();
    obj["replacement"] = rule.replacement.c_str();
  }
  String res;
  serializeJson(doc, res);
  return sendJson(req, 200, res.c_str());
}

esp_err_t handle_outgoing_shortcuts_add(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  DynamicJsonDocument doc(512);
  if (!parseJsonBody(req, &doc, 512) || !doc["trigger"].is<const char*>() || !doc["replacement"].is<const char*>()) return sendJsonError(req, 400, "invalid payload");
  std::string trigger = doc["trigger"].as<std::string>();
  std::string replacement = doc["replacement"].as<std::string>();
  if (trigger.empty() || replacement.empty()) return sendJsonError(req, 400, "invalid payload");
  g_config->outgoing_shortcuts.push_back({trigger, replacement});
  persistConfig();
  auditLog("outgoing.shortcut.add", "trigger=" + trigger + " replacement=" + replacement);
  return sendJson(req, 200, "{\"ok\":true}");
}

esp_err_t handle_outgoing_shortcuts_remove(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  DynamicJsonDocument doc(512);
  if (!parseJsonBody(req, &doc, 512) || !doc["trigger"].is<const char*>()) return sendJsonError(req, 400, "invalid payload");
  std::string trigger = doc["trigger"].as<std::string>();
  g_config->outgoing_shortcuts.erase(
      std::remove_if(g_config->outgoing_shortcuts.begin(), g_config->outgoing_shortcuts.end(), [&trigger](const ShortcutRule &r) { return r.trigger == trigger; }),
      g_config->outgoing_shortcuts.end());
  persistConfig();
  return sendJson(req, 200, "{\"ok\":true}");
}

esp_err_t handle_totp_secrets_get(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  StaticJsonDocument<1024> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto &entry : g_config->totp_secrets) {
    if (!entry.active) continue;
    JsonObject obj = arr.createNestedObject();
    obj["name"] = entry.name.c_str();
  }
  String res;
  serializeJson(doc, res);
  return sendJson(req, 200, res.c_str());
}

esp_err_t handle_totp_bootstrap_status(httpd_req_t *req) {
  size_t active_count = 0;
  bool has_pending_bootstrap = false;
  for (const auto &entry : g_config->totp_secrets) {
    if (entry.active) ++active_count;
    else has_pending_bootstrap = true;
  }
  StaticJsonDocument<128> doc;
  doc["needs_bootstrap"] = active_count == 0;
  doc["secret_count"] = active_count;
  doc["has_pending_bootstrap"] = has_pending_bootstrap;
  String res;
  serializeJson(doc, res);
  return sendJson(req, 200, res.c_str());
}

esp_err_t handle_totp_bootstrap_new(httpd_req_t *req) {
  if (wifi_soft_ap_setup_mode()) {
    return sendJsonError(req, 403, "bootstrap disabled on setup hotspot; use TOTP after Wi-Fi is configured");
  }
  for (const auto &entry : g_config->totp_secrets) {
    if (entry.active) return sendJsonError(req, 409, "bootstrap already completed");
  }
  for (const auto &entry : g_config->totp_secrets) {
    if (entry.active || entry.secret.empty()) continue;
    StaticJsonDocument<768> doc;
    appendTotpCreationPayload(&doc, entry.name, entry.secret);
    String res;
    serializeJson(doc, res);
    return sendJson(req, 200, res.c_str());
  }
  DynamicJsonDocument body(512);
  std::string name = "Bootstrap token";
  if (parseJsonBody(req, &body, 512) && body["name"].is<const char*>()) name = body["name"].as<std::string>();
  std::string secret = generateBase32Secret(20);
  TotpSecretEntry bootstrap_entry;
  bootstrap_entry.name = name;
  bootstrap_entry.secret = secret;
  bootstrap_entry.active = false;
  g_config->totp_secrets.push_back(bootstrap_entry);
  saveConfig(*g_config);
  StaticJsonDocument<768> doc;
  appendTotpCreationPayload(&doc, name, secret);
  String res;
  serializeJson(doc, res);
  return sendJson(req, 200, res.c_str());
}

esp_err_t handle_totp_secrets_new(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  DynamicJsonDocument body(512);
  if (!parseJsonBody(req, &body, 512) || !body["name"].is<const char*>()) return sendJsonError(req, 400, "invalid payload");
  std::string name = body["name"].as<std::string>();
  if (name.empty()) return sendJsonError(req, 400, "invalid payload");
  for (const auto &entry : g_config->totp_secrets) {
    if (entry.name == name) return sendJsonError(req, 409, "name already exists");
  }
  std::string secret = generateBase32Secret(20);
  TotpSecretEntry entry;
  entry.name = name;
  entry.secret = secret;
  entry.active = true;
  g_config->totp_secrets.push_back(entry);
  persistConfig();
  StaticJsonDocument<768> doc;
  appendTotpCreationPayload(&doc, name, secret);
  String res;
  serializeJson(doc, res);
  return sendJson(req, 200, res.c_str());
}

esp_err_t handle_totp_secrets_remove(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  DynamicJsonDocument body(512);
  if (!parseJsonBody(req, &body, 512) || !body["name"].is<const char*>()) return sendJsonError(req, 400, "invalid payload");
  std::string name = body["name"].as<std::string>();
  g_config->totp_secrets.erase(
      std::remove_if(g_config->totp_secrets.begin(), g_config->totp_secrets.end(), [&name](const TotpSecretEntry &e) { return e.name == name; }),
      g_config->totp_secrets.end());
  persistConfig();
  return sendJson(req, 200, "{\"ok\":true}");
}

esp_err_t handle_totp_status(httpd_req_t *req) {
  StaticJsonDocument<128> doc;
  doc["time_synced"] = web_totp_is_time_synced();
  String res;
  serializeJson(doc, res);
  return sendJson(req, 200, res.c_str());
}

esp_err_t handle_https_config_get(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  // Avoid chained operator+ with jsonEscape(cert) + jsonEscape(key): each temp std::string
  // can double peak heap use and throw std::bad_alloc (abort) on ESP32.
  std::string payload;
  const size_t ncert = g_config->https_cert_pem.size();
  const size_t nkey = g_config->https_key_pem.size();
  payload.reserve(ncert + nkey + 64);
  payload.assign("{\"cert_pem\":\"");
  jsonEscapeAppend(&payload, g_config->https_cert_pem);
  payload.append("\",\"key_pem\":\"");
  jsonEscapeAppend(&payload, g_config->https_key_pem);
  payload.append("\",\"needs_restart\":true}");
  return sendJson(req, 200, payload);
}

esp_err_t handle_https_config_post(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  DynamicJsonDocument body(12288);
  if (!parseJsonBody(req, &body, 12288) || !body["cert_pem"].is<const char*>() || !body["key_pem"].is<const char*>()) {
    return sendJsonError(req, 400, "invalid payload");
  }
  std::string cert = body["cert_pem"].as<std::string>();
  std::string key = body["key_pem"].as<std::string>();
  const bool is_rsa_key = key.find("BEGIN RSA PRIVATE KEY") != std::string::npos;
  const bool is_ec_key = key.find("BEGIN EC PRIVATE KEY") != std::string::npos;
  if (cert.find("BEGIN CERTIFICATE") == std::string::npos || (!is_rsa_key && !is_ec_key)) {
    return sendJsonError(req, 400, "invalid cert or key format (expect PEM cert + RSA/EC private key)");
  }
  g_config->https_cert_pem = cert;
  g_config->https_key_pem = key;
  saveConfig(*g_config);
  auditLog("https.config.updated", "updated");
  return sendJson(req, 200, "{\"ok\":true,\"needs_restart\":true}");
}

esp_err_t handle_wifi_config_get(httpd_req_t *req) {
  if (!requireWifiConfigAccess(req)) return ESP_OK;
  std::string payload = "{\"wifi_ssid\":\"" + jsonEscape(g_config->wifi_ssid) +
                        "\",\"wifi_password\":\"" + jsonEscape(g_config->wifi_password) + "\"}";
  return sendJson(req, 200, payload);
}

esp_err_t handle_wifi_config_post(httpd_req_t *req) {
  if (!requireWifiConfigAccess(req)) return ESP_OK;
  DynamicJsonDocument body(2048);
  if (!parseJsonBody(req, &body, 2048) || !body["wifi_ssid"].is<const char*>()) {
    return sendJsonError(req, 400, "invalid payload");
  }
  g_config->wifi_ssid = body["wifi_ssid"].as<std::string>();
  g_config->wifi_password =
      body["wifi_password"].is<const char*>() ? body["wifi_password"].as<std::string>() : std::string();
  saveConfig(*g_config);
  auditLog("wifi.config.updated", "ssid=" + g_config->wifi_ssid);
  if (wifi_soft_ap_setup_mode()) {
    esp_err_t out = sendJson(req, 200, "{\"ok\":true,\"restarting\":true}");
    delay(400);
    ESP.restart();
    return out;
  }
  return sendJson(req, 200, "{\"ok\":true,\"needs_restart\":true}");
}

esp_err_t handle_totp_time(httpd_req_t *req) {
  time_t now = time(nullptr);
  struct tm info;
  gmtime_r(&now, &info);
  char iso[32];
  strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &info);
  StaticJsonDocument<192> doc;
  doc["epoch"] = static_cast<unsigned long>(now);
  doc["iso"] = iso;
  doc["time_synced"] = web_totp_is_time_synced();
  String res;
  serializeJson(doc, res);
  return sendJson(req, 200, res.c_str());
}

esp_err_t handle_totp_sync(httpd_req_t *req) {
  // Allow one NTP attempt without a session only while the clock has never synced (login / bootstrap).
  if (web_totp_is_time_synced() && !requireAuth(req)) return ESP_OK;
  bool ok = web_totp_sync_time_with_ntp();
  StaticJsonDocument<128> doc;
  doc["ok"] = ok;
  doc["time_synced"] = web_totp_is_time_synced();
  String res;
  serializeJson(doc, res);
  return sendJson(req, ok ? 200 : 500, res.c_str());
}

esp_err_t handle_auth_status(httpd_req_t *req) {
  StaticJsonDocument<160> doc;
  doc["authenticated"] = hasValidSession(req);
  doc["wifi_setup_portal"] = wifi_soft_ap_setup_mode();
  String res;
  serializeJson(doc, res);
  return sendJson(req, 200, res.c_str());
}

esp_err_t handle_setup_status(httpd_req_t *req) {
  DynamicJsonDocument doc(384);
  doc["setup_ap"] = wifi_soft_ap_setup_mode();
  if (wifi_soft_ap_setup_mode()) {
    doc["ap_ip"] = WiFi.softAPIP().toString();
  }
  String out;
  serializeJson(doc, out);
  return sendJson(req, 200, out.c_str());
}

esp_err_t handle_totp_time_provision(httpd_req_t *req) {
  if (!wifi_soft_ap_setup_mode()) {
    return sendJsonError(req, 403, "only available on setup hotspot");
  }
  DynamicJsonDocument body(128);
  if (!parseJsonBody(req, &body, 128) || !body.containsKey("unix")) {
    return sendJsonError(req, 400, "invalid payload");
  }
  const long long unix_ll = body["unix"].as<long long>();
  if (unix_ll < 1700000000LL || unix_ll > 4000000000LL) {
    return sendJsonError(req, 400, "invalid unix time");
  }
  struct timeval tv {};
  tv.tv_sec = static_cast<time_t>(unix_ll);
  tv.tv_usec = 0;
  if (settimeofday(&tv, nullptr) != 0) {
    return sendJsonError(req, 500, "settimeofday failed");
  }
  auditLog("totp.time_provisioned", "source=browser");
  return sendJson(req, 200, "{\"ok\":true}");
}

esp_err_t handle_auth_login(httpd_req_t *req) {
  DynamicJsonDocument body(512);
  if (!parseJsonBody(req, &body, 512) || !body.containsKey("totp")) return sendJsonError(req, 400, "invalid payload");
  std::string totp;
  {
    JsonVariant tv = body["totp"];
    if (tv.is<const char*>()) {
      totp = tv.as<std::string>();
    } else if (tv.is<long>() || tv.is<int>()) {
      const long v = tv.as<long>();
      if (v < 0 || v > 999999L) return sendJsonError(req, 400, "invalid payload");
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%06ld", v);
      totp = buf;
    } else {
      return sendJsonError(req, 400, "invalid payload");
    }
  }

  // TOTP is time-based: try one on-demand sync if clock is not ready.
  if (!web_totp_is_time_synced()) {
    web_totp_sync_time_with_ntp();
  }
  if (!web_totp_is_time_synced()) {
    return sendJsonError(req, 503, "time not synchronized");
  }

  bool validated = false;
  std::string matched_token_name;
  for (auto &entry : g_config->totp_secrets) {
    if (entry.secret.empty()) continue;
    if (!web_totp_validate_token_with_secret(totp, entry.secret)) continue;
    validated = true;
    matched_token_name = entry.name;
    if (!entry.active) {
      entry.active = true;
      persistConfig();
    }
    break;
  }
  if (!validated) return sendJsonError(req, 401, "invalid totp");
  g_session_token = generateSessionToken();
  g_session_token_name = matched_token_name;
  auditLog("auth.login.success", "token=" + matched_token_name);
  std::string payload = "{\"ok\":true,\"session_token\":\"" + g_session_token + "\"}";
  return sendJson(req, 200, payload);
}

esp_err_t handle_auth_logout(httpd_req_t *req) {
  const std::string token_name = g_session_token_name.empty() ? "unknown" : g_session_token_name;
  g_session_token.clear();
  g_session_token_name.clear();
  auditLog("auth.logout", "token=" + token_name);
  return sendJson(req, 200, "{\"ok\":true}");
}

esp_err_t handle_firmware_ota(httpd_req_t *req) {
  if (!requireOtaSessionOrSetupAp(req)) return ESP_OK;
  const std::string totp_hdr = headerValue(req, "X-TOTP");
  if (totp_hdr.empty() || !web_totp_validate_token(totp_hdr)) {
    return sendJsonError(req, 401, "valid X-TOTP header required");
  }

  const int total_len = req->content_len;
  if (total_len <= 0) {
    return sendJsonError(req, 400, "Content-Length required with non-empty firmware body");
  }

  const esp_partition_t *update_part = esp_ota_get_next_update_partition(nullptr);
  if (update_part == nullptr) {
    auditLog("firmware.ota.failed", "reason=no_update_partition");
    return sendJsonError(req, 500, "no OTA partition");
  }
  if (static_cast<size_t>(total_len) > update_part->size) {
    auditLog("firmware.ota.failed", "reason=image_too_large");
    return sendJsonError(req, 400, "firmware larger than OTA slot");
  }

  if (!Update.begin(static_cast<size_t>(total_len))) {
    const char *err_str = Update.errorString();
    auditLog("firmware.ota.failed", std::string("begin_failed:") + (err_str ? err_str : ""));
    return sendJsonError(req, 500, err_str ? err_str : "update begin failed");
  }

  int remaining = total_len;
  std::array<char, 2048> buf{};
  while (remaining > 0) {
    const int chunk = std::min(remaining, static_cast<int>(buf.size()));
    const int r = httpd_req_recv(req, buf.data(), chunk);
    if (r <= 0) {
      Update.abort();
      auditLog("firmware.ota.failed", "reason=recv_interrupted");
      return sendJsonError(req, 500, "upload interrupted");
    }
    if (Update.write(reinterpret_cast<uint8_t *>(buf.data()), static_cast<size_t>(r)) != static_cast<size_t>(r)) {
      Update.abort();
      auditLog("firmware.ota.failed", "reason=flash_write");
      return sendJsonError(req, 500, "flash write failed");
    }
    remaining -= r;
    yield();
  }

  if (!Update.end(true)) {
    const char *err_str = Update.errorString();
    auditLog("firmware.ota.failed", std::string("end_failed:") + (err_str ? err_str : ""));
    return sendJsonError(req, 500, err_str ? err_str : "update end failed");
  }

  auditLog("firmware.ota.success", "bytes=" + std::to_string(total_len));
  esp_err_t out = sendJson(req, 200, "{\"ok\":true,\"restarting\":true}");
  delay(250);
  ESP.restart();
  return out;
}

esp_err_t handle_audit(httpd_req_t *req) {
  if (!requireAuth(req)) return ESP_OK;
  DynamicJsonDocument doc(24576);
  JsonArray arr = doc.to<JsonArray>();
  for (auto it = g_audit_events.rbegin(); it != g_audit_events.rend(); ++it) {
    JsonObject obj = arr.createNestedObject();
    obj["ts"] = it->ts.c_str();
    obj["action"] = it->action.c_str();
    obj["detail"] = it->detail.c_str();
  }
  String res;
  serializeJson(doc, res);
  return sendJson(req, 200, res.c_str());
}

esp_err_t handle_static(httpd_req_t *req) {
  std::string path = req->uri;
  const size_t q = path.find('?');
  if (q != std::string::npos) path.erase(q);
  if (path.size() < 9 || path.compare(0, 8, "/static/") != 0) return sendJsonError(req, 404, "not found");
  if (path.find("..") != std::string::npos) return sendJsonError(req, 400, "bad path");

  const char *ct = "application/octet-stream";
  if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".woff2") == 0) {
    ct = "font/woff2";
  } else if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".woff") == 0) {
    ct = "font/woff";
  } else if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".css") == 0) {
    ct = "text/css; charset=utf-8";
  } else if (path.size() >= 3 && path.compare(path.size() - 3, 3, ".js") == 0) {
    ct = "application/javascript; charset=utf-8";
  } else if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".html") == 0) {
    ct = "text/html; charset=utf-8";
  }
  return sendFsFile(req, path.c_str(), ct);
}

esp_err_t handle_docs_index(httpd_req_t *req) { return sendFsFile(req, "/api/docs/index.html", "text/html"); }
esp_err_t handle_docs_openapi(httpd_req_t *req) { return sendFsFile(req, "/api/docs/openapi.yaml", "application/x-yaml"); }
esp_err_t handle_root(httpd_req_t *req) { return sendFsFile(req, "/index.html", "text/html"); }
esp_err_t handle_index_html(httpd_req_t *req) { return sendFsFile(req, "/index.html", "text/html"); }

void registerUri(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *)) {
  httpd_uri_t spec = {};
  spec.uri = uri;
  spec.method = method;
  spec.handler = handler;
  spec.user_ctx = nullptr;
  esp_err_t err = httpd_register_uri_handler(g_https_server, &spec);
  if (err != ESP_OK) {
    Serial.printf("Failed to register URI %s (method=%d): %s (0x%x)\n", uri, static_cast<int>(method),
                  esp_err_to_name(err), static_cast<unsigned int>(err));
  }
}

std::string httpRedirectHost(httpd_req_t *req) {
  std::string h = headerValue(req, "Host");
  if (!h.empty()) {
    if (h.size() > 3 && h.compare(h.size() - 3, 3, ":80") == 0) h.resize(h.size() - 3);
    return h;
  }
  if (wifi_soft_ap_setup_mode()) return std::string(WiFi.softAPIP().toString().c_str());
  return std::string(WiFi.localIP().toString().c_str());
}

esp_err_t handleHttpToHttpsRedirect(httpd_req_t *req) {
  std::string host = httpRedirectHost(req);
  const char *uri = req->uri;
  if (uri[0] == '\0') uri = "/";
  std::string loc = "https://" + host + uri;
  if (loc.size() > 512) loc = "https://" + host + "/";
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", loc.c_str());
  httpd_resp_set_hdr(req, "Connection", "close");
  return httpd_resp_send(req, nullptr, 0);
}

void registerHttpRedirectHandlers() {
  static const httpd_method_t kMethods[] = {HTTP_GET, HTTP_HEAD, HTTP_POST, HTTP_PUT, HTTP_DELETE, HTTP_OPTIONS};
  for (httpd_method_t m : kMethods) {
    httpd_uri_t spec = {};
    spec.uri = "/*";
    spec.method = m;
    spec.handler = handleHttpToHttpsRedirect;
    spec.user_ctx = nullptr;
    esp_err_t err = httpd_register_uri_handler(g_http_redirect_server, &spec);
    if (err != ESP_OK) {
      Serial.printf("HTTP redirect: register /* method=%d failed: %s\n", static_cast<int>(m), esp_err_to_name(err));
    }
  }
}

bool startHttpRedirectServer() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 80;
  cfg.max_open_sockets = 3;
  cfg.max_uri_handlers = 8;
  cfg.uri_match_fn = httpd_uri_match_wildcard;
  cfg.lru_purge_enable = true;
  esp_err_t err = httpd_start(&g_http_redirect_server, &cfg);
  if (err != ESP_OK || g_http_redirect_server == nullptr) {
    Serial.printf("HTTP redirect server (port 80) failed: %s\n", esp_err_to_name(err));
    return false;
  }
  registerHttpRedirectHandlers();
  Serial.println("HTTP port 80 -> 302 redirect to HTTPS (443).");
  return true;
}
}  // namespace

void web_server_setup(Config *config) {
  g_config = config;
  loadAuditEventsFromFs();
  auditLog("server.start", "web server setup");

  if (g_config->https_cert_pem.empty() || g_config->https_key_pem.empty()) {
    Serial.println("HTTPS cert/key missing; refusing to start web server.");
    return;
  }

  httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
  conf.httpd.server_port = 443;
  // Parallel TLS handshakes allocate a lot (~tens of KiB each). Too many => mbedtls -0x7780
  // (MBEDTLS_ERR_SSL_ALLOC_FAILED) and esp_tls_create_server_session 0x7780. 4 sockets is a safer
  // default on ESP32; browsers may open extra tabs but LRU purge frees idle sockets.
  conf.httpd.max_open_sockets = 4;
  // Handshake and mbedTLS work need headroom on the httpd task stack (default is often 10240).
  conf.httpd.stack_size = 16384;
  // Must exceed the number of registerUri() calls (see esp_err.h: 0xb001 = HANDLERS_FULL).
  conf.httpd.max_uri_handlers = 55;
  conf.httpd.lru_purge_enable = true;
  conf.httpd.uri_match_fn = httpd_uri_match_wildcard;
  conf.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  // IDF 5+: dedicated server cert fields (cacert_pem is for CA / client auth).
  conf.servercert = reinterpret_cast<const uint8_t *>(g_config->https_cert_pem.c_str());
  conf.servercert_len = g_config->https_cert_pem.size() + 1;
  conf.cacert_pem = nullptr;
  conf.cacert_len = 0;
#else
  // IDF 4.x (Arduino-esp32 2.x): server PEM historically lives in cacert_pem (see upstream note
  // in esp_https_server.h).
  conf.cacert_pem = reinterpret_cast<const uint8_t *>(g_config->https_cert_pem.c_str());
  conf.cacert_len = g_config->https_cert_pem.size() + 1;
#endif
  conf.prvtkey_pem = reinterpret_cast<const uint8_t *>(g_config->https_key_pem.c_str());
  conf.prvtkey_len = g_config->https_key_pem.size() + 1;

  const esp_err_t ssl_err = httpd_ssl_start(&g_https_server, &conf);
  if (ssl_err != ESP_OK || g_https_server == nullptr) {
    Serial.printf("Failed to start esp_https_server: %s (0x%x)\n",
                  esp_err_to_name(ssl_err),
                  static_cast<unsigned int>(ssl_err));
    return;
  }

  registerUri("/api/incoming/blacklist", HTTP_GET, handle_blacklist_get);
  registerUri("/api/incoming/blacklist/add", HTTP_POST, handle_blacklist_add);
  registerUri("/api/incoming/blacklist/remove", HTTP_POST, handle_blacklist_remove);
  registerUri("/api/incoming/whitelist", HTTP_GET, handle_whitelist_get);
  registerUri("/api/incoming/whitelist/add", HTTP_POST, handle_whitelist_add);
  registerUri("/api/incoming/whitelist/remove", HTTP_POST, handle_whitelist_remove);
  registerUri("/api/outgoing/whitelist", HTTP_GET, handle_outgoing_whitelist_get);
  registerUri("/api/outgoing/whitelist/add", HTTP_POST, handle_outgoing_whitelist_add);
  registerUri("/api/outgoing/whitelist/remove", HTTP_POST, handle_outgoing_whitelist_remove);
  registerUri("/api/outgoing/blacklist", HTTP_GET, handle_outgoing_blacklist_get);
  registerUri("/api/outgoing/blacklist/add", HTTP_POST, handle_outgoing_blacklist_add);
  registerUri("/api/outgoing/blacklist/remove", HTTP_POST, handle_outgoing_blacklist_remove);
  registerUri("/api/outgoing/shortcuts", HTTP_GET, handle_outgoing_shortcuts_get);
  registerUri("/api/outgoing/shortcuts/add", HTTP_POST, handle_outgoing_shortcuts_add);
  registerUri("/api/outgoing/shortcuts/remove", HTTP_POST, handle_outgoing_shortcuts_remove);
  registerUri("/api/totp/secrets", HTTP_GET, handle_totp_secrets_get);
  registerUri("/api/totp/bootstrap/status", HTTP_GET, handle_totp_bootstrap_status);
  registerUri("/api/totp/bootstrap/new", HTTP_POST, handle_totp_bootstrap_new);
  registerUri("/api/totp/secrets/new", HTTP_POST, handle_totp_secrets_new);
  registerUri("/api/totp/secrets/remove", HTTP_POST, handle_totp_secrets_remove);
  registerUri("/api/totp/status", HTTP_GET, handle_totp_status);
  registerUri("/api/totp/time", HTTP_GET, handle_totp_time);
  registerUri("/api/totp/sync", HTTP_POST, handle_totp_sync);
  registerUri("/api/totp/time/provision", HTTP_POST, handle_totp_time_provision);
  registerUri("/api/setup/status", HTTP_GET, handle_setup_status);
  registerUri("/api/https/config", HTTP_GET, handle_https_config_get);
  registerUri("/api/https/config", HTTP_POST, handle_https_config_post);
  // Alias without "https" in the path — some browsers / middleboxes mishandle `/api/https/...`.
  registerUri("/api/tls/config", HTTP_GET, handle_https_config_get);
  registerUri("/api/tls/config", HTTP_POST, handle_https_config_post);
  registerUri("/api/wifi/config", HTTP_GET, handle_wifi_config_get);
  registerUri("/api/wifi/config", HTTP_POST, handle_wifi_config_post);
  registerUri("/api/auth/status", HTTP_GET, handle_auth_status);
  registerUri("/api/auth/login", HTTP_POST, handle_auth_login);
  registerUri("/api/auth/logout", HTTP_POST, handle_auth_logout);
  registerUri("/api/firmware/ota", HTTP_POST, handle_firmware_ota);
  registerUri("/api/audit", HTTP_GET, handle_audit);
  registerUri("/api/docs/index.html", HTTP_GET, handle_docs_index);
  registerUri("/api/docs/openapi.yaml", HTTP_GET, handle_docs_openapi);
  registerUri("/static/*", HTTP_GET, handle_static);
  registerUri("/", HTTP_GET, handle_root);
  registerUri("/index.html", HTTP_GET, handle_index_html);

  startHttpRedirectServer();
}

void web_server_audit_event(const std::string &action, const std::string &detail) { auditLog(action, detail); }

void web_server_audit_tick() {
  if (!g_audit_dirty) return;
  const uint32_t now_ms = millis();
  if (now_ms - g_last_audit_flush_ms < kAuditFlushIntervalMs) return;
  saveAuditEventsToFs();
}