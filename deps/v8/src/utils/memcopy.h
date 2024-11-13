// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UTILS_MEMCOPY_H_
#define V8_UTILS_MEMCOPY_H_

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "src/base/bits.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

using Address = uintptr_t;

// ----------------------------------------------------------------------------
// Generated memcpy/memmove for ia32 and arm.

void init_memcopy_functions();

#if defined(V8_TARGET_ARCH_IA32)
// Limit below which the extra overhead of the MemCopy function is likely
// to outweigh the benefits of faster copying.
const size_t kMinComplexMemCopy = 64;

// Copy memory area. No restrictions.
V8_EXPORT_PRIVATE void MemMove(void* dest, const void* src, size_t size);
using MemMoveFunction = void (*)(void* dest, const void* src, size_t size);

// Keep the distinction of "move" vs. "copy" for the benefit of other
// architectures.
V8_INLINE void MemCopy(void* dest, const void* src, size_t size) {
  MemMove(dest, src, size);
}
#elif defined(V8_HOST_ARCH_ARM)
using MemCopyUint8Function = void (*)(uint8_t* dest, const uint8_t* src,
                                      size_t size);
V8_EXPORT_PRIVATE extern MemCopyUint8Function memcopy_uint8_function;
V8_INLINE void MemCopyUint8Wrapper(uint8_t* dest, const uint8_t* src,
                                   size_t chars) {
  memcpy(dest, src, chars);
}
// For values < 16, the assembler function is slower than the inlined C code.
const size_t kMinComplexMemCopy = 16;
V8_INLINE void MemCopy(void* dest, const void* src, size_t size) {
  (*memcopy_uint8_function)(reinterpret_cast<uint8_t*>(dest),
                            reinterpret_cast<const uint8_t*>(src), size);
}
V8_EXPORT_PRIVATE V8_INLINE void MemMove(void* dest, const void* src,
                                         size_t size) {
  memmove(dest, src, size);
}

// For values < 12, the assembler function is slower than the inlined C code.
const int kMinComplexConvertMemCopy = 12;
#else
#if defined(V8_OPTIMIZE_WITH_NEON)
// We intentionally use misaligned read/writes for NEON intrinsics, disable
// alignment sanitization explicitly.
// Overlapping writes help to save instructions, e.g. doing 2 two-byte writes
// instead 3 one-byte write for count == 3.
template <typename IntType>
V8_INLINE V8_CLANG_NO_SANITIZE("alignment") void OverlappingWrites(
    void* dst, const void* src, size_t count) {
  *reinterpret_cast<IntType*>(dst) = *reinterpret_cast<const IntType*>(src);
  *reinterpret_cast<IntType*>(static_cast<uint8_t*>(dst) + count -
                              sizeof(IntType)) =
      *reinterpret_cast<const IntType*>(static_cast<const uint8_t*>(src) +
                                        count - sizeof(IntType));
}

