// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TRACING_PERFETTO_UTILS_H_
#define V8_TRACING_PERFETTO_UTILS_H_

#include <cstdint>
#include <cstring>
#include <vector>

#include "include/v8config.h"
#include "src/base/functional.h"
#include "src/base/logging.h"
#include "src/objects/string.h"
#include "src/objects/tagged.h"

namespace v8 {
namespace internal {

// Helper class to write String objects into Perfetto protos. Deals with
// character encoding and String objects composed of multiple slices.
class PerfettoV8String {
 public:
  explicit PerfettoV8String(Tagged<String> string);

  PerfettoV8String(const PerfettoV8String&) V8_NOEXCEPT = delete;
  PerfettoV8String& operator=(const PerfettoV8String&) V8_NOEXCEPT = delete;

  PerfettoV8String(PerfettoV8String&&) V8_NOEXCEPT = default;
  PerfettoV8String& operator=(PerfettoV8String&&) V8_NOEXCEPT = default;

  bool is_one_byte() const { return is_one_byte_; }
  template <typename Proto>
  void WriteToProto(Proto& proto) const {
    if (is_one_byte()) {
      proto.set_latin1(buffer_.get(), size_);
    } else {
#if defined(V8_TARGET_BIG_ENDIAN)
      proto.set_utf16_be(buffer_.get(), size_);
#else
      proto.set_utf16_le(buffer_.get(), size_);
#endif
    }
  }

  bool operator==(const PerfettoV8String& o) const {
    return is_one_byte_ == o.is_one_byte_ && size_ == o.size_ &&
           memcmp(buffer_.get(), o.buffer_.get(), size_) == 0;
  }

  bool operator!=(const PerfettoV8String& o) const { return !(*this == o); }

  struct Hasher {
    size_t operator()(const PerfettoV8String& s) const {
      base::Hasher hash;
      hash.AddRange(s.buffer_.get(), s.buffer_.get() + s.size_);
      hash.Combine(s.is_one_byte_);
      return hash.hash();
    }
  };

 private:
  bool is_one_byte_;
  size_t size_;
  std::unique_ptr<uint8_t[]> buffer_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_TRACING_PERFETTO_UTILS_H_
