// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/strings/unicode-decoder.h"

#include "src/strings/unicode-inl.h"
#include "src/utils/memcopy.h"

#if V8_ENABLE_WEBASSEMBLY
#include "third_party/utf8-decoder/generalized-utf8-decoder.h"
#endif

namespace v8 {
namespace internal {

namespace {
template <class Decoder>
struct DecoderTraits;

template <>
struct DecoderTraits<Utf8Decoder> {
  static bool IsInvalidSurrogatePair(uint32_t lead, uint32_t trail) {
    // The DfaDecoder will only ever decode Unicode scalar values, and all
    // sequences of USVs are valid.
    DCHECK(!unibrow::Utf16::IsLeadSurrogate(trail));
    DCHECK(!unibrow::Utf16::IsTrailSurrogate(trail));
    return false;
  }
  static const bool kAllowIncompleteSequences = true;
  using DfaDecoder = Utf8DfaDecoder;
};

#if V8_ENABLE_WEBASSEMBLY
template <>
struct DecoderTraits<Wtf8Decoder> {
  static bool IsInvalidSurrogatePair(uint32_t lead, uint32_t trail) {
    return unibrow::Utf16::IsSurrogatePair(lead, trail);
  }
  static const bool kAllowIncompleteSequences = false;
  using DfaDecoder = GeneralizedUtf8DfaDecoder;
};

template <>
struct DecoderTraits<StrictUtf8Decoder> {
  static bool IsInvalidSurrogatePair(uint32_t lead, uint32_t trail) {
    // The DfaDecoder will only ever decode Unicode scalar values, and all
    // sequences of USVs are valid.
    DCHECK(!unibrow::Utf16::IsLeadSurrogate(trail));
    DCHECK(!unibrow::Utf16::IsTrailSurrogate(trail));
    return false;
  }
  static const bool kAllowIncompleteSequences = false;
  using DfaDecoder = Utf8DfaDecoder;
};
#endif  // V8_ENABLE_WEBASSEMBLY
}  // namespace

template <class Decoder>
Utf8DecoderBase<Decoder>::Utf8DecoderBase(base::Vector<const uint8_t> data)
    : encoding_(Encoding::kAscii),
      non_ascii_start_(NonAsciiStart(data.begin(), data.length())),
      utf16_length_(non_ascii_start_) {
  using Traits = DecoderTraits<Decoder>;
  if (non_ascii_start_ == data.length()) return;

  bool is_one_byte = true;
  auto state = Traits::DfaDecoder::kAccept;
  uint32_t current = 0;
  uint32_t previous = 0;
  const uint8_t* cursor = data.begin() + non_ascii_start_;
  const uint8_t* end = data.begin() + data.length();

  while (cursor < end) {
    if (V8_LIKELY(*cursor <= unibrow::Utf8::kMaxOneByteChar &&
                  state == Traits::DfaDecoder::kAccept)) {
      DCHECK_EQ(0u, current);
      DCHECK(!Traits::IsInvalidSurrogatePair(previous, *cursor));
      previous = *cursor;
      utf16_length_++;
      cursor++;
      continue;
    }

    auto previous_state = state;
    Traits::DfaDecoder::Decode(*cursor, &state, &current);
    if (state < Traits::DfaDecoder::kAccept) {
      DCHECK_EQ(state, Traits::DfaDecoder::kReject);
      if (Traits::kAllowIncompleteSequences) {
        state = Traits::DfaDecoder::kAccept;
        static_assert(unibrow::Utf8::kBadChar > unibrow::Latin1::kMaxChar);
        is_one_byte = false;
        utf16_length_++;
        previous = unibrow::Utf8::kBadChar;
        current = 0;
        // If we were trying to continue a multibyte sequence, try this byte
        // again.
        if (previous_state != Traits::DfaDecoder::kAccept) continue;
      } else {
        encoding_ = Encoding::kInvalid;
        return;
      }
    } else if (state == Traits::DfaDecoder::kAccept) {
      if (Traits::IsInvalidSurrogatePair(previous, current)) {
        encoding_ = Encoding::kInvalid;
        return;
      }
      is_one_byte = is_one_byte && current <= unibrow::Latin1::kMaxChar;
      utf16_length_++;
      if (current > unibrow::Utf16::kMaxNonSurrogateCharCode) utf16_length_++;
      previous = current;
      current = 0;
    }
    cursor++;
  }

  if (state == Traits::DfaDecoder::kAccept) {
    encoding_ = is_one_byte ? Encoding::kLatin1 : Encoding::kUtf16;
  } else if (Traits::kAllowIncompleteSequences) {
    static_assert(unibrow::Utf8::kBadChar > unibrow::Latin1::kMaxChar);
    encoding_ = Encoding::kUtf16;
    utf16_length_++;
  } else {
    encoding_ = Encoding::kInvalid;
  }
}

template <class Decoder>
template <typename Char>
void Utf8DecoderBase<Decoder>::Decode(Char* out,
                                      base::Vector<const uint8_t> data) {
  using Traits = DecoderTraits<Decoder>;
  DCHECK(!is_invalid());
  CopyChars(out, data.begin(), non_ascii_start_);

  out += non_ascii_start_;

  auto state = Traits::DfaDecoder::kAccept;
  uint32_t current = 0;
  const uint8_t* cursor = data.begin() + non_ascii_start_;
  const uint8_t* end = data.begin() + data.length();

  while (cursor < end) {
    if (V8_LIKELY(*cursor <= unibrow::Utf8::kMaxOneByteChar &&
                  state == Traits::DfaDecoder::kAccept)) {
      DCHECK_EQ(0u, current);
      *(out++) = static_cast<Char>(*cursor);
      cursor++;
      continue;
    }

    auto previous_state = state;
    Traits::DfaDecoder::Decode(*cursor, &state, &current);
    if (Traits::kAllowIncompleteSequences &&
        state < Traits::DfaDecoder::kAccept) {
      state = Traits::DfaDecoder::kAccept;
      *(out++) = static_cast<Char>(unibrow::Utf8::kBadChar);
      current = 0;
      // If we were trying to continue a multibyte sequence, try this byte
      // again.
      if (previous_state != Traits::DfaDecoder::kAccept) continue;
    } else if (state == Traits::DfaDecoder::kAccept) {
      if (sizeof(Char) == 1 ||
          current <= unibrow::Utf16::kMaxNonSurrogateCharCode) {
        *(out++) = static_cast<Char>(current);
      } else {
        *(out++) = unibrow::Utf16::LeadSurrogate(current);
        *(out++) = unibrow::Utf16::TrailSurrogate(current);
      }
      current = 0;
    }
    cursor++;
  }

  if (Traits::kAllowIncompleteSequences &&
      state != Traits::DfaDecoder::kAccept) {
    *out = static_cast<Char>(unibrow::Utf8::kBadChar);
  } else {
    DCHECK_EQ(state, Traits::DfaDecoder::kAccept);
  }
}

#define DEFINE_UNICODE_DECODER(Decoder)                                 \
  template V8_EXPORT_PRIVATE Utf8DecoderBase<Decoder>::Utf8DecoderBase( \
      base::Vector<const uint8_t> data);                                \
  template V8_EXPORT_PRIVATE void Utf8DecoderBase<Decoder>::Decode(     \
      uint8_t* out, base::Vector<const uint8_t> data);                  \
  template V8_EXPORT_PRIVATE void Utf8DecoderBase<Decoder>::Decode(     \
      uint16_t* out, base::Vector<const uint8_t> data)

DEFINE_UNICODE_DECODER(Utf8Decoder);

#if V8_ENABLE_WEBASSEMBLY
DEFINE_UNICODE_DECODER(Wtf8Decoder);
DEFINE_UNICODE_DECODER(StrictUtf8Decoder);
#endif  // V8_ENABLE_WEBASSEMBLY

#undef DEFINE_UNICODE_DECODER

}  // namespace internal
}  // namespace v8
