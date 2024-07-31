// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef ABSL_BASE_INTERNAL_ENDIAN_H_
#define ABSL_BASE_INTERNAL_ENDIAN_H_

#include <cstdint>
#include <cstdlib>

#include "absl/base/casts.h"
#include "absl/base/config.h"
#include "absl/base/internal/unaligned_access.h"
#include "absl/base/nullability.h"
#include "absl/base/port.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

inline uint64_t gbswap_64(uint64_t host_int) {
#if ABSL_HAVE_BUILTIN(__builtin_bswap64) || defined(__GNUC__)
  return __builtin_bswap64(host_int);
#elif defined(_MSC_VER)
  return _byteswap_uint64(host_int);
#else
  return (((host_int & uint64_t{0xFF}) << 56) |
          ((host_int & uint64_t{0xFF00}) << 40) |
          ((host_int & uint64_t{0xFF0000}) << 24) |
          ((host_int & uint64_t{0xFF000000}) << 8) |
          ((host_int & uint64_t{0xFF00000000}) >> 8) |
          ((host_int & uint64_t{0xFF0000000000}) >> 24) |
          ((host_int & uint64_t{0xFF000000000000}) >> 40) |
          ((host_int & uint64_t{0xFF00000000000000}) >> 56));
#endif
}

inline uint32_t gbswap_32(uint32_t host_int) {
#if ABSL_HAVE_BUILTIN(__builtin_bswap32) || defined(__GNUC__)
  return __builtin_bswap32(host_int);
#elif defined(_MSC_VER)
  return _byteswap_ulong(host_int);
#else
  return (((host_int & uint32_t{0xFF}) << 24) |
          ((host_int & uint32_t{0xFF00}) << 8) |
          ((host_int & uint32_t{0xFF0000}) >> 8) |
          ((host_int & uint32_t{0xFF000000}) >> 24));
#endif
}

inline uint16_t gbswap_16(uint16_t host_int) {
#if ABSL_HAVE_BUILTIN(__builtin_bswap16) || defined(__GNUC__)
  return __builtin_bswap16(host_int);
#elif defined(_MSC_VER)
  return _byteswap_ushort(host_int);
#else
  return (((host_int & uint16_t{0xFF}) << 8) |
          ((host_int & uint16_t{0xFF00}) >> 8));
#endif
}

#ifdef ABSL_IS_LITTLE_ENDIAN

// Portable definitions for htonl (host-to-network) and friends on little-endian
// architectures.
inline uint16_t ghtons(uint16_t x) { return gbswap_16(x); }
inline uint32_t ghtonl(uint32_t x) { return gbswap_32(x); }
inline uint64_t ghtonll(uint64_t x) { return gbswap_64(x); }

#elif defined ABSL_IS_BIG_ENDIAN

// Portable definitions for htonl (host-to-network) etc on big-endian
// architectures. These definitions are simpler since the host byte order is the
// same as network byte order.
inline uint16_t ghtons(uint16_t x) { return x; }
inline uint32_t ghtonl(uint32_t x) { return x; }
inline uint64_t ghtonll(uint64_t x) { return x; }

#else
#error \
    "Unsupported byte order: Either ABSL_IS_BIG_ENDIAN or " \
       "ABSL_IS_LITTLE_ENDIAN must be defined"
#endif  // byte order

inline uint16_t gntohs(uint16_t x) { return ghtons(x); }
inline uint32_t gntohl(uint32_t x) { return ghtonl(x); }
inline uint64_t gntohll(uint64_t x) { return ghtonll(x); }