V8_CLANG_NO_SANITIZE("alignment")
inline void MemCopy(void* dst, const void* src, size_t count) {
  auto* dst_u = static_cast<uint8_t*>(dst);
  const auto* src_u = static_cast<const uint8_t*>(src);
  // Common cases. Handle before doing clz.
  if (count == 0) {
    return;
  }
  if (count == 1) {
    *dst_u = *src_u;
    return;
  }
  const size_t order =
      sizeof(count) * CHAR_BIT - base::bits::CountLeadingZeros(count - 1);
  switch (order) {
    case 1:  // count: [2, 2]
      *reinterpret_cast<uint16_t*>(dst_u) =
          *reinterpret_cast<const uint16_t*>(src_u);
      return;
    case 2:  // count: [3, 4]
      OverlappingWrites<uint16_t>(dst_u, src_u, count);
      return;
    case 3:  // count: [5, 8]
      OverlappingWrites<uint32_t>(dst_u, src_u, count);
      return;
    case 4:  // count: [9, 16]
      OverlappingWrites<uint64_t>(dst_u, src_u, count);
      return;
    case 5:  // count: [17, 32]
      vst1q_u8(dst_u, vld1q_u8(src_u));
      vst1q_u8(dst_u + count - sizeof(uint8x16_t),
               vld1q_u8(src_u + count - sizeof(uint8x16_t)));
      return;
    default:  // count: [33, ...]
      vst1q_u8(dst_u, vld1q_u8(src_u));
      for (size_t i = count % sizeof(uint8x16_t); i < count;
           i += sizeof(uint8x16_t)) {
        vst1q_u8(dst_u + i, vld1q_u8(src_u + i));
      }
      return;
  }
}
#else  // !defined(V8_OPTIMIZE_WITH_NEON)
// Copy memory area to disjoint memory area.
inline void MemCopy(void* dest, const void* src, size_t size) {
  // Fast path for small sizes. The compiler will expand the {memcpy} for small
  // fixed sizes to a sequence of move instructions. This avoids the overhead of
  // the general {memcpy} function.
  switch (size) {
#define CASE(N)           \
  case N:                 \
    memcpy(dest, src, N); \
    return;
    CASE(1)
    CASE(2)
    CASE(3)
    CASE(4)
    CASE(5)
    CASE(6)
    CASE(7)
    CASE(8)
    CASE(9)
    CASE(10)
    CASE(11)
    CASE(12)
    CASE(13)
    CASE(14)
    CASE(15)
    CASE(16)
#undef CASE
    default:
      memcpy(dest, src, size);
      return;
  }
}
#endif  // !defined(V8_OPTIMIZE_WITH_NEON)
#if V8_TARGET_BIG_ENDIAN
inline void MemCopyAndSwitchEndianness(void* dst, void* src,
                                       size_t num_elements,
                                       size_t element_size) {
#define COPY_LOOP(type, reverse)                            \
  {                                                         \
    for (uint32_t i = 0; i < num_elements; i++) {           \
      type t;                                               \
      type* s = reinterpret_cast<type*>(src) + i;           \
      type* d = reinterpret_cast<type*>(dst) + i;           \
      memcpy(&t, reinterpret_cast<void*>(s), element_size); \
      t = reverse(t);                                       \
      memcpy(reinterpret_cast<void*>(d), &t, element_size); \
    }                                                       \
    return;                                                 \
  }

  switch (element_size) {
    case 1:
      MemCopy(dst, src, num_elements);
      return;
    case 2:
      COPY_LOOP(uint16_t, ByteReverse16);
    case 4:
      COPY_LOOP(uint32_t, ByteReverse32);
    case 8:
      COPY_LOOP(uint64_t, ByteReverse64);
    default:
      UNREACHABLE();
  }
#undef COPY_LOOP
}
#endif
V8_EXPORT_PRIVATE inline void MemMove(void* dest, const void* src,
                                      size_t size) {
  // Fast path for small sizes. The compiler will expand the {memmove} for small
  // fixed sizes to a sequence of move instructions. This avoids the overhead of
  // the general {memmove} function.
  switch (size) {
#define CASE(N)            \
  case N:                  \
    memmove(dest, src, N); \
    return;
    CASE(1)
    CASE(2)
    CASE(3)
    CASE(4)
    CASE(5)
    CASE(6)
    CASE(7)
    CASE(8)
    CASE(9)
    CASE(10)
    CASE(11)
    CASE(12)
    CASE(13)
    CASE(14)
    CASE(15)
    CASE(16)
#undef CASE
    default:
      memmove(dest, src, size);
      return;
  }
}
const size_t kMinComplexMemCopy = 8;
#endif  // V8_TARGET_ARCH_IA32

// Copies words from |src| to |dst|. The data spans must not overlap.
// |src| and |dst| must be TWord-size aligned.
template <size_t kBlockCopyLimit, typename T>
inline void CopyImpl(T* dst_ptr, const T* src_ptr, size_t count) {
  constexpr int kTWordSize = sizeof(T);
#ifdef DEBUG
  Address dst = reinterpret_cast<Address>(dst_ptr);
  Address src = reinterpret_cast<Address>(src_ptr);
  DCHECK(IsAligned(dst, kTWordSize));
  DCHECK(IsAligned(src, kTWordSize));
  DCHECK(((src <= dst) && ((src + count * kTWordSize) <= dst)) ||
         ((dst <= src) && ((dst + count * kTWordSize) <= src)));
#endif
  if (count == 0) return;

  // Use block copying MemCopy if the segment we're copying is
  // enough to justify the extra call/setup overhead.
  if (count < kBlockCopyLimit) {
    do {
      count--;
      *dst_ptr++ = *src_ptr++;
    } while (count > 0);
  } else {
    MemCopy(dst_ptr, src_ptr, count * kTWordSize);
  }
}

// Copies kSystemPointerSize-sized words from |src| to |dst|. The data spans
// must not overlap. |src| and |dst| must be kSystemPointerSize-aligned.
inline void CopyWords(Address dst, const Address src, size_t num_words) {
  static const size_t kBlockCopyLimit = 16;
  CopyImpl<kBlockCopyLimit>(reinterpret_cast<Address*>(dst),
                            reinterpret_cast<const Address*>(src), num_words);
}

