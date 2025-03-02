// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ and the sibling file
// utf8-decoder.h for details.
//
// This file decodes "generalized UTF-8", which is the same as UTF-8 except that
// it allows surrogates: https://simonsapin.github.io/wtf-8/#generalized-utf8

#include <stdint.h>

#ifndef __GENERALIZED_UTF8_DFA_DECODER_H
#define __GENERALIZED_UTF8_DFA_DECODER_H

struct GeneralizedUtf8DfaDecoder {
  enum State : uint8_t {
    kReject = 0,
    kAccept = 11,
    kTwoByte = 22,
    kThreeByte = 33,
    kFourByte = 44,
    kFourByteLow = 55,
    kThreeByteHigh = 66,
    kFourByteMidHigh = 77,
  };

  static inline void Decode(uint8_t byte, State* state, uint32_t* buffer) {
    // This first table maps bytes to character to a transition.
    //
    // The transition value takes a state to a new state, but it also determines
    // the set of bits from the current byte that contribute to the decoded
    // codepoint:
    //
    //   Transition | Current byte bits that contribute to decoded codepoint
    //   -------------------------------------------------------------------
    //    0, 1      | 0b01111111
    //    2, 3      | 0b00111111
    //    4, 5      | 0b00011111
    //    6, 7      | 0b00001111
    //    8, 9      | 0b00000111
    //    10        | 0b00000011
    //
    // Given the WTF-8 encoding, we therefore have the following constraints:

    //   1. The transition value for 1-byte encodings should have the value 0 or
    //      1 so that we preserve all of the low 7 bits.
    //   2. Continuation bytes (0x80 to 0xBF) are of the form 0b10xxxxxx, and
    //      therefore should have transition value between 0 and 3.
    //   3. Leading bytes for 2-byte encodings are of the form 0b110yyyyy, and
    //      therefore the transition value can be between 2 and 5.
    //   4. Leading bytes for 3-byte encodings (0b1110zzzz) need transition
    //      value between 4 and 7.
    //   5. Leading bytes for 4-byte encodings (0b11110uuu) need transition
    //      value between 6 and 9.
    //   6. We need more states to impose irregular constraints.  Sometimes we
    //      can use the knowldege that e.g. some high significant bits of the
    //      xxxx in 0b1110xxxx are 0, then we can use a higher transition value.
    //   7. Transitions to invalid states can use any transition value.
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
        8,  8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  // C0-CF
        4,  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  // D0-DF
        9,  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,  // E0-EF
        10, 6, 6, 6, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,  // F0-FF
    };

    // This second table maps a state to a new state when adding a transition.
    //  00-7F
    //  |   80-8F
    //  |   |   90-9F
    //  |   |   |   A0-BF
    //  |   |   |   |   C2-DF
    //  |   |   |   |   |   E1-EF
    //  |   |   |   |   |   |   F1-F3
    //  |   |   |   |   |   |   |   F4
    //  |   |   |   |   |   |   |   |   C0, C1, F5-FF
    //  |   |   |   |   |   |   |   |   |  E0
    //  |   |   |   |   |   |   |   |   |  |   F0
    static constexpr uint8_t states[] = {
        0,  0,  0,  0,  0,  0,  0,  0,  0, 0,  0,   // REJECT = 0
        11, 0,  0,  0,  22, 33, 44, 55, 0, 66, 77,  // ACCEPT = 11
        0,  11, 11, 11, 0,  0,  0,  0,  0, 0,  0,   // 2-byte = 22
        0,  22, 22, 22, 0,  0,  0,  0,  0, 0,  0,   // 3-byte = 33
        0,  33, 33, 33, 0,  0,  0,  0,  0, 0,  0,   // 4-byte = 44
        0,  33, 0,  0,  0,  0,  0,  0,  0, 0,  0,   // 4-byte low = 55
        0,  0,  0,  22, 0,  0,  0,  0,  0, 0,  0,   // 3-byte high = 66
        0,  0,  33, 33, 0,  0,  0,  0,  0, 0,  0,   // 4-byte mid/high = 77
    };

    uint8_t type = transitions[byte];
    *state = static_cast<State>(states[*state + type]);
    *buffer = (*buffer << 6) | (byte & (0x7F >> (type >> 1)));
  }
};

#endif  // __GENERALIZED_UTF8_DFA_DECODER_H
