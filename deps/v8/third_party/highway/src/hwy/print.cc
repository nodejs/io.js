// Copyright 2022 Google LLC
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

#include "hwy/print.h"

#include <stdio.h>

#include "hwy/base.h"
#include "hwy/detect_compiler_arch.h"

namespace hwy {
namespace detail {

HWY_DLLEXPORT void TypeName(const TypeInfo& info, size_t N, char* string100) {
  const char prefix = info.is_float ? 'f' : (info.is_signed ? 'i' : 'u');
  // Omit the xN suffix for scalars.
  if (N == 1) {
    // NOLINTNEXTLINE
    snprintf(string100, 64, "%c%d", prefix,
             static_cast<int>(info.sizeof_t * 8));
  } else {
    // NOLINTNEXTLINE
    snprintf(string100, 64, "%c%dx%d", prefix,
             static_cast<int>(info.sizeof_t * 8), static_cast<int>(N));
  }
}

HWY_DLLEXPORT void ToString(const TypeInfo& info, const void* ptr,
                            char* string100) {
  if (info.sizeof_t == 1) {
    if (info.is_signed) {
      int8_t byte;
      CopyBytes<1>(ptr, &byte);  // endian-safe: we ensured sizeof(T)=1.
      snprintf(string100, 100, "%d", byte);  // NOLINT
    } else {
      uint8_t byte;
      CopyBytes<1>(ptr, &byte);  // endian-safe: we ensured sizeof(T)=1.
      snprintf(string100, 100, "0x%02X", byte);  // NOLINT
    }
  } else if (info.sizeof_t == 2) {
    if (info.is_bf16) {
      const double value = static_cast<double>(F32FromBF16Mem(ptr));
      const char* fmt = hwy::ScalarAbs(value) < 1E-3 ? "%.3E" : "%.3f";
      snprintf(string100, 100, fmt, value);  // NOLINT
    } else if (info.is_float) {
      const double value = static_cast<double>(F32FromF16Mem(ptr));
      const char* fmt = hwy::ScalarAbs(value) < 1E-4 ? "%.4E" : "%.4f";
      snprintf(string100, 100, fmt, value);  // NOLINT
    } else {
      uint16_t bits;
      CopyBytes<2>(ptr, &bits);
      snprintf(string100, 100, "0x%04X", bits);  // NOLINT
    }
  } else if (info.sizeof_t == 4) {
    if (info.is_float) {
      float value;
      CopyBytes<4>(ptr, &value);
      const char* fmt = hwy::ScalarAbs(value) < 1E-6 ? "%.9E" : "%.9f";
      snprintf(string100, 100, fmt, static_cast<double>(value));  // NOLINT
    } else if (info.is_signed) {
      int32_t value;
      CopyBytes<4>(ptr, &value);
      snprintf(string100, 100, "%d", value);  // NOLINT
    } else {
      uint32_t value;
      CopyBytes<4>(ptr, &value);
      snprintf(string100, 100, "%u", value);  // NOLINT
    }
  } else if (info.sizeof_t == 8) {
    if (info.is_float) {
      double value;
      CopyBytes<8>(ptr, &value);
      const char* fmt = hwy::ScalarAbs(value) < 1E-9 ? "%.18E" : "%.18f";
      snprintf(string100, 100, fmt, value);  // NOLINT
    } else {
      const uint8_t* ptr8 = reinterpret_cast<const uint8_t*>(ptr);
      uint32_t lo, hi;
      CopyBytes<4>(ptr8 + (HWY_IS_LITTLE_ENDIAN ? 0 : 4), &lo);
      CopyBytes<4>(ptr8 + (HWY_IS_LITTLE_ENDIAN ? 4 : 0), &hi);
      snprintf(string100, 100, "0x%08x%08x", hi, lo);  // NOLINT
    }
  } else if (info.sizeof_t == 16) {
    HWY_ASSERT(!info.is_float && !info.is_signed && !info.is_bf16);
    const uint8_t* ptr8 = reinterpret_cast<const uint8_t*>(ptr);
    uint32_t words[4];
    CopyBytes<4>(ptr8 + (HWY_IS_LITTLE_ENDIAN ? 0 : 12), &words[0]);
    CopyBytes<4>(ptr8 + (HWY_IS_LITTLE_ENDIAN ? 4 : 8), &words[1]);
    CopyBytes<4>(ptr8 + (HWY_IS_LITTLE_ENDIAN ? 8 : 4), &words[2]);
    CopyBytes<4>(ptr8 + (HWY_IS_LITTLE_ENDIAN ? 12 : 0), &words[3]);
    // NOLINTNEXTLINE
    snprintf(string100, 100, "0x%08x%08x_%08x%08x", words[3], words[2],
             words[1], words[0]);
  }
}

HWY_DLLEXPORT void PrintArray(const TypeInfo& info, const char* caption,
                              const void* array_void, size_t N, size_t lane_u,
                              size_t max_lanes) {
  const uint8_t* array_bytes = reinterpret_cast<const uint8_t*>(array_void);

  char type_name[100];
  TypeName(info, N, type_name);

  const intptr_t lane = intptr_t(lane_u);
  const size_t begin = static_cast<size_t>(HWY_MAX(0, lane - 2));
  const size_t end = HWY_MIN(begin + max_lanes, N);
  fprintf(stderr, "%s %s [%d+ ->]:\n  ", type_name, caption,
          static_cast<int>(begin));
  for (size_t i = begin; i < end; ++i) {
    const void* ptr = array_bytes + i * info.sizeof_t;
    char str[100];
    ToString(info, ptr, str);
    fprintf(stderr, "%s,", str);
  }
  if (begin >= end) fprintf(stderr, "(out of bounds)");
  fprintf(stderr, "\n");
}

}  // namespace detail
}  // namespace hwy
