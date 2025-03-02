// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_STD_OBJECT_SIZES_H_
#define V8_WASM_STD_OBJECT_SIZES_H_

#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "include/v8config.h"
#include "src/base/vector.h"

namespace v8::internal::wasm {

// These helpers are used to estimate the memory consumption of standard
// data structures off the managed heap.
// The size of the container itself is not included here, because it's
// typically included in the size of the containing element.

template <typename T>
inline size_t ContentSize(const std::vector<T>& vector) {
  // We use {capacity()} rather than {size()} because we want to compute
  // actual memory consumption.
  return vector.capacity() * sizeof(T);
}

template <typename T>
inline size_t ContentSize(const base::OwnedVector<T>& vector) {
  return vector.size() * sizeof(T);
}

template <typename Key, typename T>
inline size_t ContentSize(const std::map<Key, T>& map) {
  // Very rough lower bound approximation: two internal pointers per entry.
  return map.size() * (sizeof(Key) + sizeof(T) + 2 * sizeof(void*));
}

template <typename Key, typename T, typename Hash>
inline size_t ContentSize(const std::unordered_map<Key, T, Hash>& map) {
  // Very rough lower bound approximation: two internal pointers per entry.
  size_t raw = map.size() * (sizeof(Key) + sizeof(T) + 2 * sizeof(void*));
  // In the spirit of computing lower bounds of definitely-used memory,
  // we assume a 75% fill ratio.
  return raw * 4 / 3;
}

template <typename T, typename Hash>
inline size_t ContentSize(const std::unordered_set<T, Hash>& set) {
  // Very rough lower bound approximation: two internal pointers per entry.
  size_t raw = set.size() * (sizeof(T) + 2 * sizeof(void*));
  // In the spirit of computing lower bounds of definitely-used memory,
  // we assume a 75% fill ratio.
  return raw * 4 / 3;
}

// To make it less likely for size estimation functions to become outdated
// when the classes they're responsible for change, we insert static asserts
// about the respective class's size into them to at least catch some possible
// future modifications. Since object sizes are toolchain specific, we define
// restrictions here under which we enable these checks.
// When one of these checks fails, that probably means you've added fields to
// a class guarded by it. Update the respective EstimateCurrentMemoryConsumption
// function accordingly, and then update the check's expected size.
#if V8_TARGET_ARCH_X64 && defined(__clang__) && V8_TARGET_OS_LINUX &&          \
    !V8_USE_ADDRESS_SANITIZER && !V8_USE_MEMORY_SANITIZER && defined(DEBUG) && \
    V8_COMPRESS_POINTERS && !defined(V8_GC_MOLE) && defined(_LIBCPP_VERSION)
#define UPDATE_WHEN_CLASS_CHANGES(classname, size)                       \
  static_assert(sizeof(classname) == size,                               \
                "Update {EstimateCurrentMemoryConsumption} when adding " \
                "fields to " #classname)
#else
#define UPDATE_WHEN_CLASS_CHANGES(classname, size) (void)0
#endif

}  // namespace v8::internal::wasm

#endif  // V8_WASM_STD_OBJECT_SIZES_H_
