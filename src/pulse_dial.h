#ifndef PULSE_DIAL_H
#define PULSE_DIAL_H

#include <stdint.h>
#include <string>

void pulse_dial_setup(uint8_t pulse_pin);
// Optional: called from the pulse sampling loop so BOOT / factory-reset can stay responsive.
void pulse_dial_set_idle_poll(void (*fn)(void));
bool pulse_dial_read_number(std::string *number, uint32_t timeout_ms, uint32_t inter_digit_gap_ms);

#endif // PULSE_DIAL_H
