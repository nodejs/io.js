// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/strings/unicode.h"
#include "test/cctest/cctest.h"
#include "third_party/utf8-decoder/generalized-utf8-decoder.h"
#include "third_party/utf8-decoder/utf8-decoder.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_wasm_strings {

struct Utf8Decoder {
  Utf8DfaDecoder::State state = Utf8DfaDecoder::kAccept;
  uint32_t codepoint = 0;
  void Decode(uint8_t byte) {
    DCHECK(!failure());
    Utf8DfaDecoder::Decode(byte, &state, &codepoint);
  }
  bool success() const { return state == Utf8DfaDecoder::kAccept; }
  bool failure() const { return state == Utf8DfaDecoder::kReject; }
  bool incomplete() const { return !success() && !failure(); }
};

struct GeneralizedUtf8Decoder {
  GeneralizedUtf8DfaDecoder::State state = GeneralizedUtf8DfaDecoder::kAccept;
  uint32_t codepoint = 0;
  void Decode(uint8_t byte) {
    DCHECK(!failure());
    GeneralizedUtf8DfaDecoder::Decode(byte, &state, &codepoint);
  }
  bool success() const { return state == GeneralizedUtf8DfaDecoder::kAccept; }
  bool failure() const { return state == GeneralizedUtf8DfaDecoder::kReject; }
  bool incomplete() const { return !success() && !failure(); }
};

struct DecodingOracle {
  Utf8Decoder utf8;
  GeneralizedUtf8Decoder generalized_utf8;

  void Decode(uint8_t byte) {
    utf8.Decode(byte);
    generalized_utf8.Decode(byte);
  }

  void CheckSame() const {
    CHECK_EQ(utf8.success(), generalized_utf8.success());
    CHECK_EQ(utf8.failure(), generalized_utf8.failure());
    if (utf8.success()) CHECK(utf8.codepoint == generalized_utf8.codepoint);
  }

  bool success() const {
    CheckSame();
    return utf8.success();
  }
  bool failure() const {
    CheckSame();
    return utf8.failure();
  }
  bool incomplete() const {
    CheckSame();
    return utf8.incomplete();
  }
};

TEST(GeneralizedUTF8Decode) {
  // Exhaustive check that the generalized UTF-8 decoder matches the strict
  // UTF-8 encoder, except for surrogates.  Each production should end the
  // decoders accepting or rejecting the production.
  for (uint32_t byte1 = 0; byte1 <= 0xFF; byte1++) {
    DecodingOracle decoder1;
    decoder1.Decode(byte1);

    if (byte1 <= 0x7F) {
      // First byte in [0x00, 0x7F]: one-byte.
      CHECK(decoder1.success());
    } else if (byte1 <= 0xC1) {
      // First byte in [0x80, 0xC1]: invalid.
      CHECK(decoder1.failure());
    } else if (byte1 <= 0xDF) {
      // First byte in [0xC2, 0xDF]: two-byte.
      CHECK(decoder1.incomplete());
      // Second byte completes the sequence.  Only [0x80, 0xBF] is valid.
      for (uint32_t byte2 = 0x00; byte2 <= 0xFF; byte2++) {
        DecodingOracle decoder2 = decoder1;
        decoder2.Decode(byte2);
        if (0x80 <= byte2 && byte2 <= 0xBF) {
          CHECK(decoder2.success());
        } else {
          CHECK(decoder2.failure());
        }
      }
    } else if (byte1 <= 0xEF) {
      // First byte in [0xE0, 0xEF]: three-byte sequence.
      CHECK(decoder1.incomplete());
      uint32_t min = byte1 == 0xE0 ? 0xA0 : 0x80;
      for (uint32_t byte2 = 0x00; byte2 <= 0xFF; byte2++) {
        DecodingOracle decoder2 = decoder1;
        decoder2.Decode(byte2);
        if (min <= byte2 && byte2 <= 0xBF) {
          // Second byte in [min, 0xBF]: continuation.
          bool is_surrogate = byte1 == 0xED && byte2 >= 0xA0;
          if (is_surrogate) {
            // Here's where we expect the two decoders to differ: generalized
            // UTF-8 will get a surrogate and strict UTF-8 errors.
            CHECK(decoder2.utf8.failure());
            CHECK(decoder2.generalized_utf8.incomplete());
          } else {
            CHECK(decoder2.incomplete());
          }

          // Third byte completes the sequence.  Only [0x80, 0xBF] is valid.
          for (uint32_t byte3 = 0x00; byte3 <= 0xFF; byte3++) {
            DecodingOracle decoder3 = decoder2;
            if (is_surrogate) {
              decoder3.generalized_utf8.Decode(byte3);
              if (0x80 <= byte3 && byte3 <= 0xBF) {
                CHECK(decoder3.generalized_utf8.success());
                uint32_t codepoint = decoder3.generalized_utf8.codepoint;
                CHECK(unibrow::Utf16::IsLeadSurrogate(codepoint) ||
                      unibrow::Utf16::IsTrailSurrogate(codepoint));
              } else {
                CHECK(decoder3.generalized_utf8.failure());
              }
            } else {
              decoder3.Decode(byte3);
              if (0x80 <= byte3 && byte3 <= 0xBF) {
                CHECK(decoder3.success());
              } else {
                CHECK(decoder3.failure());
              }
            }
          }
        } else {
          // Second byte not in range: failure.
          CHECK(decoder2.failure());
        }
      }
    } else if (byte1 <= 0xF4) {
      // First byte in [0xF0, 0xF4]: four-byte sequence.
      CHECK(decoder1.incomplete());
      uint32_t min = byte1 == 0xF0 ? 0x90 : 0x80;
      uint32_t max = byte1 == 0xF4 ? 0x8F : 0xBF;
      for (uint32_t byte2 = 0x00; byte2 <= 0xFF; byte2++) {
        DecodingOracle decoder2 = decoder1;
        decoder2.Decode(byte2);
        if (min <= byte2 && byte2 <= max) {
          // Second byte in [min, max]: continuation.
          CHECK(decoder2.incomplete());
          for (uint32_t byte3 = 0x00; byte3 <= 0xFF; byte3++) {
            DecodingOracle decoder3 = decoder2;
            decoder3.Decode(byte3);
            if (0x80 <= byte3 && byte3 <= 0xBF) {
              // Third byte in [0x80, BF]: continuation.
              CHECK(decoder3.incomplete());
              for (uint32_t byte4 = 0x00; byte4 <= 0xFF; byte4++) {
                DecodingOracle decoder4 = decoder3;
                decoder4.Decode(byte4);
                // Fourth byte4 completes the sequence.
                if (0x80 <= byte4 && byte4 <= 0xBF) {
                  CHECK(decoder4.success());
                } else {
                  CHECK(decoder4.failure());
                }
              }
            } else {
              CHECK(decoder3.failure());
            }
          }
        } else {
          CHECK(decoder2.failure());
        }
      }
    } else {
      // First byte in [0xF5, 0xFF]: failure.
      CHECK(decoder1.failure());
    }
  }
}

}  // namespace test_wasm_strings
}  // namespace wasm
}  // namespace internal
}  // namespace v8
