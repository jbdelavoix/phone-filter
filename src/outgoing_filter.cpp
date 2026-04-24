#include "outgoing_filter.h"
#include "phone_number.h"

namespace {
bool startsWith(const std::string &value, const std::string &prefix) {
  return value.rfind(prefix, 0) == 0;
}

bool matchesAnyPrefix(const std::string &number, const std::vector<std::string> &prefixes) {
  for (const auto &prefix : prefixes) {
    if (prefix == "*") return true;
    if (!prefix.empty() && phone_prefix_match(number, prefix)) return true;
  }
  return false;
}
}  // namespace

std::string outgoing_filter_apply_shortcut(const Config &config, const std::string &dialed_number) {
  for (const auto &rule : config.outgoing_shortcuts) {
    if (rule.trigger.empty() || rule.replacement.empty()) continue;
    if (!startsWith(dialed_number, rule.trigger)) continue;
    return rule.replacement + dialed_number.substr(rule.trigger.size());
  }
  return dialed_number;
}

bool outgoing_filter_should_allow(const Config &config, const std::string &number) {
  const bool is_blacklisted = matchesAnyPrefix(number, config.outgoing_blacklist);
  if (is_blacklisted) {
    if (config.outgoing_whitelist.empty()) return false;
    return matchesAnyPrefix(number, config.outgoing_whitelist);
  }

  if (config.outgoing_whitelist.empty()) return true;
  if (matchesAnyPrefix(number, config.outgoing_whitelist)) return true;
  return false;
}
