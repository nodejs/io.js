// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_MEMORY_H_
#define V8_BASE_MEMORY_H_

#include "src/base/macros.h"

namespace v8 {
namespace base {

using Address = uintptr_t;

// Memory provides an interface to 'raw' memory. It encapsulates the casts
// that typically are needed when incompatible pointer types are used.
// Note that this class currently relies on undefined behaviour. There is a
// proposal (http://wg21.link/p0593r2) to make it defined behaviour though.
template <class T>
inline T& Memory(Address addr) {
  DCHECK(IsAligned(addr, alignof(T)));
  return *reinterpret_cast<T*>(addr);
}
template <class T>
inline T& Memory(uint8_t* addr) {
  return Memory<T>(reinterpret_cast<Address>(addr));
}

template <typename V>
static inline V ReadUnalignedValue(Address p) {
  ASSERT_TRIVIALLY_COPYABLE(V);
  V r;
  memcpy(&r, reinterpret_cast<void*>(p), sizeof(V));
  return r;
}

template <typename V>
static inline V ReadUnalignedValue(const char p[sizeof(V)]) {
  return ReadUnalignedValue<V>(reinterpret_cast<Address>(p));
}

template <typename V>
static inline void WriteUnalignedValue(Address p, V value) {
  ASSERT_TRIVIALLY_COPYABLE(V);
  memcpy(reinterpret_cast<void*>(p), &value, sizeof(V));
}

template <typename V>
static inline void WriteUnalignedValue(char p[sizeof(V)], V value) {
  return WriteUnalignedValue<V>(reinterpret_cast<Address>(p), value);
}

template <typename V>
static inline V ReadLittleEndianValue(Address p) {
#if defined(V8_TARGET_LITTLE_ENDIAN)
  return ReadUnalignedValue<V>(p);
#elif defined(V8_TARGET_BIG_ENDIAN)
  V ret{};
  const uint8_t* src = reinterpret_cast<const uint8_t*>(p);
  uint8_t* dst = reinterpret_cast<uint8_t*>(&ret);
  for (size_t i = 0; i < sizeof(V); i++) {
    dst[i] = src[sizeof(V) - i - 1];
  }
  return ret;
#endif  // V8_TARGET_LITTLE_ENDIAN
}

template <typename V>
static inline void WriteLittleEndianValue(Address p, V value) {
#if defined(V8_TARGET_LITTLE_ENDIAN)
  WriteUnalignedValue<V>(p, value);
#elif defined(V8_TARGET_BIG_ENDIAN)
  uint8_t* src = reinterpret_cast<uint8_t*>(&value);
  uint8_t* dst = reinterpret_cast<uint8_t*>(p);
  for (size_t i = 0; i < sizeof(V); i++) {
    dst[i] = src[sizeof(V) - i - 1];
  }
#endif  // V8_TARGET_LITTLE_ENDIAN
}

template <typename V>
static inline V ReadLittleEndianValue(V* p) {
  return ReadLittleEndianValue<V>(reinterpret_cast<Address>(p));
}

template <typename V>
static inline void WriteLittleEndianValue(V* p, V value) {
  static_assert(
      !std::is_array<V>::value,
      "Passing an array decays to pointer, causing unexpected results.");
  WriteLittleEndianValue<V>(reinterpret_cast<Address>(p), value);
}

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_MEMORY_H_