// Utilities to convert numbers between the current hosts's native byte
// order and little-endian byte order
//
// Load/Store methods are alignment safe
namespace little_endian {
// Conversion functions.
#ifdef ABSL_IS_LITTLE_ENDIAN

inline uint16_t FromHost16(uint16_t x) { return x; }
inline uint16_t ToHost16(uint16_t x) { return x; }

inline uint32_t FromHost32(uint32_t x) { return x; }
inline uint32_t ToHost32(uint32_t x) { return x; }

inline uint64_t FromHost64(uint64_t x) { return x; }
inline uint64_t ToHost64(uint64_t x) { return x; }

inline constexpr bool IsLittleEndian() { return true; }

#elif defined ABSL_IS_BIG_ENDIAN

inline uint16_t FromHost16(uint16_t x) { return gbswap_16(x); }
inline uint16_t ToHost16(uint16_t x) { return gbswap_16(x); }

inline uint32_t FromHost32(uint32_t x) { return gbswap_32(x); }
inline uint32_t ToHost32(uint32_t x) { return gbswap_32(x); }

inline uint64_t FromHost64(uint64_t x) { return gbswap_64(x); }
inline uint64_t ToHost64(uint64_t x) { return gbswap_64(x); }

inline constexpr bool IsLittleEndian() { return false; }

#endif /* ENDIAN */

inline uint8_t FromHost(uint8_t x) { return x; }
inline uint16_t FromHost(uint16_t x) { return FromHost16(x); }
inline uint32_t FromHost(uint32_t x) { return FromHost32(x); }
inline uint64_t FromHost(uint64_t x) { return FromHost64(x); }
inline uint8_t ToHost(uint8_t x) { return x; }
inline uint16_t ToHost(uint16_t x) { return ToHost16(x); }
inline uint32_t ToHost(uint32_t x) { return ToHost32(x); }
inline uint64_t ToHost(uint64_t x) { return ToHost64(x); }

inline int8_t FromHost(int8_t x) { return x; }
inline int16_t FromHost(int16_t x) {
  return bit_cast<int16_t>(FromHost16(bit_cast<uint16_t>(x)));
}
inline int32_t FromHost(int32_t x) {
  return bit_cast<int32_t>(FromHost32(bit_cast<uint32_t>(x)));
}
inline int64_t FromHost(int64_t x) {
  return bit_cast<int64_t>(FromHost64(bit_cast<uint64_t>(x)));
}
inline int8_t ToHost(int8_t x) { return x; }
inline int16_t ToHost(int16_t x) {
  return bit_cast<int16_t>(ToHost16(bit_cast<uint16_t>(x)));
}
inline int32_t ToHost(int32_t x) {
  return bit_cast<int32_t>(ToHost32(bit_cast<uint32_t>(x)));
}
inline int64_t ToHost(int64_t x) {
  return bit_cast<int64_t>(ToHost64(bit_cast<uint64_t>(x)));
}

// Functions to do unaligned loads and stores in little-endian order.
inline uint16_t Load16(absl::Nonnull<const void *> p) {
  return ToHost16(ABSL_INTERNAL_UNALIGNED_LOAD16(p));
}

inline void Store16(absl::Nonnull<void *> p, uint16_t v) {
  ABSL_INTERNAL_UNALIGNED_STORE16(p, FromHost16(v));
}

inline uint32_t Load32(absl::Nonnull<const void *> p) {
  return ToHost32(ABSL_INTERNAL_UNALIGNED_LOAD32(p));
}

inline void Store32(absl::Nonnull<void *> p, uint32_t v) {
  ABSL_INTERNAL_UNALIGNED_STORE32(p, FromHost32(v));
}

inline uint64_t Load64(absl::Nonnull<const void *> p) {
  return ToHost64(ABSL_INTERNAL_UNALIGNED_LOAD64(p));
}

inline void Store64(absl::Nonnull<void *> p, uint64_t v) {
  ABSL_INTERNAL_UNALIGNED_STORE64(p, FromHost64(v));
}

}  // namespace little_endian

