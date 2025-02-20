// Copyright 2021 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "hwy/tests/test_util.h"

#include <stdio.h>

#include "hwy/base.h"
#include "hwy/print.h"

namespace hwy {

HWY_TEST_DLLEXPORT bool BytesEqual(const void* p1, const void* p2,
                                   const size_t size, size_t* pos) {
  const uint8_t* bytes1 = reinterpret_cast<const uint8_t*>(p1);
  const uint8_t* bytes2 = reinterpret_cast<const uint8_t*>(p2);
  for (size_t i = 0; i < size; ++i) {
    if (bytes1[i] != bytes2[i]) {
      if (pos != nullptr) {
        *pos = i;
      }
      return false;
    }
  }
  return true;
}

void AssertStringEqual(const char* expected, const char* actual,
                       const char* target_name, const char* filename,
                       int line) {
  while (*expected == *actual++) {
    if (*expected++ == '\0') return;
  }

  Abort(filename, line, "%s string mismatch: expected '%s', got '%s'.\n",
        target_name, expected, actual);
}

namespace detail {

HWY_TEST_DLLEXPORT bool IsEqual(const TypeInfo& info, const void* expected_ptr,
                                const void* actual_ptr) {
  if (!info.is_float) {
    return BytesEqual(expected_ptr, actual_ptr, info.sizeof_t);
  }
  if (info.sizeof_t == 2) {
    const float expected = info.is_bf16 ? F32FromBF16Mem(expected_ptr)
                                        : F32FromF16Mem(expected_ptr);
    const float actual =
        info.is_bf16 ? F32FromBF16Mem(actual_ptr) : F32FromF16Mem(actual_ptr);
    return ComputeUlpDelta(expected, actual) <= 1;
  } else if (info.sizeof_t == 4) {
    float expected, actual;
    CopyBytes<4>(expected_ptr, &expected);
    CopyBytes<4>(actual_ptr, &actual);
    return ComputeUlpDelta(expected, actual) <= 1;
  } else if (info.sizeof_t == 8) {
    double expected, actual;
    CopyBytes<8>(expected_ptr, &expected);
    CopyBytes<8>(actual_ptr, &actual);
    return ComputeUlpDelta(expected, actual) <= 1;
  } else {
    HWY_ABORT("Unexpected float size %d\n", static_cast<int>(info.sizeof_t));
  }
}

HWY_TEST_DLLEXPORT HWY_NORETURN void PrintMismatchAndAbort(
    const TypeInfo& info, const void* expected_ptr, const void* actual_ptr,
    const char* target_name, const char* filename, int line, size_t lane,
    size_t num_lanes) {
  char type_name[100];
  TypeName(info, 1, type_name);
  char expected_str[100];
  ToString(info, expected_ptr, expected_str);
  char actual_str[100];
  ToString(info, actual_ptr, actual_str);
  Abort(filename, line,
        "%s, %sx%d lane %d mismatch: expected '%s', got '%s'.\n", target_name,
        type_name, static_cast<int>(num_lanes), static_cast<int>(lane),
        expected_str, actual_str);
}

HWY_TEST_DLLEXPORT void AssertArrayEqual(const TypeInfo& info,
                                         const void* expected_void,
                                         const void* actual_void, size_t N,
                                         const char* target_name,
                                         const char* filename, int line) {
  const uint8_t* expected_array =
      reinterpret_cast<const uint8_t*>(expected_void);
  const uint8_t* actual_array = reinterpret_cast<const uint8_t*>(actual_void);
  for (size_t i = 0; i < N; ++i) {
    const void* expected_ptr = expected_array + i * info.sizeof_t;
    const void* actual_ptr = actual_array + i * info.sizeof_t;
    if (!IsEqual(info, expected_ptr, actual_ptr)) {
      fprintf(stderr, "\n\n");
      PrintArray(info, "expect", expected_array, N, i);
      PrintArray(info, "actual", actual_array, N, i);

      PrintMismatchAndAbort(info, expected_ptr, actual_ptr, target_name,
                            filename, line, i, N);
    }
  }
}

}  // namespace detail
}  // namespace hwy
