// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_STRINGS_STRING_BUILDER_INL_H_
#define V8_STRINGS_STRING_BUILDER_INL_H_

#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/objects/string-inl.h"
#include "src/strings/string-builder.h"

namespace v8 {
namespace internal {

const int kStringBuilderConcatHelperLengthBits = 11;
const int kStringBuilderConcatHelperPositionBits = 19;

using StringBuilderSubstringLength =
    base::BitField<int, 0, kStringBuilderConcatHelperLengthBits>;
using StringBuilderSubstringPosition =
    base::BitField<int, kStringBuilderConcatHelperLengthBits,
                   kStringBuilderConcatHelperPositionBits>;

template <typename sinkchar>
void StringBuilderConcatHelper(Tagged<String> special, sinkchar* sink,
                               Tagged<FixedArray> fixed_array,
                               int array_length);

// Returns the result length of the concatenation.
// On illegal argument, -1 is returned.
int StringBuilderConcatLength(int special_length,
                              Tagged<FixedArray> fixed_array, int array_length,
                              bool* one_byte);

// static
inline void ReplacementStringBuilder::AddSubjectSlice(
    FixedArrayBuilder* builder, int from, int to) {
  DCHECK_GE(from, 0);
  int length = to - from;
  DCHECK_GT(length, 0);
  if (StringBuilderSubstringLength::is_valid(length) &&
      StringBuilderSubstringPosition::is_valid(from)) {
    int encoded_slice = StringBuilderSubstringLength::encode(length) |
                        StringBuilderSubstringPosition::encode(from);
    builder->Add(Smi::FromInt(encoded_slice));
  } else {
    // Otherwise encode as two smis.
    builder->Add(Smi::FromInt(-length));
    builder->Add(Smi::FromInt(from));
  }
}

inline void ReplacementStringBuilder::AddSubjectSlice(int from, int to) {
  EnsureCapacity(2);  // Subject slices are encoded with up to two smis.
  AddSubjectSlice(&array_builder_, from, to);
  IncrementCharacterCount(to - from);
}

template <typename SrcChar, typename DestChar>
void IncrementalStringBuilder::Append(SrcChar c) {
  DCHECK_EQ(encoding_ == String::ONE_BYTE_ENCODING, sizeof(DestChar) == 1);
  if (sizeof(DestChar) == 1) {
    DCHECK_EQ(String::ONE_BYTE_ENCODING, encoding_);
    Cast<SeqOneByteString>(*current_part_)
        ->SeqOneByteStringSet(current_index_++, c);
  } else {
    DCHECK_EQ(String::TWO_BYTE_ENCODING, encoding_);
    Cast<SeqTwoByteString>(*current_part_)
        ->SeqTwoByteStringSet(current_index_++, c);
  }
  if (current_index_ == part_length_) Extend();
  DCHECK(HasValidCurrentIndex());
}

V8_INLINE void IncrementalStringBuilder::AppendCharacter(uint8_t c) {
  if (encoding_ == String::ONE_BYTE_ENCODING) {
    Append<uint8_t, uint8_t>(c);
  } else {
    Append<uint8_t, base::uc16>(c);
  }
}

template <int N>
V8_INLINE void IncrementalStringBuilder::AppendCStringLiteral(
    const char (&literal)[N]) {
  // Note that the literal contains the zero char.
  const int length = N - 1;
  static_assert(length > 0);
  if (length == 1) return AppendCharacter(literal[0]);
  if (encoding_ == String::ONE_BYTE_ENCODING && CurrentPartCanFit(N)) {
    const uint8_t* chars = reinterpret_cast<const uint8_t*>(literal);
    Cast<SeqOneByteString>(*current_part_)
        ->SeqOneByteStringSetChars(current_index_, chars, length);
    current_index_ += length;
    if (current_index_ == part_length_) Extend();
    DCHECK(HasValidCurrentIndex());
    return;
  }
  return AppendCString(literal);
}

template <typename SrcChar>
V8_INLINE void IncrementalStringBuilder::AppendCString(const SrcChar* s) {
  if (encoding_ == String::ONE_BYTE_ENCODING) {
    while (*s != '\0') Append<SrcChar, uint8_t>(*s++);
  } else {
    while (*s != '\0') Append<SrcChar, base::uc16>(*s++);
  }
}

V8_INLINE void IncrementalStringBuilder::AppendInt(int i) {
  char buffer[kIntToCStringBufferSize];
  const char* str =
      IntToCString(i, base::Vector<char>(buffer, kIntToCStringBufferSize));
  AppendCString(str);
}

V8_INLINE int IncrementalStringBuilder::EscapedLengthIfCurrentPartFits(
    int length) {
  if (length > kMaxPartLength) return 0;
  // The worst case length of an escaped character is 6. Shifting the remaining
  // string length right by 3 is a more pessimistic estimate, but faster to
  // calculate.
  static_assert((kMaxPartLength << 3) <= String::kMaxLength);
  // This shift will not overflow because length is already less than the
  // maximum part length.
  int worst_case_length = length << 3;
  return CurrentPartCanFit(worst_case_length) ? worst_case_length : 0;
}

// Change encoding to two-byte.
void IncrementalStringBuilder::ChangeEncoding() {
  DCHECK_EQ(String::ONE_BYTE_ENCODING, encoding_);
  ShrinkCurrentPart();
  encoding_ = String::TWO_BYTE_ENCODING;
  Extend();
}

template <typename DestChar>
inline IncrementalStringBuilder::NoExtend<DestChar>::NoExtend(
    Tagged<String> string, int offset, const DisallowGarbageCollection& no_gc) {
  DCHECK(IsSeqOneByteString(string) || IsSeqTwoByteString(string));
  if (sizeof(DestChar) == 1) {
    start_ = reinterpret_cast<DestChar*>(
        Cast<SeqOneByteString>(string)->GetChars(no_gc) + offset);
  } else {
    start_ = reinterpret_cast<DestChar*>(
        Cast<SeqTwoByteString>(string)->GetChars(no_gc) + offset);
  }
  cursor_ = start_;
#ifdef DEBUG
  string_ = string;
#endif
}

#ifdef DEBUG
template <typename DestChar>
inline IncrementalStringBuilder::NoExtend<DestChar>::~NoExtend() {
  DestChar* end;
  if (sizeof(DestChar) == 1) {
    auto one_byte_string = Cast<SeqOneByteString>(string_);
    end = reinterpret_cast<DestChar*>(one_byte_string->GetChars(no_gc_) +
                                      one_byte_string->length());
  } else {
    auto two_byte_string = Cast<SeqTwoByteString>(string_);
    end = reinterpret_cast<DestChar*>(two_byte_string->GetChars(no_gc_) +
                                      two_byte_string->length());
  }
  DCHECK_LE(cursor_, end + 1);
}
#endif

template <typename DestChar>
inline IncrementalStringBuilder::NoExtendBuilder<DestChar>::NoExtendBuilder(
    IncrementalStringBuilder* builder, int required_length,
    const DisallowGarbageCollection& no_gc)
    : NoExtend<DestChar>(*(builder->current_part()), builder->current_index_,
                         no_gc),
      builder_(builder) {
  DCHECK(builder->CurrentPartCanFit(required_length));
}

V8_INLINE Factory* IncrementalStringBuilder::factory() {
  return isolate_->factory();
}

V8_INLINE void IncrementalStringBuilder::ShrinkCurrentPart() {
  DCHECK(current_index_ < part_length_);
  set_current_part(SeqString::Truncate(
      isolate_, indirect_handle(Cast<SeqString>(current_part()), isolate_),
      current_index_));
}

}  // namespace internal
}  // namespace v8

#endif  // V8_STRINGS_STRING_BUILDER_INL_H_
