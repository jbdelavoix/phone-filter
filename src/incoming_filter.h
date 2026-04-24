#ifndef INCOMING_FILTER_H
#define INCOMING_FILTER_H

#include <string>

#include "config.h"

bool incoming_filter_should_allow(const Config &config, const std::string &number);

#endif // INCOMING_FILTER_H
