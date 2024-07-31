// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tracing/perfetto-utils.h"

#include "include/v8config.h"
#include "src/objects/string-inl.h"
#include "src/objects/string.h"
#include "src/objects/tagged.h"

namespace v8 {
namespace internal {

PerfettoV8String::PerfettoV8String(Tagged<String> string)
    : is_one_byte_(string->IsOneByteRepresentation()), size_(0) {
  if (string->length() <= 0) {
    return;
  }
  size_ = static_cast<size_t>(string->length()) *
          (string->IsOneByteRepresentation() ? sizeof(uint8_t)
                                             : sizeof(base::uc16));
  buffer_.reset(new uint8_t[size_]);
  if (is_one_byte_) {
    String::WriteToFlat(string, buffer_.get(), 0, string->length());
  } else {
    String::WriteToFlat(string, reinterpret_cast<base::uc16*>(buffer_.get()), 0,
                        string->length());
  }
}

}  // namespace internal
}  // namespace v8
