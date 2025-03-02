// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UTILS_UTILS_INL_H_
#define V8_UTILS_UTILS_INL_H_

#include "include/v8-platform.h"
#include "src/base/platform/time.h"
#include "src/base/strings.h"
#include "src/init/v8.h"
#include "src/strings/char-predicates-inl.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

class V8_NODISCARD TimedScope {
 public:
  explicit TimedScope(double* result)
      : start_(TimestampMs()), result_(result) {}

  ~TimedScope() { *result_ = TimestampMs() - start_; }

 private:
  static inline double TimestampMs() {
    return V8::GetCurrentPlatform()->MonotonicallyIncreasingTime() *
           static_cast<double>(base::Time::kMillisecondsPerSecond);
  }

  double start_;
  double* result_;
};

template <typename Char>
bool TryAddArrayIndexChar(uint32_t* index, Char c) {
  DCHECK(IsDecimalDigit(c));
  int d = c - '0';
  // The maximum index is 4294967294; for the computation below to not
  // exceed that, the previous index value must be <= 429496729 if d <= 4,
  // or <= 429496728 if d >= 5. The (d+3)>>3 computation is a branch-free
  // way to express that.
  if (*index > 429496729U - ((d + 3) >> 3)) return false;
  *index = (*index) * 10 + d;
  return true;
}

template <typename Char>
bool TryAddIntegerIndexChar(uint64_t* index, Char c) {
  DCHECK(IsDecimalDigit(c));
  int d = c - '0';
  *index = (*index) * 10 + d;
  return (*index <= kMaxSafeIntegerUint64);
}

template <typename Stream, typename index_t, enum ToIndexMode mode>
bool StringToIndex(Stream* stream, index_t* index) {
  uint16_t ch = stream->GetNext();

  // If the string begins with a '0' character, it must only consist
  // of it to be a legal array index.
  if (ch == '0') {
    *index = 0;
    return !stream->HasMore();
  }

  // Convert string to uint32 array index; character by character.
  if (!IsDecimalDigit(ch)) return false;
  int d = ch - '0';
  index_t result = d;
  while (stream->HasMore()) {
    // Clang on Mac doesn't think that size_t and uint*_t should be
    // implicitly convertible.
    base::uc32 c = stream->GetNext();
    if (!IsDecimalDigit(c)) return false;
    if (sizeof(result) == 8) {
      DCHECK_EQ(kToIntegerIndex, mode);
      if (!TryAddIntegerIndexChar(reinterpret_cast<uint64_t*>(&result), c)) {
        return false;
      }
    } else {
      // Either mode is fine here.
      if (!TryAddArrayIndexChar(reinterpret_cast<uint32_t*>(&result), c))
        return false;
    }
  }

  *index = result;
  return true;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_UTILS_UTILS_INL_H_
