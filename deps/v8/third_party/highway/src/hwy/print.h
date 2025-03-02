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

#ifndef HWY_PRINT_H_
#define HWY_PRINT_H_

// Helpers for printing vector lanes.

#include <stddef.h>
#include <stdio.h>

#include "hwy/base.h"
#include "hwy/highway_export.h"

namespace hwy {

namespace detail {

// For implementing value comparisons etc. as type-erased functions to reduce
// template bloat.
struct TypeInfo {
  size_t sizeof_t;
  bool is_float;
  bool is_signed;
  bool is_bf16;
};

template <typename T>
HWY_INLINE TypeInfo MakeTypeInfo() {
  TypeInfo info;
  info.sizeof_t = sizeof(T);
  info.is_float = IsFloat<T>();
  info.is_signed = IsSigned<T>();
  info.is_bf16 = IsSame<T, bfloat16_t>();
  return info;
}

HWY_DLLEXPORT void TypeName(const TypeInfo& info, size_t N, char* string100);
HWY_DLLEXPORT void ToString(const TypeInfo& info, const void* ptr,
                            char* string100);

HWY_DLLEXPORT void PrintArray(const TypeInfo& info, const char* caption,
                              const void* array_void, size_t N,
                              size_t lane_u = 0, size_t max_lanes = 7);

}  // namespace detail

template <typename T>
HWY_NOINLINE void PrintValue(T value) {
  char str[100];
  detail::ToString(hwy::detail::MakeTypeInfo<T>(), &value, str);
  fprintf(stderr, "%s,", str);
}

template <typename T>
HWY_NOINLINE void PrintArray(const T* value, size_t count) {
  detail::PrintArray(hwy::detail::MakeTypeInfo<T>(), "", value, count, 0,
                     count);
}

}  // namespace hwy

#endif  // HWY_PRINT_H_
