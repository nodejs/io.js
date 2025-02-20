// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_STRINGS_STRING_CASE_H_
#define V8_STRINGS_STRING_CASE_H_

#include <cinttypes>

namespace v8 {
namespace internal {

template <bool is_lower>
uint32_t FastAsciiConvert(char* dst, const char* src, uint32_t length,
                          bool* changed_out);

}  // namespace internal
}  // namespace v8

#endif  // V8_STRINGS_STRING_CASE_H_
