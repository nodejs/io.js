// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_FUNCTIONAL_H_
#define V8_BASE_FUNCTIONAL_H_

#include <stddef.h>
#include <stdint.h>

#include <cstddef>
#include <cstring>
#include <functional>
#include <type_traits>
#include <utility>

#include "src/base/base-export.h"
#include "src/base/bits.h"
#include "src/base/macros.h"

namespace v8::base {

// base::hash is an implementation of the hash function object specified by
// C++11. It was designed to be compatible with std::hash (in C++11) and
// boost:hash (which in turn is based on the hash function object specified by
// the Draft Technical Report on C++ Library Extensions (TR1)).
//
// base::hash is implemented by calling either the hash_value function or the
// hash_value member function. In the first case, the namespace is not specified
// so that it can detect overloads via argument dependent lookup. So if there is
// a free function hash_value in the same namespace as a custom type, it will
// get called.
//
// If users are asked to implement a hash function for their own types with no
// guidance, they generally write bad hash functions. Instead, we provide a
// base::Hasher class to pass hash-relevant member variables into, in order to
// define a decent hash function.
//
// Consider the following example:
//
//   namespace v8 {
//   namespace bar {
//     struct Coordinate {
//       int val;
//       size_t hash_value() const { return hash_value(val); }
//     };
//     struct Point {
//       Coordinate x;
//       Coordinate y;
//     };
//     size_t hash_value(Point const& p) {
//       return base::Hasher::Combine(p.x, p.y);
//     }
//   }
//
//   namespace foo {
//     void DoSomeWork(bar::Point const& p) {
//       base::hash<bar::Point> h;
//       ...
//       size_t hash = h(p);  // calls bar::hash_value(Point const&), which
//                            // calls p.x.hash_value() and p.y.hash_value().
//       ...
//     }
//   }
//   }
//
// This header also provides implementations of hash_value for basic types.
//
// Based on the "Hashing User-Defined Types in C++1y" proposal from Jeffrey
// Yasskin and Chandler Carruth, see
// http://www.open-std.org/Jtc1/sc22/wg21/docs/papers/2012/n3333.html.

template <typename>
struct hash;

// Combine two hash values together. This code was taken from MurmurHash.
V8_INLINE size_t hash_combine(size_t seed, size_t hash) {
#if V8_HOST_ARCH_32_BIT
  const uint32_t c1 = 0xCC9E2D51;
  const uint32_t c2 = 0x1B873593;

  hash *= c1;
  hash = bits::RotateRight32(hash, 15);
  hash *= c2;

  seed ^= hash;
  seed = bits::RotateRight32(seed, 13);
  seed = seed * 5 + 0xE6546B64;
#else
  const uint64_t m = uint64_t{0xC6A4A7935BD1E995};
  const uint32_t r = 47;

  hash *= m;
  hash ^= hash >> r;
  hash *= m;

  seed ^= hash;
  seed *= m;
#endif  // V8_HOST_ARCH_32_BIT
  return seed;
}

// base::Hasher makes it easier to combine multiple fields into one hash and
// avoids the ambiguity of the different {hash_combine} methods.
class Hasher {
 public:
  constexpr Hasher() = default;
  constexpr explicit Hasher(size_t seed) : hash_(seed) {}

  // Retrieve the current hash.
  constexpr size_t hash() const { return hash_; }

  // Combine an existing hash value into this hasher's hash.
  Hasher& AddHash(size_t other_hash) {
    hash_ = hash_combine(hash_, other_hash);
    return *this;
  }

  // Hash a value {t} and combine its hash into this hasher's hash.
  template <typename T>
  Hasher& Add(const T& t) {
    return AddHash(base::hash<T>{}(t));
  }

  // Hash a range of values and combine the hashes into this hasher's hash.
  template <typename Iterator>
  Hasher& AddRange(Iterator first, Iterator last) {
    // TODO(clemensb): If the iterator returns an integral or POD value smaller
    // than size_t we can combine multiple elements together to get better
    // hashing performance.
    for (; first != last; ++first) Add(*first);
    return *this;
  }

  // Hash a collection of values and combine the hashes into this hasher's hash.
  template <typename C>
  auto AddRange(C collection)
      -> decltype(AddRange(std::begin(collection), std::end(collection))) {
    return AddRange(std::begin(collection), std::end(collection));
  }

  // Hash multiple values and combine their hashes.
  template <typename... T>
  constexpr static size_t Combine(const T&... ts) {
    Hasher hasher;
    (..., hasher.Add(ts));
    return hasher.hash();
  }

