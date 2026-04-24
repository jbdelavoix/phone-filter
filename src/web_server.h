#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "config.h"
#include <string>

void web_server_setup(Config *config);
void web_server_audit_event(const std::string &action, const std::string &detail = "");
void web_server_audit_tick();

#endif // WEB_SERVER_H