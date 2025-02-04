#ifndef ENCODE_64B66B_H
#define ENCODE_64B66B_H

#include <stdint.h>

void encode_64b66b(uint64_t data, uint8_t encoded[9], uint64_t* scrambler_state);

#endif