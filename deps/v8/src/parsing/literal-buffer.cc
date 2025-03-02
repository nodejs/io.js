// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/literal-buffer.h"

#include "src/base/strings.h"
#include "src/execution/isolate.h"
#include "src/execution/local-isolate.h"
#include "src/heap/factory.h"
#include "src/utils/memcopy.h"

namespace v8 {
namespace internal {

template <typename IsolateT>
DirectHandle<String> LiteralBuffer::Internalize(IsolateT* isolate) const {
  if (is_one_byte()) {
    return isolate->factory()->InternalizeString(one_byte_literal());
  }
  return isolate->factory()->InternalizeString(two_byte_literal());
}

template DirectHandle<String> LiteralBuffer::Internalize(
    Isolate* isolate) const;
template DirectHandle<String> LiteralBuffer::Internalize(
    LocalIsolate* isolate) const;

int LiteralBuffer::NewCapacity(int min_capacity) {
  return min_capacity < (kMaxGrowth / (kGrowthFactor - 1))
             ? min_capacity * kGrowthFactor
             : min_capacity + kMaxGrowth;
}

void LiteralBuffer::ExpandBuffer() {
  int min_capacity = std::max({kInitialCapacity, backing_store_.length()});
  base::Vector<uint8_t> new_store =
      base::Vector<uint8_t>::New(NewCapacity(min_capacity));
  if (position_ > 0) {
    MemCopy(new_store.begin(), backing_store_.begin(), position_);
  }
  backing_store_.Dispose();
  backing_store_ = new_store;
}

void LiteralBuffer::ConvertToTwoByte() {
  DCHECK(is_one_byte());
  base::Vector<uint8_t> new_store;
  int new_content_size = position_ * base::kUC16Size;
  if (new_content_size >= backing_store_.length()) {
    // Ensure room for all currently read code units as UC16 as well
    // as the code unit about to be stored.
    new_store = base::Vector<uint8_t>::New(NewCapacity(new_content_size));
  } else {
    new_store = backing_store_;
  }
  uint8_t* src = backing_store_.begin();
  uint16_t* dst = reinterpret_cast<uint16_t*>(new_store.begin());
  for (int i = position_ - 1; i >= 0; i--) {
    dst[i] = src[i];
  }
  if (new_store.begin() != backing_store_.begin()) {
    backing_store_.Dispose();
    backing_store_ = new_store;
  }
  position_ = new_content_size;
  is_one_byte_ = false;
}

void LiteralBuffer::AddTwoByteChar(base::uc32 code_unit) {
  DCHECK(!is_one_byte());
  if (position_ >= backing_store_.length()) ExpandBuffer();
  if (code_unit <=
      static_cast<base::uc32>(unibrow::Utf16::kMaxNonSurrogateCharCode)) {
    *reinterpret_cast<uint16_t*>(&backing_store_[position_]) = code_unit;
    position_ += base::kUC16Size;
  } else {
    *reinterpret_cast<uint16_t*>(&backing_store_[position_]) =
        unibrow::Utf16::LeadSurrogate(code_unit);
    position_ += base::kUC16Size;
    if (position_ >= backing_store_.length()) ExpandBuffer();
    *reinterpret_cast<uint16_t*>(&backing_store_[position_]) =
        unibrow::Utf16::TrailSurrogate(code_unit);
    position_ += base::kUC16Size;
  }
}

}  // namespace internal
}  // namespace v8
