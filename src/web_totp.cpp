#include "web_totp.h"

#include <Arduino.h>
#include <mbedtls/md.h>
#include <sys/time.h>
#include <time.h>

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace {
constexpr int kTotpDigits = 6;
constexpr int kTimeStepSeconds = 30;
/// RFC 6238 recommends a validation window; phones and NTP can drift by a minute or more.
constexpr int kAllowedTimeStepSkew = 3;  // +/- 3 steps (~ +/- 90s)
std::vector<std::vector<uint8_t>> g_totp_secrets;

std::string trimAscii(const std::string &s) {
  size_t a = 0;
  while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  size_t b = s.size();
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

int base32CharValue(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= '2' && c <= '7') return 26 + (c - '2');
  return -1;
}

std::vector<uint8_t> base32Decode(const char *input) {
  std::vector<uint8_t> out;
  int buffer = 0;
  int bits_left = 0;

  for (size_t i = 0; input[i] != '\0'; ++i) {
    char c = static_cast<char>(toupper(static_cast<unsigned char>(input[i])));
    if (c == ' ' || c == '-') continue;
    if (c == '=') break;

    int v = base32CharValue(c);
    if (v < 0) return {};

    buffer = (buffer << 5) | v;
    bits_left += 5;

    while (bits_left >= 8) {
      bits_left -= 8;
      out.push_back(static_cast<uint8_t>((buffer >> bits_left) & 0xFF));
    }
  }

  return out;
}

int computeTotpForCounter(const std::vector<uint8_t> &secret, uint64_t counter) {
  std::array<uint8_t, 8> msg{};
  for (int i = 7; i >= 0; --i) {
    msg[i] = static_cast<uint8_t>(counter & 0xFF);
    counter >>= 8;
  }

  unsigned char hmac[20];
  const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  if (md == nullptr) return -1;

  int rc = mbedtls_md_hmac(
    md,
    secret.data(),
    secret.size(),
    msg.data(),
    msg.size(),
    hmac
  );
  if (rc != 0) return -1;

  int offset = hmac[19] & 0x0F;
  uint32_t bin_code =
      ((hmac[offset] & 0x7F) << 24) |
      ((hmac[offset + 1] & 0xFF) << 16) |
      ((hmac[offset + 2] & 0xFF) << 8) |
      (hmac[offset + 3] & 0xFF);

  uint32_t mod = 1;
  for (int i = 0; i < kTotpDigits; ++i) mod *= 10;
  return static_cast<int>(bin_code % mod);
}

bool totpCodeEqualsSecretAtSteps(const std::vector<uint8_t> &secret, const std::string &digits6,
                                 uint64_t step_center) {
  for (int delta = -kAllowedTimeStepSkew; delta <= kAllowedTimeStepSkew; ++delta) {
    const int64_t step_i = static_cast<int64_t>(step_center) + delta;
    if (step_i < 0) continue;
    const int expected = computeTotpForCounter(secret, static_cast<uint64_t>(step_i));
    if (expected < 0) continue;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%06d", expected);
    if (digits6 == buf) return true;
  }
  return false;
}

bool isNumericCode(const std::string &code) {
  if (code.size() != kTotpDigits) return false;
  for (char c : code) {
    if (!isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

std::string normalizeTotpCode(const std::string &raw) {
  std::string normalized;
  normalized.reserve(raw.size());
  for (char c : raw) {
    if (isdigit(static_cast<unsigned char>(c))) normalized.push_back(c);
  }
  return normalized;
}
}  // namespace

void web_totp_set_secrets(const std::vector<std::string> &secrets) {
  g_totp_secrets.clear();
  for (const auto &secret : secrets) {
    const std::string t = trimAscii(secret);
    std::vector<uint8_t> decoded = base32Decode(t.c_str());
    if (!decoded.empty()) g_totp_secrets.push_back(decoded);
  }
}

bool web_totp_is_time_synced() {
  time_t now = time(nullptr);
  return now > 1700000000;
}

bool web_totp_validate_token(const std::string &token) {
  const std::string normalized = normalizeTotpCode(token);
  if (!isNumericCode(normalized)) return false;
  if (!web_totp_is_time_synced()) return false;
  if (g_totp_secrets.empty()) return false;

  const uint64_t step = static_cast<uint64_t>(time(nullptr) / kTimeStepSeconds);

  for (const auto &secret : g_totp_secrets) {
    if (totpCodeEqualsSecretAtSteps(secret, normalized, step)) return true;
  }

  return false;
}

bool web_totp_validate_token_with_secret(const std::string &token, const std::string &secret_base32) {
  const std::string normalized = normalizeTotpCode(token);
  if (!isNumericCode(normalized)) return false;
  if (!web_totp_is_time_synced()) return false;

  std::vector<uint8_t> secret = base32Decode(trimAscii(secret_base32).c_str());
  if (secret.empty()) return false;

  const uint64_t step = static_cast<uint64_t>(time(nullptr) / kTimeStepSeconds);
  return totpCodeEqualsSecretAtSteps(secret, normalized, step);
}

bool web_totp_sync_time_with_ntp() {
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  for (int i = 0; i < 48; ++i) {
    if (web_totp_is_time_synced()) return true;
    delay(500);
  }
  return web_totp_is_time_synced();
}
