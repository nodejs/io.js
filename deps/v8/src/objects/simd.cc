// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/simd.h"

#include "src/base/cpu.h"
#include "src/codegen/cpu-features.h"
#include "src/objects/compressed-slots.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/smi-inl.h"

#ifdef _MSC_VER
// MSVC doesn't define SSE3. However, it does define AVX, and AVX implies SSE3.
#ifdef __AVX__
#ifndef __SSE3__
#define __SSE3__
#endif
#endif
#endif

#ifdef __SSE3__
#include <immintrin.h>
#endif

#ifdef V8_HOST_ARCH_ARM64
// We use Neon only on 64-bit ARM (because on 32-bit, some instructions and some
// types are not available). Note that ARM64 is guaranteed to have Neon.
#define NEON64
#include <arm_neon.h>
#endif

namespace v8 {
namespace internal {

namespace {

enum class SimdKinds { kSSE, kNeon, kAVX2, kNone };

inline SimdKinds get_vectorization_kind() {
#ifdef __SSE3__
#if defined(V8_TARGET_ARCH_IA32) || defined(V8_TARGET_ARCH_X64)
  bool has_avx2 = CpuFeatures::IsSupported(AVX2);
#else
  bool has_avx2 = false;
#endif
  if (has_avx2) {
    return SimdKinds::kAVX2;
  } else {
    // No need for a runtime check since we do not support x86/x64 CPUs without
    // SSE3.
    return SimdKinds::kSSE;
  }
#elif defined(NEON64)
  // No need for a runtime check since all Arm64 CPUs have Neon.
  return SimdKinds::kNeon;
#else
  return SimdKinds::kNone;
#endif
}

// Searches for |search_element| in |array| using a simple non-vectorized linear
// search. This is used as a fall-back when SIMD are not available, and to
// process the end of arrays than SIMD cannot process.
template <typename T>
inline uintptr_t slow_search(T* array, uintptr_t array_len, uintptr_t index,
                             T search_element) {
  for (; index < array_len; index++) {
    if (array[index] == search_element) {
      return index;
    }
  }
  return -1;
}

#ifdef NEON64
// extract_first_nonzero_index returns the first non-zero index in |v|. |v| is a
// Neon vector that can be either 32x4 (the return is then 0, 1, 2 or 3) or 64x2
// (the return is then 0 or 1). This is more or less equivalent to doing a
// movemask followed by a tzcnt on Intel.
//
// The input |v| should be a vector of -1 or 0 (for instance {0, 0},
// {0, -1, 0, -1}, {0, -1, 0, 0}), where -1 represents a match (and 0 a
// non-match), that was obtained by doing a vceqq. This function extract the
// index of the first non-zero item of the vector. To do so, we "and" the vector
// with {4, 3, 2, 1} (each number is "4 - the index of the item it's in"), which
// produces a vector of "indices or 0". Then, we extract the maximum of this
// vector, which is the index of the 1st match. An example:
//
//   v = {-1, 0, 0, -1}
//   mask = {4, 3, 2, 1}
//   v & mask = {4, 0, 0, 1}
//   max(v & mask) = 4
//   index of the first match = 4-max = 4-4 = 0
//

// With MSVC, uint32x4_t and uint64x2_t typedef to a union, where first member
// is uint64_t[2], and not uint32_t[4].
// C++ standard dictates that a union can only be initialized through its first
// member, which forces us to have uint64_t[2] for definition.
#if defined(_MSC_VER) && !defined(__clang__)
#define PACK32x4(w, x, y, z) \
  { ((w) + (uint64_t(x) << 32)), ((y) + (uint64_t(z) << 32)) }
#else
#define PACK32x4(w, x, y, z) \
  { (w), (x), (y), (z) }
#endif  // MSVC workaround

V8_ALLOW_UNUSED inline int extract_first_nonzero_index_uint32x4_t(
    uint32x4_t v) {
  uint32x4_t mask = PACK32x4(4, 3, 2, 1);
  mask = vandq_u32(mask, v);
  return 4 - vmaxvq_u32(mask);
}

inline int extract_first_nonzero_index_uint64x2_t(uint64x2_t v) {
  uint32x4_t mask =
      PACK32x4(2, 0, 1, 0);  // Could also be {2,2,1,1} or {0,2,0,1}
  mask = vandq_u32(mask, vreinterpretq_u32_u64(v));
  return 2 - vmaxvq_u32(mask);
}

inline int32_t reinterpret_vmaxvq_u64(uint64x2_t v) {
  return vmaxvq_u32(vreinterpretq_u32_u64(v));
}
#endif

#define VECTORIZED_LOOP_Neon(type_load, type_eq, set1, cmp, movemask)        \
  {                                                                          \
    constexpr int elems_in_vector = sizeof(type_load) / sizeof(T);           \
    type_load search_element_vec = set1(search_element);                     \
                                                                             \
    for (; index + elems_in_vector <= array_len; index += elems_in_vector) { \
      type_load vector = *reinterpret_cast<type_load*>(&array[index]);       \
      type_eq eq = cmp(vector, search_element_vec);                          \
      if (movemask(eq)) {                                                    \
        return index + extract_first_nonzero_index_##type_eq(eq);            \
      }                                                                      \
    }                                                                        \
  }

#define VECTORIZED_LOOP_x86(type_load, type_eq, set1, cmp, movemask, extract) \
  {                                                                           \
    constexpr int elems_in_vector = sizeof(type_load) / sizeof(T);            \
    type_load search_element_vec = set1(search_element);                      \
                                                                              \
    for (; index + elems_in_vector <= array_len; index += elems_in_vector) {  \
      type_load vector = *reinterpret_cast<type_load*>(&array[index]);        \
      type_eq eq = cmp(vector, search_element_vec);                           \
      int eq_mask = movemask(eq);                                             \
      if (eq_mask) {                                                          \
        return index + extract(eq_mask);                                      \
      }                                                                       \
    }                                                                         \
  }

// Uses SIMD to vectorize the search loop. This function should only be called
// for large-ish arrays. Note that nothing will break if |array_len| is less
// than vectorization_threshold: things will just be slower than necessary.
template <typename T>
inline uintptr_t fast_search_noavx(T* array, uintptr_t array_len,
                                   uintptr_t index, T search_element) {
  static constexpr bool is_uint32 =
      sizeof(T) == sizeof(uint32_t) && std::is_integral<T>::value;
  static constexpr bool is_uint64 =
      sizeof(T) == sizeof(uint64_t) && std::is_integral<T>::value;
  static constexpr bool is_double =
      sizeof(T) == sizeof(double) && std::is_floating_point<T>::value;

  static_assert(is_uint32 || is_uint64 || is_double);

#if !(defined(__SSE3__) || defined(NEON64))
  // No SIMD available.
  return slow_search(array, array_len, index, search_element);
#endif

#ifdef __SSE3__
  const int target_align = 16;
#elif defined(NEON64)
  const int target_align = 16;
#else
  const int target_align = 4;
  UNREACHABLE();
#endif

  // Scalar loop to reach desired alignment
  for (;
       index < array_len &&
       (reinterpret_cast<std::uintptr_t>(&(array[index])) % target_align) != 0;
       index++) {
    if (array[index] == search_element) {
      return index;
    }
  }

  // Inserting one of the vectorized loop
#ifdef __SSE3__
  if constexpr (is_uint32) {
#define MOVEMASK(x) _mm_movemask_ps(_mm_castsi128_ps(x))
#define EXTRACT(x) base::bits::CountTrailingZeros32(x)
    VECTORIZED_LOOP_x86(__m128i, __m128i, _mm_set1_epi32, _mm_cmpeq_epi32,
                        MOVEMASK, EXTRACT)
#undef MOVEMASK
#undef EXTRACT
  } else if constexpr (is_uint64) {
#define SET1(x) _mm_castsi128_ps(_mm_set1_epi64x(x))
#define CMP(a, b) _mm_cmpeq_pd(_mm_castps_pd(a), _mm_castps_pd(b))
#define EXTRACT(x) base::bits::CountTrailingZeros32(x)
    VECTORIZED_LOOP_x86(__m128, __m128d, SET1, CMP, _mm_movemask_pd, EXTRACT)
#undef SET1
#undef CMP
#undef EXTRACT
  } else if constexpr (is_double) {
#define EXTRACT(x) base::bits::CountTrailingZeros32(x)
    VECTORIZED_LOOP_x86(__m128d, __m128d, _mm_set1_pd, _mm_cmpeq_pd,
                        _mm_movemask_pd, EXTRACT)
#undef EXTRACT
  }
#elif defined(NEON64)
  if constexpr (is_uint32) {
    VECTORIZED_LOOP_Neon(uint32x4_t, uint32x4_t, vdupq_n_u32, vceqq_u32,
                         vmaxvq_u32)
  } else if constexpr (is_uint64) {
    VECTORIZED_LOOP_Neon(uint64x2_t, uint64x2_t, vdupq_n_u64, vceqq_u64,
                         reinterpret_vmaxvq_u64)
  } else if constexpr (is_double) {
    VECTORIZED_LOOP_Neon(float64x2_t, uint64x2_t, vdupq_n_f64, vceqq_f64,
                         reinterpret_vmaxvq_u64)
  }
#else
  UNREACHABLE();
#endif

  // The vectorized loop stops when there are not enough items left in the array
  // to fill a vector register. The slow_search function will take care of
  // iterating through the few remaining items.
  return slow_search(array, array_len, index, search_element);
}

#if defined(_MSC_VER) && defined(__clang__)
// Generating AVX2 code with Clang on Windows without the /arch:AVX2 flag does
// not seem possible at the moment.
#define IS_CLANG_WIN 1
#endif

// Since we don't compile with -mavx or -mavx2 (or /arch:AVX2 on MSVC), Clang
// and MSVC do not define __AVX__ nor __AVX2__. Thus, if __SSE3__ is defined, we
// generate the AVX2 code, and, at runtime, we'll decide to call it or not,
// depending on whether the CPU supports AVX2.
#if defined(__SSE3__) && !defined(_M_IX86) && !defined(IS_CLANG_WIN)
#ifdef _MSC_VER
#define TARGET_AVX2
#else
#define TARGET_AVX2 __attribute__((target("avx2")))
#endif
template <typename T>
TARGET_AVX2 inline uintptr_t fast_search_avx(T* array, uintptr_t array_len,
                                             uintptr_t index,
                                             T search_element) {
  static constexpr bool is_uint32 =
      sizeof(T) == sizeof(uint32_t) && std::is_integral<T>::value;
  static constexpr bool is_uint64 =
      sizeof(T) == sizeof(uint64_t) && std::is_integral<T>::value;
  static constexpr bool is_double =
      sizeof(T) == sizeof(double) && std::is_floating_point<T>::value;

  static_assert(is_uint32 || is_uint64 || is_double);

  const int target_align = 32;
  // Scalar loop to reach desired alignment
  for (;
       index < array_len &&
       (reinterpret_cast<std::uintptr_t>(&(array[index])) % target_align) != 0;
       index++) {
    if (array[index] == search_element) {
      return index;
    }
  }

  // Generating vectorized loop
  if constexpr (is_uint32) {
#define MOVEMASK(x) _mm256_movemask_ps(_mm256_castsi256_ps(x))
#define EXTRACT(x) base::bits::CountTrailingZeros32(x)
    VECTORIZED_LOOP_x86(__m256i, __m256i, _mm256_set1_epi32, _mm256_cmpeq_epi32,
                        MOVEMASK, EXTRACT)
#undef MOVEMASK
#undef EXTRACT
  } else if constexpr (is_uint64) {
#define MOVEMASK(x) _mm256_movemask_pd(_mm256_castsi256_pd(x))
#define EXTRACT(x) base::bits::CountTrailingZeros32(x)
    VECTORIZED_LOOP_x86(__m256i, __m256i, _mm256_set1_epi64x,
                        _mm256_cmpeq_epi64, MOVEMASK, EXTRACT)
#undef MOVEMASK
#undef EXTRACT
  } else if constexpr (is_double) {
#define CMP(a, b) _mm256_cmp_pd(a, b, _CMP_EQ_OQ)
#define EXTRACT(x) base::bits::CountTrailingZeros32(x)
    VECTORIZED_LOOP_x86(__m256d, __m256d, _mm256_set1_pd, CMP,
                        _mm256_movemask_pd, EXTRACT)
#undef CMP
#undef EXTRACT
  }

  // The vectorized loop stops when there are not enough items left in the array
  // to fill a vector register. The slow_search function will take care of
  // iterating through the few remaining items.
  return slow_search(array, array_len, index, search_element);
}

#undef TARGET_AVX2
#elif defined(IS_CLANG_WIN)
template <typename T>
inline uintptr_t fast_search_avx(T* array, uintptr_t array_len, uintptr_t index,
                                 T search_element) {
  // Falling back to SSE version
  return fast_search_noavx(array, array_len, index, search_element);
}
#else
template <typename T>
uintptr_t fast_search_avx(T* array, uintptr_t array_len, uintptr_t index,
                          T search_element) {
  UNREACHABLE();
}
#endif  // ifdef __SSE3__

#undef IS_CLANG_WIN
#undef VECTORIZED_LOOP_Neon
#undef VECTORIZED_LOOP_x86

template <typename T>
inline uintptr_t search(T* array, uintptr_t array_len, uintptr_t index,
                        T search_element) {
  if (get_vectorization_kind() == SimdKinds::kAVX2) {
    return fast_search_avx(array, array_len, index, search_element);
  } else {
    return fast_search_noavx(array, array_len, index, search_element);
  }
}

enum class ArrayIndexOfIncludesKind { DOUBLE, OBJECTORSMI };

// ArrayIndexOfIncludes only handles cases that can be efficiently
// vectorized:
//
//   * Searching for a Smi in a Smi array
//
//   * Searching for a Smi or Double in a Double array
//
//   * Searching for an object in an object array.
//
// Other cases should be dealt with either with the CSA builtin or with the
// inlined optimized code.
template <ArrayIndexOfIncludesKind kind>
Address ArrayIndexOfIncludes(Address array_start, uintptr_t array_len,
                             uintptr_t from_index, Address search_element) {
  if (array_len == 0) {
    return Smi::FromInt(-1).ptr();
  }

  if constexpr (kind == ArrayIndexOfIncludesKind::DOUBLE) {
    Tagged<FixedDoubleArray> fixed_array =
        FixedDoubleArray::cast(Tagged<Object>(array_start));
    double* array = static_cast<double*>(
        fixed_array->RawField(FixedDoubleArray::OffsetOfElementAt(0))
            .ToVoidPtr());

    double search_num;
    if (IsSmi(Tagged<Object>(search_element))) {
      search_num = Tagged<Object>(search_element).ToSmi().value();
    } else {
      DCHECK(IsHeapNumber(Tagged<Object>(search_element)));
      search_num = HeapNumber::cast(Tagged<Object>(search_element))->value();
    }

    DCHECK(!std::isnan(search_num));

    if (reinterpret_cast<uintptr_t>(array) % sizeof(double) != 0) {
      // Slow scalar search for unaligned double array.
      for (; from_index < array_len; from_index++) {
        if (fixed_array->is_the_hole(static_cast<int>(from_index))) {
          // |search_num| cannot be NaN, so there is no need to check against
          // holes.
          continue;
        }
        if (fixed_array->get_scalar(static_cast<int>(from_index)) ==
            search_num) {
          return from_index;
        }
      }
      return Smi::FromInt(-1).ptr();
    }

    return search<double>(array, array_len, from_index, search_num);
  }

  if constexpr (kind == ArrayIndexOfIncludesKind::OBJECTORSMI) {
    Tagged<FixedArray> fixed_array =
        FixedArray::cast(Tagged<Object>(array_start));
    Tagged_t* array = static_cast<Tagged_t*>(
        fixed_array->RawFieldOfFirstElement().ToVoidPtr());

    DCHECK(!IsHeapNumber(Tagged<Object>(search_element)));
    DCHECK(!IsBigInt(Tagged<Object>(search_element)));
    DCHECK(!IsString(Tagged<Object>(search_element)));

    return search<Tagged_t>(array, array_len, from_index,
                            static_cast<Tagged_t>(search_element));
  }
}

}  // namespace

uintptr_t ArrayIndexOfIncludesSmiOrObject(Address array_start,
                                          uintptr_t array_len,
                                          uintptr_t from_index,
                                          Address search_element) {
  return ArrayIndexOfIncludes<ArrayIndexOfIncludesKind::OBJECTORSMI>(
      array_start, array_len, from_index, search_element);
}

uintptr_t ArrayIndexOfIncludesDouble(Address array_start, uintptr_t array_len,
                                     uintptr_t from_index,
                                     Address search_element) {
  return ArrayIndexOfIncludes<ArrayIndexOfIncludesKind::DOUBLE>(
      array_start, array_len, from_index, search_element);
}

#ifdef NEON64
#undef NEON64
#endif

}  // namespace internal
}  // namespace v8
