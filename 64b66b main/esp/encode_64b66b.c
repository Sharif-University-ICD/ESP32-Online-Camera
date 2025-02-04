#include "encode_64b66b.h"

void encode_64b66b(uint64_t data, uint8_t encoded[9], uint64_t* scrambler_state) {
    uint64_t scrambled_data = 0;
    
    // Process all 64 bits
    for (int i = 63; i >= 0; i--) {
        uint8_t input_bit = (data >> i) & 1;
        uint8_t feedback = ((*scrambler_state >> 38) & 1) ^ ((*scrambler_state >> 57) & 1);
        uint8_t scrambled_bit = input_bit ^ feedback;
        
        // Update scrambler state
        *scrambler_state = (*scrambler_state << 1) | scrambled_bit;
        *scrambler_state &= (1ULL << 58) - 1;
        
        // Build scrambled data
        scrambled_data = (scrambled_data << 1) | scrambled_bit;
    }

    // Pack into 66-bit format (2-bit sync header + 64-bit payload)
    encoded[0] = 0x40 | ((scrambled_data >> 58) & 0x3F);  // Sync header
    encoded[1] = (scrambled_data >> 50) & 0xFF;
    encoded[2] = (scrambled_data >> 42) & 0xFF;
    encoded[3] = (scrambled_data >> 34) & 0xFF;
    encoded[4] = (scrambled_data >> 26) & 0xFF;
    encoded[5] = (scrambled_data >> 18) & 0xFF;
    encoded[6] = (scrambled_data >> 10) & 0xFF;
    encoded[7] = (scrambled_data >> 2)  & 0xFF;
    encoded[8] = (scrambled_data & 0x03) << 6;
}