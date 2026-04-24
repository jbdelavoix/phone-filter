#ifndef DTMF_TX_H
#define DTMF_TX_H

#include <stdint.h>
#include <string>

void dtmf_tx_setup(uint8_t low_group_pin, uint8_t high_group_pin, uint8_t low_channel, uint8_t high_channel);
bool dtmf_tx_send_digit(char digit, uint32_t tone_ms, uint32_t gap_ms);
bool dtmf_tx_send_number(const std::string &number, uint32_t tone_ms, uint32_t gap_ms);

#endif // DTMF_TX_H
