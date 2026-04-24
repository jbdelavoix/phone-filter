#ifndef RELAY_H
#define RELAY_H

#include <stdint.h>

void relay_setup(uint8_t pin);
void relay_allow_call();
void relay_block_call();

#endif // RELAY_H
