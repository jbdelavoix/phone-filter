#ifndef OUTGOING_FILTER_H
#define OUTGOING_FILTER_H

#include <string>

#include "config.h"

std::string outgoing_filter_apply_shortcut(const Config &config, const std::string &dialed_number);
bool outgoing_filter_should_allow(const Config &config, const std::string &number);

#endif // OUTGOING_FILTER_H
