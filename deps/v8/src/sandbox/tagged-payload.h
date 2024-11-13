// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_TAGGED_PAYLOAD_H_
#define V8_SANDBOX_TAGGED_PAYLOAD_H_

#include "src/common/globals.h"
#include "src/sandbox/external-buffer-tag.h"
#include "src/sandbox/indirect-pointer-tag.h"

namespace v8 {
namespace internal {

// Struct providing common utilities for pointer tagging.
template <typename PayloadTaggingScheme>
struct TaggedPayload {
  static_assert(PayloadTaggingScheme::kMarkBit != 0,
                "Invalid kMarkBit specified in tagging scheme.");

  TaggedPayload(Address pointer, typename PayloadTaggingScheme::TagType tag)
      : encoded_word_(Tag(pointer, tag)) {}

  Address Untag(typename PayloadTaggingScheme::TagType tag) const {
    return encoded_word_ & ~(tag | PayloadTaggingScheme::kMarkBit);
  }

  static Address Tag(Address pointer,
                     typename PayloadTaggingScheme::TagType tag) {
    return pointer | tag;
  }

  bool IsTaggedWith(typename PayloadTaggingScheme::TagType tag) const {
    // We have to explicitly ignore the marking bit (which is part of the
    // tag) since an unmarked entry with tag kXyzTag is still considered to
    // be tagged with kXyzTag.
    uint64_t expected = tag & ~PayloadTaggingScheme::kMarkBit;
    uint64_t actual = (encoded_word_ & PayloadTaggingScheme::kTagMask) &
                      ~PayloadTaggingScheme::kMarkBit;
    return expected == actual;
  }

  void SetMarkBit() { encoded_word_ |= PayloadTaggingScheme::kMarkBit; }

  void ClearMarkBit() { encoded_word_ &= ~PayloadTaggingScheme::kMarkBit; }

  bool HasMarkBitSet() const {
    return (encoded_word_ & PayloadTaggingScheme::kMarkBit) != 0;
  }

  uint32_t ExtractFreelistLink() const {
    return static_cast<uint32_t>(encoded_word_);
  }

  typename PayloadTaggingScheme::TagType ExtractTag() const {
    return static_cast<typename PayloadTaggingScheme::TagType>(
        (encoded_word_ & PayloadTaggingScheme::kTagMask) |
        PayloadTaggingScheme::kMarkBit);
  }

  bool ContainsFreelistLink() const {
      return IsTaggedWith(PayloadTaggingScheme::kFreeEntryTag);
  }

  bool ContainsEvacuationEntry() const {
    if constexpr (PayloadTaggingScheme::kSupportsEvacuation) {
      return IsTaggedWith(PayloadTaggingScheme::kEvacuationEntryTag);
    } else {
      return false;
    }
  }

  Address ExtractEvacuationEntryHandleLocation() const {
    if constexpr (PayloadTaggingScheme::kSupportsEvacuation) {
      return Untag(PayloadTaggingScheme::kEvacuationEntryTag);
    } else {
      UNREACHABLE();
    }
  }

  bool ContainsPointer() const {
    return !ContainsFreelistLink() && !ContainsEvacuationEntry();
  }

  bool operator==(TaggedPayload other) const {
    return encoded_word_ == other.encoded_word_;
  }

  bool operator!=(TaggedPayload other) const {
    return encoded_word_ != other.encoded_word_;
  }

 private:
  Address encoded_word_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_TAGGED_PAYLOAD_H_
