// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
// The remapped transition table is justified at
// https://docs.google.com/spreadsheets/d/1AZcQwuEL93HmNCljJWUwFMGqf7JAQ0puawZaUgP0E14

#include <stdint.h>

#ifndef __UTF8_DFA_DECODER_H
#define __UTF8_DFA_DECODER_H

struct Utf8DfaDecoder {
  enum State : uint8_t {
    kReject = 0,
    kAccept = 12,
    kTwoByte = 24,
    kThreeByte = 36,
    kThreeByteLowMid = 48,
    kFourByte = 60,
    kFourByteLow = 72,
    kThreeByteHigh = 84,
    kFourByteMidHigh = 96,
  };

  static inline void Decode(uint8_t byte, State* state, uint32_t* buffer) {
    // This first table maps bytes to character to a transition.
    static constexpr uint8_t transitions[] = {
        0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 00-0F
        0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 10-1F
        0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 20-2F
        0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 30-3F
        0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 40-4F
        0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 50-5F
        0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 60-6F
        0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 70-7F
        1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 80-8F
        2,  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  // 90-9F
        3,  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  // A0-AF
        3,  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  // B0-BF
        9,  9, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  // C0-CF
        4,  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  // D0-DF
        10, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 5, 5,  // E0-EF
        11, 7, 7, 7, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,  // F0-FF
    };

    // This second table maps a state to a new state when adding a transition.
    //  00-7F
    //  |   80-8F
    //  |   |   90-9F
    //  |   |   |   A0-BF
    //  |   |   |   |   C2-DF
    //  |   |   |   |   |   E1-EC, EE, EF
    //  |   |   |   |   |   |   ED
    //  |   |   |   |   |   |   |   F1-F3
    //  |   |   |   |   |   |   |   |   F4
    //  |   |   |   |   |   |   |   |   |   C0, C1, F5-FF
    //  |   |   |   |   |   |   |   |   |   |  E0
    //  |   |   |   |   |   |   |   |   |   |  |   F0
    static constexpr uint8_t states[] = {
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0,  0,   // REJECT = 0
        12, 0,  0,  0,  24, 36, 48, 60, 72, 0, 84, 96,  // ACCEPT = 12
        0,  12, 12, 12, 0,  0,  0,  0,  0,  0, 0,  0,   // 2-byte = 24
        0,  24, 24, 24, 0,  0,  0,  0,  0,  0, 0,  0,   // 3-byte = 36
        0,  24, 24, 0,  0,  0,  0,  0,  0,  0, 0,  0,   // 3-byte low/mid = 48
        0,  36, 36, 36, 0,  0,  0,  0,  0,  0, 0,  0,   // 4-byte = 60
        0,  36, 0,  0,  0,  0,  0,  0,  0,  0, 0,  0,   // 4-byte low = 72
        0,  0,  0,  24, 0,  0,  0,  0,  0,  0, 0,  0,   // 3-byte high = 84
        0,  0,  36, 36, 0,  0,  0,  0,  0,  0, 0,  0,   // 4-byte mid/high = 96
    };

    uint8_t type = transitions[byte];
    *state = static_cast<State>(states[*state + type]);
    *buffer = (*buffer << 6) | (byte & (0x7F >> (type >> 1)));
  }
};

#endif /* __UTF8_DFA_DECODER_H */
