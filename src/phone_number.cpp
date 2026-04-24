#include "phone_number.h"

#include <cctype>

namespace {
std::string compact_phone(const std::string &raw) {
  std::string out;
  out.reserve(raw.size());
  for (char c : raw) {
    if (std::isdigit(static_cast<unsigned char>(c))) {
      out.push_back(c);
      continue;
    }
    if (c == '+' && out.empty()) out.push_back(c);
  }
  return out;
}

void append_unique(std::vector<std::string> *values, const std::string &value) {
  if (values == nullptr || value.empty()) return;
  for (const auto &v : *values) {
    if (v == value) return;
  }
  values->push_back(value);
}
}  // namespace

std::vector<std::string> phone_number_variants(const std::string &raw) {
  std::vector<std::string> variants;
  const std::string compact = compact_phone(raw);
  if (compact.empty()) return variants;

  append_unique(&variants, compact);

  // Generic international prefix normalization:
  // +XXXXXXXX <=> 00XXXXXXXX <=> XXXXXXXXX
  if (compact.rfind("+", 0) == 0 && compact.size() > 1) {
    const std::string digits = compact.substr(1);
    append_unique(&variants, "00" + digits);
    append_unique(&variants, digits);
  } else if (compact.rfind("00", 0) == 0 && compact.size() > 2) {
    const std::string digits = compact.substr(2);
    append_unique(&variants, "+" + digits);
    append_unique(&variants, digits);
  } else {
    append_unique(&variants, "+" + compact);
    append_unique(&variants, "00" + compact);
  }

  return variants;
}

bool phone_prefix_match(const std::string &number, const std::string &prefix) {
  const auto number_variants = phone_number_variants(number);
  const auto prefix_variants = phone_number_variants(prefix);

  for (const auto &n : number_variants) {
    for (const auto &p : prefix_variants) {
      if (!p.empty() && n.rfind(p, 0) == 0) return true;
    }
  }
  return false;
}
