#ifndef PHONE_NUMBER_H
#define PHONE_NUMBER_H

#include <string>
#include <vector>

std::vector<std::string> phone_number_variants(const std::string &raw);
bool phone_prefix_match(const std::string &number, const std::string &prefix);

#endif  // PHONE_NUMBER_H