// Utilities to convert numbers between the current hosts's native byte
// order and big-endian byte order (same as network byte order)
//
// Load/Store methods are alignment safe
namespace big_endian {
#ifdef ABSL_IS_LITTLE_ENDIAN

inline uint16_t FromHost16(uint16_t x) { return gbswap_16(x); }
inline uint16_t ToHost16(uint16_t x) { return gbswap_16(x); }

inline uint32_t FromHost32(uint32_t x) { return gbswap_32(x); }
inline uint32_t ToHost32(uint32_t x) { return gbswap_32(x); }

inline uint64_t FromHost64(uint64_t x) { return gbswap_64(x); }
inline uint64_t ToHost64(uint64_t x) { return gbswap_64(x); }

inline constexpr bool IsLittleEndian() { return true; }

#elif defined ABSL_IS_BIG_ENDIAN

inline uint16_t FromHost16(uint16_t x) { return x; }
inline uint16_t ToHost16(uint16_t x) { return x; }

inline uint32_t FromHost32(uint32_t x) { return x; }
inline uint32_t ToHost32(uint32_t x) { return x; }

inline uint64_t FromHost64(uint64_t x) { return x; }
inline uint64_t ToHost64(uint64_t x) { return x; }

inline constexpr bool IsLittleEndian() { return false; }

#endif /* ENDIAN */

inline uint8_t FromHost(uint8_t x) { return x; }
inline uint16_t FromHost(uint16_t x) { return FromHost16(x); }
inline uint32_t FromHost(uint32_t x) { return FromHost32(x); }
inline uint64_t FromHost(uint64_t x) { return FromHost64(x); }
inline uint8_t ToHost(uint8_t x) { return x; }
inline uint16_t ToHost(uint16_t x) { return ToHost16(x); }
inline uint32_t ToHost(uint32_t x) { return ToHost32(x); }
inline uint64_t ToHost(uint64_t x) { return ToHost64(x); }

inline int8_t FromHost(int8_t x) { return x; }
inline int16_t FromHost(int16_t x) {
  return bit_cast<int16_t>(FromHost16(bit_cast<uint16_t>(x)));
}
inline int32_t FromHost(int32_t x) {
  return bit_cast<int32_t>(FromHost32(bit_cast<uint32_t>(x)));
}
inline int64_t FromHost(int64_t x) {
  return bit_cast<int64_t>(FromHost64(bit_cast<uint64_t>(x)));
}
inline int8_t ToHost(int8_t x) { return x; }
inline int16_t ToHost(int16_t x) {
  return bit_cast<int16_t>(ToHost16(bit_cast<uint16_t>(x)));
}
inline int32_t ToHost(int32_t x) {
  return bit_cast<int32_t>(ToHost32(bit_cast<uint32_t>(x)));
}
inline int64_t ToHost(int64_t x) {
  return bit_cast<int64_t>(ToHost64(bit_cast<uint64_t>(x)));
}

// Functions to do unaligned loads and stores in big-endian order.
inline uint16_t Load16(absl::Nonnull<const void *> p) {
  return ToHost16(ABSL_INTERNAL_UNALIGNED_LOAD16(p));
}

inline void Store16(absl::Nonnull<void *> p, uint16_t v) {
  ABSL_INTERNAL_UNALIGNED_STORE16(p, FromHost16(v));
}

inline uint32_t Load32(absl::Nonnull<const void *> p) {
  return ToHost32(ABSL_INTERNAL_UNALIGNED_LOAD32(p));
}

inline void Store32(absl::Nonnull<void *>p, uint32_t v) {
  ABSL_INTERNAL_UNALIGNED_STORE32(p, FromHost32(v));
}

inline uint64_t Load64(absl::Nonnull<const void *> p) {
  return ToHost64(ABSL_INTERNAL_UNALIGNED_LOAD64(p));
}

inline void Store64(absl::Nonnull<void *> p, uint64_t v) {
  ABSL_INTERNAL_UNALIGNED_STORE64(p, FromHost64(v));
}

}  // namespace big_endian

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_ENDIAN_H_
