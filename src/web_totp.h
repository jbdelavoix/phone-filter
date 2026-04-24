#ifndef WEB_TOTP_H
#define WEB_TOTP_H

#include <string>
#include <vector>

bool web_totp_validate_token(const std::string &token);
bool web_totp_validate_token_with_secret(const std::string &token, const std::string &secret_base32);
bool web_totp_is_time_synced();
bool web_totp_sync_time_with_ntp();
void web_totp_set_secrets(const std::vector<std::string> &secrets);

#endif  // WEB_TOTP_H