// Copies data from |src| to |dst|.  The data spans must not overlap.
template <typename T>
inline void CopyBytes(T* dst, const T* src, size_t num_bytes) {
  static_assert(sizeof(T) == 1);
  if (num_bytes == 0) return;
  CopyImpl<kMinComplexMemCopy>(dst, src, num_bytes);
}

inline void MemsetUint32(uint32_t* dest, uint32_t value, size_t counter) {
#if V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64
#define STOS "stosl"
#endif

#if defined(MEMORY_SANITIZER)
  // MemorySanitizer does not understand inline assembly.
#undef STOS
#endif

#if defined(__GNUC__) && defined(STOS)
  asm volatile(
      "cld;"
      "rep ; " STOS
      : "+&c"(counter), "+&D"(dest)
      : "a"(value)
      : "memory", "cc");
#else
  for (size_t i = 0; i < counter; i++) {
    dest[i] = value;
  }
#endif

#undef STOS
}

inline void MemsetPointer(Address* dest, Address value, size_t counter) {
#if V8_HOST_ARCH_IA32
#define STOS "stosl"
#elif V8_HOST_ARCH_X64
#define STOS "stosq"
#endif

#if defined(MEMORY_SANITIZER)
  // MemorySanitizer does not understand inline assembly.
#undef STOS
#endif

#if defined(__GNUC__) && defined(STOS)
  asm volatile(
      "cld;"
      "rep ; " STOS
      : "+&c"(counter), "+&D"(dest)
      : "a"(value)
      : "memory", "cc");
#else
  for (size_t i = 0; i < counter; i++) {
    dest[i] = value;
  }
#endif

#undef STOS
}

template <typename T, typename U>
inline void MemsetPointer(T** dest, U* value, size_t counter) {
#ifdef DEBUG
  T* a = nullptr;
  U* b = nullptr;
  a = b;  // Fake assignment to check assignability.
  USE(a);
#endif  // DEBUG
  MemsetPointer(reinterpret_cast<Address*>(dest),
                reinterpret_cast<Address>(value), counter);
}

template <typename T>
inline void MemsetPointer(T** dest, std::nullptr_t, size_t counter) {
  MemsetPointer(reinterpret_cast<Address*>(dest), Address{0}, counter);
}

// Copy from 8bit/16bit chars to 8bit/16bit chars. Values are zero-extended if
// needed. Ranges are not allowed to overlap.
// The separate declaration is needed for the V8_NONNULL, which is not allowed
// on a definition.
template <typename SrcType, typename DstType>
void CopyChars(DstType* dst, const SrcType* src, size_t count) V8_NONNULL(1, 2);

template <typename SrcType, typename DstType>
void CopyChars(DstType* dst, const SrcType* src, size_t count) {
  static_assert(std::is_integral<SrcType>::value);
  static_assert(std::is_integral<DstType>::value);
  using SrcTypeUnsigned = typename std::make_unsigned<SrcType>::type;
  using DstTypeUnsigned = typename std::make_unsigned<DstType>::type;

#ifdef DEBUG
  // Check for no overlap, otherwise {std::copy_n} cannot be used.
  Address src_start = reinterpret_cast<Address>(src);
  Address src_end = src_start + count * sizeof(SrcType);
  Address dst_start = reinterpret_cast<Address>(dst);
  Address dst_end = dst_start + count * sizeof(DstType);
  DCHECK(src_end <= dst_start || dst_end <= src_start);
#endif

  auto* dst_u = reinterpret_cast<DstTypeUnsigned*>(dst);
  auto* src_u = reinterpret_cast<const SrcTypeUnsigned*>(src);

#if defined(V8_OPTIMIZE_WITH_NEON)
  if constexpr (sizeof(DstType) == 1 && sizeof(SrcType) == 1) {
    // Use simd optimized memcpy.
    MemCopy(dst, src, count);
    return;
  }
#endif  // defined(V8_OPTIMIZE_WITH_NEON)

  // Especially Atom CPUs profit from this explicit instantiation for small
  // counts. This gives up to 20 percent improvement for microbenchmarks such as
  // joining an array of small integers (2019-10-16).
  switch (count) {
#define CASE(N)                   \
  case N:                         \
    std::copy_n(src_u, N, dst_u); \
    return;
    CASE(1)
    CASE(2)
    CASE(3)
    CASE(4)
    CASE(5)
    CASE(6)
    CASE(7)
    CASE(8)
    CASE(9)
    CASE(10)
    CASE(11)
    CASE(12)
    CASE(13)
    CASE(14)
    CASE(15)
    CASE(16)
#undef CASE
    default:
      std::copy_n(src_u, count, dst_u);
      return;
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_UTILS_MEMCOPY_H_