 private:
  size_t hash_ = 0;
};

// Thomas Wang, Integer Hash Functions.
// https://gist.github.com/badboy/6267743
template <typename T>
V8_INLINE size_t hash_value_unsigned_impl(T v) {
  switch (sizeof(T)) {
    case 4: {
      // "32 bit Mix Functions"
      v = ~v + (v << 15);  // v = (v << 15) - v - 1;
      v = v ^ (v >> 12);
      v = v + (v << 2);
      v = v ^ (v >> 4);
      v = v * 2057;  // v = (v + (v << 3)) + (v << 11);
      v = v ^ (v >> 16);
      return static_cast<size_t>(v);
    }
    case 8: {
      switch (sizeof(size_t)) {
        case 4: {
          // "64 bit to 32 bit Hash Functions"
          v = ~v + (v << 18);  // v = (v << 18) - v - 1;
          v = v ^ (v >> 31);
          v = v * 21;  // v = (v + (v << 2)) + (v << 4);
          v = v ^ (v >> 11);
          v = v + (v << 6);
          v = v ^ (v >> 22);
          return static_cast<size_t>(v);
        }
        case 8: {
          // "64 bit Mix Functions"
          v = ~v + (v << 21);  // v = (v << 21) - v - 1;
          v = v ^ (v >> 24);
          v = (v + (v << 3)) + (v << 8);  // v * 265
          v = v ^ (v >> 14);
          v = (v + (v << 2)) + (v << 4);  // v * 21
          v = v ^ (v >> 28);
          v = v + (v << 31);
          return static_cast<size_t>(v);
        }
      }
    }
  }
  UNREACHABLE();
}

#define V8_BASE_HASH_VALUE_TRIVIAL(type) \
  V8_INLINE size_t hash_value(type v) { return static_cast<size_t>(v); }
V8_BASE_HASH_VALUE_TRIVIAL(bool)
V8_BASE_HASH_VALUE_TRIVIAL(unsigned char)
V8_BASE_HASH_VALUE_TRIVIAL(unsigned short)  // NOLINT(runtime/int)
#undef V8_BASE_HASH_VALUE_TRIVIAL

V8_INLINE size_t hash_value(unsigned int v) {
  return hash_value_unsigned_impl(v);
}

V8_INLINE size_t hash_value(unsigned long v) {  // NOLINT(runtime/int)
  return hash_value_unsigned_impl(v);
}

V8_INLINE size_t hash_value(unsigned long long v) {  // NOLINT(runtime/int)
  return hash_value_unsigned_impl(v);
}

#define V8_BASE_HASH_VALUE_SIGNED(type)                  \
  V8_INLINE size_t hash_value(signed type v) {           \
    return hash_value(base::bit_cast<unsigned type>(v)); \
  }
V8_BASE_HASH_VALUE_SIGNED(char)
V8_BASE_HASH_VALUE_SIGNED(short)      // NOLINT(runtime/int)
V8_BASE_HASH_VALUE_SIGNED(int)        // NOLINT(runtime/int)
V8_BASE_HASH_VALUE_SIGNED(long)       // NOLINT(runtime/int)
V8_BASE_HASH_VALUE_SIGNED(long long)  // NOLINT(runtime/int)
#undef V8_BASE_HASH_VALUE_SIGNED

V8_INLINE size_t hash_value(float v) {
  // 0 and -0 both hash to zero.
  return v != 0.0f ? hash_value(base::bit_cast<uint32_t>(v)) : 0;
}

V8_INLINE size_t hash_value(double v) {
  // 0 and -0 both hash to zero.
  return v != 0.0 ? hash_value(base::bit_cast<uint64_t>(v)) : 0;
}

template <typename T, size_t N>
V8_INLINE size_t hash_value(const T (&v)[N]) {
  return Hasher{}.AddRange(v, v + N).hash();
}

template <typename T, size_t N>
V8_INLINE size_t hash_value(T (&v)[N]) {
  return Hasher{}.AddRange(v, v + N).hash();
}

template <typename T>
V8_INLINE size_t hash_value(T* const& v) {
  return hash_value(reinterpret_cast<uintptr_t>(v));
}

template <typename T1, typename T2>
V8_INLINE size_t hash_value(std::pair<T1, T2> const& v) {
  return Hasher::Combine(v.first, v.second);
}

template <typename... T, size_t... I>
V8_INLINE size_t hash_value_impl(std::tuple<T...> const& v,
                                 std::index_sequence<I...>) {
  return Hasher::Combine(std::get<I>(v)...);
}

template <typename... T>
V8_INLINE size_t hash_value(std::tuple<T...> const& v) {
  return hash_value_impl(v, std::make_index_sequence<sizeof...(T)>());
}

template <typename T, typename = std::enable_if_t<std::is_enum<T>::value>>
V8_INLINE size_t hash_value(T v) {
  return hash_value(static_cast<std::underlying_type_t<T>>(v));
}

// Provide a hash_value function for each T with a hash_value member function.
template <typename T>
V8_INLINE auto hash_value(const T& v) -> decltype(v.hash_value()) {
  return v.hash_value();
}

// Define base::hash to call the hash_value function or member function.
template <typename T>
struct hash {
  V8_INLINE constexpr size_t operator()(const T& v) const {
    return hash_value(v);
  }
};

template <typename T>
struct hash<T*> {
  V8_INLINE size_t operator()(T* const v) const {
    return ::v8::base::hash_value(v);
  }
};

// TODO(clemensb): Depending on the types in this template the compiler might
// pick {hash_combine(size_t, size_t)} instead. Thus remove this template and
// switch callers to {Hasher::Combine}.
template <typename... Ts>
V8_INLINE size_t hash_combine(Ts const&... vs) {
  return Hasher{}.Combine(vs...);
}

// TODO(clemensb): Switch users to {Hasher{}.AddRange(first, last).hash()}.
template <typename Iterator>
V8_INLINE size_t hash_range(Iterator first, Iterator last) {
  return Hasher{}.AddRange(first, last).hash();
}

// base::bit_equal_to is a function object class for bitwise equality
// comparison, similar to std::equal_to, except that the comparison is performed
// on the bit representation of the operands.
//
// base::bit_hash is a function object class for bitwise hashing, similar to
// base::hash. It can be used together with base::bit_equal_to to implement a
// hash data structure based on the bitwise representation of types.

template <typename T>
struct bit_equal_to {};

template <typename T>
struct bit_hash {};

#define V8_BASE_BIT_SPECIALIZE_TRIVIAL(type)                 \
  template <>                                                \
  struct bit_equal_to<type> : public std::equal_to<type> {}; \
  template <>                                                \
  struct bit_hash<type> : public hash<type> {};
V8_BASE_BIT_SPECIALIZE_TRIVIAL(signed char)
V8_BASE_BIT_SPECIALIZE_TRIVIAL(unsigned char)
V8_BASE_BIT_SPECIALIZE_TRIVIAL(short)           // NOLINT(runtime/int)
V8_BASE_BIT_SPECIALIZE_TRIVIAL(unsigned short)  // NOLINT(runtime/int)
V8_BASE_BIT_SPECIALIZE_TRIVIAL(int)
V8_BASE_BIT_SPECIALIZE_TRIVIAL(unsigned int)
V8_BASE_BIT_SPECIALIZE_TRIVIAL(long)                // NOLINT(runtime/int)
V8_BASE_BIT_SPECIALIZE_TRIVIAL(unsigned long)       // NOLINT(runtime/int)
V8_BASE_BIT_SPECIALIZE_TRIVIAL(long long)           // NOLINT(runtime/int)
V8_BASE_BIT_SPECIALIZE_TRIVIAL(unsigned long long)  // NOLINT(runtime/int)
#undef V8_BASE_BIT_SPECIALIZE_TRIVIAL

#define V8_BASE_BIT_SPECIALIZE_BIT_CAST(type, btype)                   \
  template <>                                                          \
  struct bit_equal_to<type> {                                          \
    V8_INLINE bool operator()(type lhs, type rhs) const {              \
      return base::bit_cast<btype>(lhs) == base::bit_cast<btype>(rhs); \
    }                                                                  \
  };                                                                   \
  template <>                                                          \
  struct bit_hash<type> {                                              \
    V8_INLINE size_t operator()(type v) const {                        \
      hash<btype> h;                                                   \
      return h(base::bit_cast<btype>(v));                              \
    }                                                                  \
  };
V8_BASE_BIT_SPECIALIZE_BIT_CAST(float, uint32_t)
V8_BASE_BIT_SPECIALIZE_BIT_CAST(double, uint64_t)
#undef V8_BASE_BIT_SPECIALIZE_BIT_CAST

}  // namespace v8::base

#endif  // V8_BASE_FUNCTIONAL_H_
