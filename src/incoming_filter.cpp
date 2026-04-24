#include "incoming_filter.h"
#include "phone_number.h"

namespace {
bool matchesAnyPrefix(const std::string &number, const std::vector<std::string> &prefixes) {
  for (const auto &prefix : prefixes) {
    if (prefix == "*") return true;
    if (!prefix.empty() && phone_prefix_match(number, prefix)) return true;
  }
  return false;
}
}  // namespace

bool incoming_filter_should_allow(const Config &config, const std::string &number) {
  const bool is_blacklisted = matchesAnyPrefix(number, config.incoming_blacklist);
  if (!is_blacklisted) return true;
  if (config.incoming_whitelist.empty()) return false;
  return matchesAnyPrefix(number, config.incoming_whitelist);
}
