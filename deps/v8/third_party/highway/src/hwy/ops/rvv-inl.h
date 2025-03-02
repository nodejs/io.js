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

// RISC-V V vectors (length not known at compile time).
// External include guard in highway.h - see comment there.

#include <riscv_vector.h>

#include "hwy/ops/shared-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

// Support for vfloat16m*_t and PromoteTo/DemoteTo.
#ifdef __riscv_zvfhmin
#define HWY_RVV_HAVE_F16C 1
#else
#define HWY_RVV_HAVE_F16C 0
#endif

template <class V>
struct DFromV_t {};  // specialized in macros
template <class V>
using DFromV = typename DFromV_t<RemoveConst<V>>::type;

template <class V>
using TFromV = TFromD<DFromV<V>>;

template <typename T, size_t N, int kPow2>
constexpr size_t MLenFromD(Simd<T, N, kPow2> /* tag */) {
  // Returns divisor = type bits / LMUL. Folding *8 into the ScaleByPower
  // argument enables fractional LMUL < 1. Limit to 64 because that is the
  // largest value for which vbool##_t are defined.
  return HWY_MIN(64, sizeof(T) * 8 * 8 / detail::ScaleByPower(8, kPow2));
}

namespace detail {

template <class D>
class AdjustSimdTagToMinVecPow2_t {};

template <typename T, size_t N, int kPow2>
class AdjustSimdTagToMinVecPow2_t<Simd<T, N, kPow2>> {
 private:
  using D = Simd<T, N, kPow2>;
  static constexpr int kMinVecPow2 =
      -3 + static_cast<int>(FloorLog2(sizeof(T)));
  static constexpr size_t kNumMaxLanes = HWY_MAX_LANES_D(D);
  static constexpr int kNewPow2 = HWY_MAX(kPow2, kMinVecPow2);
  static constexpr size_t kNewN = D::template NewN<kNewPow2, kNumMaxLanes>();

 public:
  using type = Simd<T, kNewN, kNewPow2>;
};

template <class D>
using AdjustSimdTagToMinVecPow2 =
    typename AdjustSimdTagToMinVecPow2_t<RemoveConst<D>>::type;

}  // namespace detail

// ================================================== MACROS

// Generate specializations and function definitions using X macros. Although
// harder to read and debug, writing everything manually is too bulky.

namespace detail {  // for code folding

// For all mask sizes MLEN: (1/Nth of a register, one bit per lane)
// The first three arguments are arbitrary SEW, LMUL, SHIFT such that
// SEW >> SHIFT = MLEN.
#define HWY_RVV_FOREACH_B(X_MACRO, NAME, OP) \
  X_MACRO(64, 0, 64, NAME, OP)               \
  X_MACRO(32, 0, 32, NAME, OP)               \
  X_MACRO(16, 0, 16, NAME, OP)               \
  X_MACRO(8, 0, 8, NAME, OP)                 \
  X_MACRO(8, 1, 4, NAME, OP)                 \
  X_MACRO(8, 2, 2, NAME, OP)                 \
  X_MACRO(8, 3, 1, NAME, OP)

// For given SEW, iterate over one of LMULS: _TRUNC, _EXT, _ALL. This allows
// reusing type lists such as HWY_RVV_FOREACH_U for _ALL (the usual case) or
// _EXT (for Combine). To achieve this, we HWY_CONCAT with the LMULS suffix.
//
// Precompute SEW/LMUL => MLEN to allow token-pasting the result. For the same
// reason, also pass the double-width and half SEW and LMUL (suffixed D and H,
// respectively). "__" means there is no corresponding LMUL (e.g. LMULD for m8).
// Args: BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, MLEN, NAME, OP

// LMULS = _TRUNC: truncatable (not the smallest LMUL)
#define HWY_RVV_FOREACH_08_TRUNC(X_MACRO, BASE, CHAR, NAME, OP)            \
  X_MACRO(BASE, CHAR, 8, 16, __, mf4, mf2, mf8, -2, /*MLEN=*/32, NAME, OP) \
  X_MACRO(BASE, CHAR, 8, 16, __, mf2, m1, mf4, -1, /*MLEN=*/16, NAME, OP)  \
  X_MACRO(BASE, CHAR, 8, 16, __, m1, m2, mf2, 0, /*MLEN=*/8, NAME, OP)     \
  X_MACRO(BASE, CHAR, 8, 16, __, m2, m4, m1, 1, /*MLEN=*/4, NAME, OP)      \
  X_MACRO(BASE, CHAR, 8, 16, __, m4, m8, m2, 2, /*MLEN=*/2, NAME, OP)      \
  X_MACRO(BASE, CHAR, 8, 16, __, m8, __, m4, 3, /*MLEN=*/1, NAME, OP)

#define HWY_RVV_FOREACH_16_TRUNC(X_MACRO, BASE, CHAR, NAME, OP)           \
  X_MACRO(BASE, CHAR, 16, 32, 8, mf2, m1, mf4, -1, /*MLEN=*/32, NAME, OP) \
  X_MACRO(BASE, CHAR, 16, 32, 8, m1, m2, mf2, 0, /*MLEN=*/16, NAME, OP)   \
  X_MACRO(BASE, CHAR, 16, 32, 8, m2, m4, m1, 1, /*MLEN=*/8, NAME, OP)     \
  X_MACRO(BASE, CHAR, 16, 32, 8, m4, m8, m2, 2, /*MLEN=*/4, NAME, OP)     \
  X_MACRO(BASE, CHAR, 16, 32, 8, m8, __, m4, 3, /*MLEN=*/2, NAME, OP)

#define HWY_RVV_FOREACH_32_TRUNC(X_MACRO, BASE, CHAR, NAME, OP)          \
  X_MACRO(BASE, CHAR, 32, 64, 16, m1, m2, mf2, 0, /*MLEN=*/32, NAME, OP) \
  X_MACRO(BASE, CHAR, 32, 64, 16, m2, m4, m1, 1, /*MLEN=*/16, NAME, OP)  \
  X_MACRO(BASE, CHAR, 32, 64, 16, m4, m8, m2, 2, /*MLEN=*/8, NAME, OP)   \
  X_MACRO(BASE, CHAR, 32, 64, 16, m8, __, m4, 3, /*MLEN=*/4, NAME, OP)

#define HWY_RVV_FOREACH_64_TRUNC(X_MACRO, BASE, CHAR, NAME, OP)         \
  X_MACRO(BASE, CHAR, 64, __, 32, m2, m4, m1, 1, /*MLEN=*/32, NAME, OP) \
  X_MACRO(BASE, CHAR, 64, __, 32, m4, m8, m2, 2, /*MLEN=*/16, NAME, OP) \
  X_MACRO(BASE, CHAR, 64, __, 32, m8, __, m4, 3, /*MLEN=*/8, NAME, OP)

// LMULS = _DEMOTE: can demote from SEW*LMUL to SEWH*LMULH.
#define HWY_RVV_FOREACH_08_DEMOTE(X_MACRO, BASE, CHAR, NAME, OP)           \
  X_MACRO(BASE, CHAR, 8, 16, __, mf4, mf2, mf8, -2, /*MLEN=*/32, NAME, OP) \
  X_MACRO(BASE, CHAR, 8, 16, __, mf2, m1, mf4, -1, /*MLEN=*/16, NAME, OP)  \
  X_MACRO(BASE, CHAR, 8, 16, __, m1, m2, mf2, 0, /*MLEN=*/8, NAME, OP)     \
  X_MACRO(BASE, CHAR, 8, 16, __, m2, m4, m1, 1, /*MLEN=*/4, NAME, OP)      \
  X_MACRO(BASE, CHAR, 8, 16, __, m4, m8, m2, 2, /*MLEN=*/2, NAME, OP)      \
  X_MACRO(BASE, CHAR, 8, 16, __, m8, __, m4, 3, /*MLEN=*/1, NAME, OP)

#define HWY_RVV_FOREACH_16_DEMOTE(X_MACRO, BASE, CHAR, NAME, OP)           \
  X_MACRO(BASE, CHAR, 16, 32, 8, mf4, mf2, mf8, -2, /*MLEN=*/64, NAME, OP) \
  X_MACRO(BASE, CHAR, 16, 32, 8, mf2, m1, mf4, -1, /*MLEN=*/32, NAME, OP)  \
  X_MACRO(BASE, CHAR, 16, 32, 8, m1, m2, mf2, 0, /*MLEN=*/16, NAME, OP)    \
  X_MACRO(BASE, CHAR, 16, 32, 8, m2, m4, m1, 1, /*MLEN=*/8, NAME, OP)      \
  X_MACRO(BASE, CHAR, 16, 32, 8, m4, m8, m2, 2, /*MLEN=*/4, NAME, OP)      \
  X_MACRO(BASE, CHAR, 16, 32, 8, m8, __, m4, 3, /*MLEN=*/2, NAME, OP)

#define HWY_RVV_FOREACH_32_DEMOTE(X_MACRO, BASE, CHAR, NAME, OP)           \
  X_MACRO(BASE, CHAR, 32, 64, 16, mf2, m1, mf4, -1, /*MLEN=*/64, NAME, OP) \
  X_MACRO(BASE, CHAR, 32, 64, 16, m1, m2, mf2, 0, /*MLEN=*/32, NAME, OP)   \
  X_MACRO(BASE, CHAR, 32, 64, 16, m2, m4, m1, 1, /*MLEN=*/16, NAME, OP)    \
  X_MACRO(BASE, CHAR, 32, 64, 16, m4, m8, m2, 2, /*MLEN=*/8, NAME, OP)     \
  X_MACRO(BASE, CHAR, 32, 64, 16, m8, __, m4, 3, /*MLEN=*/4, NAME, OP)

#define HWY_RVV_FOREACH_64_DEMOTE(X_MACRO, BASE, CHAR, NAME, OP)         \
  X_MACRO(BASE, CHAR, 64, __, 32, m1, m2, mf2, 0, /*MLEN=*/64, NAME, OP) \
  X_MACRO(BASE, CHAR, 64, __, 32, m2, m4, m1, 1, /*MLEN=*/32, NAME, OP)  \
  X_MACRO(BASE, CHAR, 64, __, 32, m4, m8, m2, 2, /*MLEN=*/16, NAME, OP)  \
  X_MACRO(BASE, CHAR, 64, __, 32, m8, __, m4, 3, /*MLEN=*/8, NAME, OP)

// LMULS = _LE2: <= 2
#define HWY_RVV_FOREACH_08_LE2(X_MACRO, BASE, CHAR, NAME, OP)              \
  X_MACRO(BASE, CHAR, 8, 16, __, mf8, mf4, __, -3, /*MLEN=*/64, NAME, OP)  \
  X_MACRO(BASE, CHAR, 8, 16, __, mf4, mf2, mf8, -2, /*MLEN=*/32, NAME, OP) \
  X_MACRO(BASE, CHAR, 8, 16, __, mf2, m1, mf4, -1, /*MLEN=*/16, NAME, OP)  \
  X_MACRO(BASE, CHAR, 8, 16, __, m1, m2, mf2, 0, /*MLEN=*/8, NAME, OP)     \
  X_MACRO(BASE, CHAR, 8, 16, __, m2, m4, m1, 1, /*MLEN=*/4, NAME, OP)

#define HWY_RVV_FOREACH_16_LE2(X_MACRO, BASE, CHAR, NAME, OP)              \
  X_MACRO(BASE, CHAR, 16, 32, 8, mf4, mf2, mf8, -2, /*MLEN=*/64, NAME, OP) \
  X_MACRO(BASE, CHAR, 16, 32, 8, mf2, m1, mf4, -1, /*MLEN=*/32, NAME, OP)  \
  X_MACRO(BASE, CHAR, 16, 32, 8, m1, m2, mf2, 0, /*MLEN=*/16, NAME, OP)    \
  X_MACRO(BASE, CHAR, 16, 32, 8, m2, m4, m1, 1, /*MLEN=*/8, NAME, OP)

#define HWY_RVV_FOREACH_32_LE2(X_MACRO, BASE, CHAR, NAME, OP)              \
  X_MACRO(BASE, CHAR, 32, 64, 16, mf2, m1, mf4, -1, /*MLEN=*/64, NAME, OP) \
  X_MACRO(BASE, CHAR, 32, 64, 16, m1, m2, mf2, 0, /*MLEN=*/32, NAME, OP)   \
  X_MACRO(BASE, CHAR, 32, 64, 16, m2, m4, m1, 1, /*MLEN=*/16, NAME, OP)

#define HWY_RVV_FOREACH_64_LE2(X_MACRO, BASE, CHAR, NAME, OP)            \
  X_MACRO(BASE, CHAR, 64, __, 32, m1, m2, mf2, 0, /*MLEN=*/64, NAME, OP) \
  X_MACRO(BASE, CHAR, 64, __, 32, m2, m4, m1, 1, /*MLEN=*/32, NAME, OP)

// LMULS = _EXT: not the largest LMUL
#define HWY_RVV_FOREACH_08_EXT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_08_LE2(X_MACRO, BASE, CHAR, NAME, OP)       \
  X_MACRO(BASE, CHAR, 8, 16, __, m4, m8, m2, 2, /*MLEN=*/2, NAME, OP)

#define HWY_RVV_FOREACH_16_EXT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_16_LE2(X_MACRO, BASE, CHAR, NAME, OP)       \
  X_MACRO(BASE, CHAR, 16, 32, 8, m4, m8, m2, 2, /*MLEN=*/4, NAME, OP)

#define HWY_RVV_FOREACH_32_EXT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_32_LE2(X_MACRO, BASE, CHAR, NAME, OP)       \
  X_MACRO(BASE, CHAR, 32, 64, 16, m4, m8, m2, 2, /*MLEN=*/8, NAME, OP)

#define HWY_RVV_FOREACH_64_EXT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_64_LE2(X_MACRO, BASE, CHAR, NAME, OP)       \
  X_MACRO(BASE, CHAR, 64, __, 32, m4, m8, m2, 2, /*MLEN=*/16, NAME, OP)

// LMULS = _ALL (2^MinPow2() <= LMUL <= 8)
#define HWY_RVV_FOREACH_08_ALL(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_08_EXT(X_MACRO, BASE, CHAR, NAME, OP)       \
  X_MACRO(BASE, CHAR, 8, 16, __, m8, __, m4, 3, /*MLEN=*/1, NAME, OP)

#define HWY_RVV_FOREACH_16_ALL(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_16_EXT(X_MACRO, BASE, CHAR, NAME, OP)       \
  X_MACRO(BASE, CHAR, 16, 32, 8, m8, __, m4, 3, /*MLEN=*/2, NAME, OP)

#define HWY_RVV_FOREACH_32_ALL(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_32_EXT(X_MACRO, BASE, CHAR, NAME, OP)       \
  X_MACRO(BASE, CHAR, 32, 64, 16, m8, __, m4, 3, /*MLEN=*/4, NAME, OP)

#define HWY_RVV_FOREACH_64_ALL(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_64_EXT(X_MACRO, BASE, CHAR, NAME, OP)       \
  X_MACRO(BASE, CHAR, 64, __, 32, m8, __, m4, 3, /*MLEN=*/8, NAME, OP)

// 'Virtual' LMUL. This upholds the Highway guarantee that vectors are at least
// 128 bit and LowerHalf is defined whenever there are at least 2 lanes, even
// though RISC-V LMUL must be at least SEW/64 (notice that this rules out
// LMUL=1/2 for SEW=64). To bridge the gap, we add overloads for kPow2 equal to
// one less than should be supported, with all other parameters (vector type
// etc.) unchanged. For D with the lowest kPow2 ('virtual LMUL'), Lanes()
// returns half of what it usually would.
//
// Notice that we can only add overloads whenever there is a D argument: those
// are unique with respect to non-virtual-LMUL overloads because their kPow2
// template argument differs. Otherwise, there is no actual vuint64mf2_t, and
// defining another overload with the same LMUL would be an error. Thus we have
// a separate _VIRT category for HWY_RVV_FOREACH*, and the common case is
// _ALL_VIRT (meaning the regular LMUL plus the VIRT overloads), used in most
// functions that take a D.

#define HWY_RVV_FOREACH_08_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

#define HWY_RVV_FOREACH_16_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  X_MACRO(BASE, CHAR, 16, 32, 8, mf4, mf2, mf8, -3, /*MLEN=*/64, NAME, OP)

#define HWY_RVV_FOREACH_32_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  X_MACRO(BASE, CHAR, 32, 64, 16, mf2, m1, mf4, -2, /*MLEN=*/64, NAME, OP)

#define HWY_RVV_FOREACH_64_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  X_MACRO(BASE, CHAR, 64, __, 32, m1, m2, mf2, -1, /*MLEN=*/64, NAME, OP)

// ALL + VIRT
#define HWY_RVV_FOREACH_08_ALL_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_08_ALL(X_MACRO, BASE, CHAR, NAME, OP)            \
  HWY_RVV_FOREACH_08_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

#define HWY_RVV_FOREACH_16_ALL_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_16_ALL(X_MACRO, BASE, CHAR, NAME, OP)            \
  HWY_RVV_FOREACH_16_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

#define HWY_RVV_FOREACH_32_ALL_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_32_ALL(X_MACRO, BASE, CHAR, NAME, OP)            \
  HWY_RVV_FOREACH_32_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

#define HWY_RVV_FOREACH_64_ALL_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_64_ALL(X_MACRO, BASE, CHAR, NAME, OP)            \
  HWY_RVV_FOREACH_64_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

// LE2 + VIRT
#define HWY_RVV_FOREACH_08_LE2_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_08_LE2(X_MACRO, BASE, CHAR, NAME, OP)            \
  HWY_RVV_FOREACH_08_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

#define HWY_RVV_FOREACH_16_LE2_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_16_LE2(X_MACRO, BASE, CHAR, NAME, OP)            \
  HWY_RVV_FOREACH_16_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

#define HWY_RVV_FOREACH_32_LE2_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_32_LE2(X_MACRO, BASE, CHAR, NAME, OP)            \
  HWY_RVV_FOREACH_32_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

#define HWY_RVV_FOREACH_64_LE2_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_64_LE2(X_MACRO, BASE, CHAR, NAME, OP)            \
  HWY_RVV_FOREACH_64_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

// EXT + VIRT
#define HWY_RVV_FOREACH_08_EXT_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_08_EXT(X_MACRO, BASE, CHAR, NAME, OP)            \
  HWY_RVV_FOREACH_08_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

#define HWY_RVV_FOREACH_16_EXT_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_16_EXT(X_MACRO, BASE, CHAR, NAME, OP)            \
  HWY_RVV_FOREACH_16_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

#define HWY_RVV_FOREACH_32_EXT_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_32_EXT(X_MACRO, BASE, CHAR, NAME, OP)            \
  HWY_RVV_FOREACH_32_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

#define HWY_RVV_FOREACH_64_EXT_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_64_EXT(X_MACRO, BASE, CHAR, NAME, OP)            \
  HWY_RVV_FOREACH_64_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

// DEMOTE + VIRT
#define HWY_RVV_FOREACH_08_DEMOTE_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_08_DEMOTE(X_MACRO, BASE, CHAR, NAME, OP)            \
  HWY_RVV_FOREACH_08_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

#define HWY_RVV_FOREACH_16_DEMOTE_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_16_DEMOTE(X_MACRO, BASE, CHAR, NAME, OP)            \
  HWY_RVV_FOREACH_16_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

#define HWY_RVV_FOREACH_32_DEMOTE_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_32_DEMOTE(X_MACRO, BASE, CHAR, NAME, OP)            \
  HWY_RVV_FOREACH_32_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

#define HWY_RVV_FOREACH_64_DEMOTE_VIRT(X_MACRO, BASE, CHAR, NAME, OP) \
  HWY_RVV_FOREACH_64_DEMOTE(X_MACRO, BASE, CHAR, NAME, OP)            \
  HWY_RVV_FOREACH_64_VIRT(X_MACRO, BASE, CHAR, NAME, OP)

// SEW for unsigned:
#define HWY_RVV_FOREACH_U08(X_MACRO, NAME, OP, LMULS) \
  HWY_CONCAT(HWY_RVV_FOREACH_08, LMULS)(X_MACRO, uint, u, NAME, OP)
#define HWY_RVV_FOREACH_U16(X_MACRO, NAME, OP, LMULS) \
  HWY_CONCAT(HWY_RVV_FOREACH_16, LMULS)(X_MACRO, uint, u, NAME, OP)
#define HWY_RVV_FOREACH_U32(X_MACRO, NAME, OP, LMULS) \
  HWY_CONCAT(HWY_RVV_FOREACH_32, LMULS)(X_MACRO, uint, u, NAME, OP)
#define HWY_RVV_FOREACH_U64(X_MACRO, NAME, OP, LMULS) \
  HWY_CONCAT(HWY_RVV_FOREACH_64, LMULS)(X_MACRO, uint, u, NAME, OP)

// SEW for signed:
#define HWY_RVV_FOREACH_I08(X_MACRO, NAME, OP, LMULS) \
  HWY_CONCAT(HWY_RVV_FOREACH_08, LMULS)(X_MACRO, int, i, NAME, OP)
#define HWY_RVV_FOREACH_I16(X_MACRO, NAME, OP, LMULS) \
  HWY_CONCAT(HWY_RVV_FOREACH_16, LMULS)(X_MACRO, int, i, NAME, OP)
#define HWY_RVV_FOREACH_I32(X_MACRO, NAME, OP, LMULS) \
  HWY_CONCAT(HWY_RVV_FOREACH_32, LMULS)(X_MACRO, int, i, NAME, OP)
#define HWY_RVV_FOREACH_I64(X_MACRO, NAME, OP, LMULS) \
  HWY_CONCAT(HWY_RVV_FOREACH_64, LMULS)(X_MACRO, int, i, NAME, OP)

// SEW for float:

// Used for conversion instructions if HWY_RVV_HAVE_F16C.
#define HWY_RVV_FOREACH_F16_UNCONDITIONAL(X_MACRO, NAME, OP, LMULS) \
  HWY_CONCAT(HWY_RVV_FOREACH_16, LMULS)(X_MACRO, float, f, NAME, OP)

#if HWY_HAVE_FLOAT16
// Full support for f16 in all ops
#define HWY_RVV_FOREACH_F16(X_MACRO, NAME, OP, LMULS) \
  HWY_RVV_FOREACH_F16_UNCONDITIONAL(X_MACRO, NAME, OP, LMULS)
// Only BF16 is emulated.
#define HWY_RVV_IF_EMULATED_D(D) HWY_IF_BF16_D(D)
#define HWY_GENERIC_IF_EMULATED_D(D) HWY_IF_BF16_D(D)
#define HWY_RVV_IF_NOT_EMULATED_D(D) HWY_IF_NOT_BF16_D(D)
#else
#define HWY_RVV_FOREACH_F16(X_MACRO, NAME, OP, LMULS)
#define HWY_RVV_IF_EMULATED_D(D) HWY_IF_SPECIAL_FLOAT_D(D)
#define HWY_GENERIC_IF_EMULATED_D(D) HWY_IF_SPECIAL_FLOAT_D(D)
#define HWY_RVV_IF_NOT_EMULATED_D(D) HWY_IF_NOT_SPECIAL_FLOAT_D(D)
#endif
#define HWY_RVV_FOREACH_F32(X_MACRO, NAME, OP, LMULS) \
  HWY_CONCAT(HWY_RVV_FOREACH_32, LMULS)(X_MACRO, float, f, NAME, OP)
#define HWY_RVV_FOREACH_F64(X_MACRO, NAME, OP, LMULS) \
  HWY_CONCAT(HWY_RVV_FOREACH_64, LMULS)(X_MACRO, float, f, NAME, OP)

// Commonly used type/SEW groups:
#define HWY_RVV_FOREACH_UI08(X_MACRO, NAME, OP, LMULS) \
  HWY_RVV_FOREACH_U08(X_MACRO, NAME, OP, LMULS)        \
  HWY_RVV_FOREACH_I08(X_MACRO, NAME, OP, LMULS)

#define HWY_RVV_FOREACH_UI16(X_MACRO, NAME, OP, LMULS) \
  HWY_RVV_FOREACH_U16(X_MACRO, NAME, OP, LMULS)        \
  HWY_RVV_FOREACH_I16(X_MACRO, NAME, OP, LMULS)

#define HWY_RVV_FOREACH_UI32(X_MACRO, NAME, OP, LMULS) \
  HWY_RVV_FOREACH_U32(X_MACRO, NAME, OP, LMULS)        \
  HWY_RVV_FOREACH_I32(X_MACRO, NAME, OP, LMULS)

#define HWY_RVV_FOREACH_UI64(X_MACRO, NAME, OP, LMULS) \
  HWY_RVV_FOREACH_U64(X_MACRO, NAME, OP, LMULS)        \
  HWY_RVV_FOREACH_I64(X_MACRO, NAME, OP, LMULS)

#define HWY_RVV_FOREACH_UI3264(X_MACRO, NAME, OP, LMULS) \
  HWY_RVV_FOREACH_UI32(X_MACRO, NAME, OP, LMULS)         \
  HWY_RVV_FOREACH_UI64(X_MACRO, NAME, OP, LMULS)

#define HWY_RVV_FOREACH_U163264(X_MACRO, NAME, OP, LMULS) \
  HWY_RVV_FOREACH_U16(X_MACRO, NAME, OP, LMULS)           \
  HWY_RVV_FOREACH_U32(X_MACRO, NAME, OP, LMULS)           \
  HWY_RVV_FOREACH_U64(X_MACRO, NAME, OP, LMULS)

#define HWY_RVV_FOREACH_I163264(X_MACRO, NAME, OP, LMULS) \
  HWY_RVV_FOREACH_I16(X_MACRO, NAME, OP, LMULS)           \
  HWY_RVV_FOREACH_I32(X_MACRO, NAME, OP, LMULS)           \
  HWY_RVV_FOREACH_I64(X_MACRO, NAME, OP, LMULS)

#define HWY_RVV_FOREACH_UI163264(X_MACRO, NAME, OP, LMULS) \
  HWY_RVV_FOREACH_U163264(X_MACRO, NAME, OP, LMULS)        \
  HWY_RVV_FOREACH_I163264(X_MACRO, NAME, OP, LMULS)

#define HWY_RVV_FOREACH_F3264(X_MACRO, NAME, OP, LMULS) \
  HWY_RVV_FOREACH_F32(X_MACRO, NAME, OP, LMULS)         \
  HWY_RVV_FOREACH_F64(X_MACRO, NAME, OP, LMULS)

// For all combinations of SEW:
#define HWY_RVV_FOREACH_U(X_MACRO, NAME, OP, LMULS) \
  HWY_RVV_FOREACH_U08(X_MACRO, NAME, OP, LMULS)     \
  HWY_RVV_FOREACH_U163264(X_MACRO, NAME, OP, LMULS)

#define HWY_RVV_FOREACH_I(X_MACRO, NAME, OP, LMULS) \
  HWY_RVV_FOREACH_I08(X_MACRO, NAME, OP, LMULS)     \
  HWY_RVV_FOREACH_I163264(X_MACRO, NAME, OP, LMULS)

#define HWY_RVV_FOREACH_F(X_MACRO, NAME, OP, LMULS) \
  HWY_RVV_FOREACH_F16(X_MACRO, NAME, OP, LMULS)     \
  HWY_RVV_FOREACH_F3264(X_MACRO, NAME, OP, LMULS)

// Commonly used type categories:
#define HWY_RVV_FOREACH_UI(X_MACRO, NAME, OP, LMULS) \
  HWY_RVV_FOREACH_U(X_MACRO, NAME, OP, LMULS)        \
  HWY_RVV_FOREACH_I(X_MACRO, NAME, OP, LMULS)

#define HWY_RVV_FOREACH(X_MACRO, NAME, OP, LMULS) \
  HWY_RVV_FOREACH_UI(X_MACRO, NAME, OP, LMULS)    \
  HWY_RVV_FOREACH_F(X_MACRO, NAME, OP, LMULS)

// Assemble types for use in x-macros
#define HWY_RVV_T(BASE, SEW) BASE##SEW##_t
#define HWY_RVV_D(BASE, SEW, N, SHIFT) Simd<HWY_RVV_T(BASE, SEW), N, SHIFT>
#define HWY_RVV_V(BASE, SEW, LMUL) v##BASE##SEW##LMUL##_t
#define HWY_RVV_TUP(BASE, SEW, LMUL, TUP) v##BASE##SEW##LMUL##x##TUP##_t
#define HWY_RVV_M(MLEN) vbool##MLEN##_t

}  // namespace detail

// Until we have full intrinsic support for fractional LMUL, mixed-precision
// code can use LMUL 1..8 (adequate unless they need many registers).
#define HWY_SPECIALIZE(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                       MLEN, NAME, OP)                                         \
  template <>                                                                  \
  struct DFromV_t<HWY_RVV_V(BASE, SEW, LMUL)> {                                \
    using Lane = HWY_RVV_T(BASE, SEW);                                         \
    using type = ScalableTag<Lane, SHIFT>;                                     \
  };

HWY_RVV_FOREACH(HWY_SPECIALIZE, _, _, _ALL)
#undef HWY_SPECIALIZE

// ------------------------------ Lanes

// WARNING: we want to query VLMAX/sizeof(T), but this may actually change VL!

#if HWY_COMPILER_GCC && !HWY_IS_DEBUG_BUILD
// HWY_RVV_CAPPED_LANES_SPECIAL_CASES provides some additional optimizations
// to CappedLanes in non-debug builds
#define HWY_RVV_CAPPED_LANES_SPECIAL_CASES(BASE, SEW, LMUL)                    \
  if (__builtin_constant_p(cap >= kMaxLanes) && (cap >= kMaxLanes)) {          \
    /* If cap is known to be greater than or equal to MaxLanes(d), */          \
    /* HWY_MIN(cap, Lanes(d)) will be equal to Lanes(d) */                     \
    return Lanes(d);                                                           \
  }                                                                            \
                                                                               \
  if ((__builtin_constant_p((cap & (cap - 1)) == 0) &&                         \
       ((cap & (cap - 1)) == 0)) ||                                            \
      (__builtin_constant_p(cap <= HWY_MAX(kMinLanesPerFullVec, 4)) &&         \
       (cap <= HWY_MAX(kMinLanesPerFullVec, 4)))) {                            \
    /* If cap is known to be a power of 2, then */                             \
    /* vsetvl(HWY_MIN(cap, kMaxLanes)) is guaranteed to return the same */     \
    /* result as HWY_MIN(cap, Lanes(d)) as kMaxLanes is a power of 2 and */    \
    /* as (cap > VLMAX && cap < 2 * VLMAX) can only be true if cap is not a */ \
    /* power of 2 since VLMAX is always a power of 2 */                        \
                                                                               \
    /* If cap is known to be less than or equal to 4, then */                  \
    /* vsetvl(HWY_MIN(cap, kMaxLanes)) is guaranteed to return the same */     \
    /* result as HWY_MIN(cap, Lanes(d)) as HWY_MIN(cap, kMaxLanes) <= 4 is */  \
    /* true if cap <= 4 and as vsetvl(HWY_MIN(cap, kMaxLanes)) is */           \
    /* guaranteed to return the same result as HWY_MIN(cap, Lanes(d)) */       \
    /* if HWY_MIN(cap, kMaxLanes) <= 4 is true */                              \
                                                                               \
    /* If cap is known to be less than or equal to kMinLanesPerFullVec, */     \
    /* then vsetvl(HWY_MIN(cap, kMaxLanes)) is guaranteed to return the */     \
    /* same result as HWY_MIN(cap, Lanes(d)) as */                             \
    /* HWY_MIN(cap, kMaxLanes) <= kMinLanesPerFullVec is true if */            \
    /* cap <= kMinLanesPerFullVec is true */                                   \
                                                                               \
    /* If cap <= HWY_MAX(kMinLanesPerFullVec, 4) is true, then either */       \
    /* cap <= 4 or cap <= kMinLanesPerFullVec must be true */                  \
                                                                               \
    /* If cap <= HWY_MAX(kMinLanesPerFullVec, 4) is known to be true, */       \
    /* then vsetvl(HWY_MIN(cap, kMaxLanes)) is guaranteed to return the */     \
    /* same result as HWY_MIN(cap, Lanes(d)) */                                \
                                                                               \
    /* If no cap, avoid the HWY_MIN. */                                        \
    return detail::IsFull(d)                                                   \
               ? __riscv_vsetvl_e##SEW##LMUL(cap)                              \
               : __riscv_vsetvl_e##SEW##LMUL(HWY_MIN(cap, kMaxLanes));         \
  }
#else
#define HWY_RVV_CAPPED_LANES_SPECIAL_CASES(BASE, SEW, LMUL)
#endif

#define HWY_RVV_LANES(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT,  \
                      MLEN, NAME, OP)                                          \
  template <size_t N>                                                          \
  HWY_API size_t NAME(HWY_RVV_D(BASE, SEW, N, SHIFT) d) {                      \
    constexpr size_t kFull = HWY_LANES(HWY_RVV_T(BASE, SEW));                  \
    constexpr size_t kCap = MaxLanes(d);                                       \
    /* If no cap, avoid generating a constant by using VLMAX. */               \
    return N == kFull ? __riscv_vsetvlmax_e##SEW##LMUL()                       \
                      : __riscv_vsetvl_e##SEW##LMUL(kCap);                     \
  }                                                                            \
  template <size_t N>                                                          \
  HWY_API size_t Capped##NAME(HWY_RVV_D(BASE, SEW, N, SHIFT) d, size_t cap) {  \
    /* NOTE: Section 6.3 of the RVV specification, which can be found at */    \
    /* https://github.com/riscv/riscv-v-spec/blob/master/v-spec.adoc, */       \
    /* allows vsetvl to return a result less than Lanes(d) but greater than */ \
    /* or equal to ((cap + 1) / 2) if */                                       \
    /* (Lanes(d) > 2 && cap > HWY_MAX(Lanes(d), 4) && cap < (2 * Lanes(d))) */ \
    /* is true */                                                              \
                                                                               \
    /* VLMAX is the number of lanes in a vector of type */                     \
    /* VFromD<decltype(d)>, which is returned by */                            \
    /* Lanes(DFromV<VFromD<decltype(d)>>()) */                                 \
                                                                               \
    /* VLMAX is guaranteed to be a power of 2 under Section 2 of the RVV */    \
    /* specification */                                                        \
                                                                               \
    /* The VLMAX of a vector of type VFromD<decltype(d)> is at least 2 as */   \
    /* the HWY_RVV target requires support for the RVV Zvl128b extension, */   \
    /* which guarantees that vectors with LMUL=1 are at least 16 bytes */      \
                                                                               \
    /* If VLMAX == 2 is true, then vsetvl(cap) is equal to HWY_MIN(cap, 2) */  \
    /* as cap == 3 is the only value such that */                              \
    /* (cap > VLMAX && cap < 2 * VLMAX) if VLMAX == 2 and as */                \
    /* ((3 + 1) / 2) is equal to 2 */                                          \
                                                                               \
    /* If cap <= 4 is true, then vsetvl(cap) must be equal to */               \
    /* HWY_MIN(cap, VLMAX) as cap <= VLMAX is true if VLMAX >= 4 is true */    \
    /* and as vsetvl(cap) is guaranteed to be equal to HWY_MIN(cap, VLMAX) */  \
    /* if VLMAX == 2 */                                                        \
                                                                               \
    /* We want CappedLanes(d, cap) to return Lanes(d) if cap > Lanes(d) as */  \
    /* LoadN(d, p, cap) expects to load exactly HWY_MIN(cap, Lanes(d)) */      \
    /* lanes and StoreN(v, d, p, cap) expects to store exactly */              \
    /* HWY_MIN(cap, Lanes(d)) lanes, even in the case where vsetvl returns */  \
    /* a result that is less than HWY_MIN(cap, Lanes(d)) */                    \
                                                                               \
    /* kMinLanesPerFullVec is the minimum value of VLMAX for a vector of */    \
    /* type VFromD<decltype(d)> */                                             \
    constexpr size_t kMinLanesPerFullVec =                                     \
        detail::ScaleByPower(16 / (SEW / 8), SHIFT);                           \
    /* kMaxLanes is the maximum number of lanes returned by Lanes(d) */        \
    constexpr size_t kMaxLanes = MaxLanes(d);                                  \
                                                                               \
    HWY_RVV_CAPPED_LANES_SPECIAL_CASES(BASE, SEW, LMUL)                        \
                                                                               \
    if (kMaxLanes <= HWY_MAX(kMinLanesPerFullVec, 4)) {                        \
      /* If kMaxLanes <= kMinLanesPerFullVec is true, then */                  \
      /* vsetvl(HWY_MIN(cap, kMaxLanes)) is guaranteed to return */            \
      /* HWY_MIN(cap, Lanes(d)) as */                                          \
      /* HWY_MIN(cap, kMaxLanes) <= kMaxLanes <= VLMAX is true if */           \
      /* kMaxLanes <= kMinLanesPerFullVec is true */                           \
                                                                               \
      /* If kMaxLanes <= 4 is true, then vsetvl(HWY_MIN(cap, kMaxLanes)) is */ \
      /* guaranteed to return the same result as HWY_MIN(cap, Lanes(d)) as */  \
      /* HWY_MIN(cap, kMaxLanes) <= 4 is true if kMaxLanes <= 4 is true */     \
                                                                               \
      /* If kMaxLanes <= HWY_MAX(kMinLanesPerFullVec, 4) is true, then */      \
      /* either kMaxLanes <= 4 or kMaxLanes <= kMinLanesPerFullVec must be */  \
      /* true */                                                               \
                                                                               \
      return __riscv_vsetvl_e##SEW##LMUL(HWY_MIN(cap, kMaxLanes));             \
    } else {                                                                   \
      /* If kMaxLanes > HWY_MAX(kMinLanesPerFullVec, 4) is true, need to */    \
      /* obtain the actual number of lanes using Lanes(d) and clamp cap to */  \
      /* the result of Lanes(d) */                                             \
      const size_t actual = Lanes(d);                                          \
      return HWY_MIN(actual, cap);                                             \
    }                                                                          \
  }

#define HWY_RVV_LANES_VIRT(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,   \
                           SHIFT, MLEN, NAME, OP)                             \
  template <size_t N>                                                         \
  HWY_API size_t NAME(HWY_RVV_D(BASE, SEW, N, SHIFT) d) {                     \
    constexpr size_t kCap = MaxLanes(d);                                      \
    /* In case of virtual LMUL (intrinsics do not provide "uint16mf8_t") */   \
    /* vsetvl may or may not be correct, so do it ourselves. */               \
    const size_t actual =                                                     \
        detail::ScaleByPower(__riscv_vlenb() / (SEW / 8), SHIFT);             \
    return HWY_MIN(actual, kCap);                                             \
  }                                                                           \
  template <size_t N>                                                         \
  HWY_API size_t Capped##NAME(HWY_RVV_D(BASE, SEW, N, SHIFT) d, size_t cap) { \
    /* In case of virtual LMUL (intrinsics do not provide "uint16mf8_t") */   \
    /* vsetvl may or may not be correct, so do it ourselves. */               \
    const size_t actual =                                                     \
        detail::ScaleByPower(__riscv_vlenb() / (SEW / 8), SHIFT);             \
    /* If no cap, avoid an extra HWY_MIN. */                                  \
    return detail::IsFull(d) ? HWY_MIN(actual, cap)                           \
                             : HWY_MIN(HWY_MIN(actual, cap), MaxLanes(d));    \
  }

HWY_RVV_FOREACH(HWY_RVV_LANES, Lanes, setvlmax_e, _ALL)
HWY_RVV_FOREACH(HWY_RVV_LANES_VIRT, Lanes, lenb, _VIRT)
#undef HWY_RVV_LANES
#undef HWY_RVV_LANES_VIRT
#undef HWY_RVV_CAPPED_LANES_SPECIAL_CASES

template <class D, HWY_RVV_IF_EMULATED_D(D)>
HWY_API size_t Lanes(D /* tag*/) {
  return Lanes(RebindToUnsigned<D>());
}

template <class D, HWY_RVV_IF_EMULATED_D(D)>
HWY_API size_t CappedLanes(D /* tag*/, size_t cap) {
  return CappedLanes(RebindToUnsigned<D>(), cap);
}

// ------------------------------ Common x-macros

// Last argument to most intrinsics. Use when the op has no d arg of its own,
// which means there is no user-specified cap.
#define HWY_RVV_AVL(SEW, SHIFT) \
  Lanes(ScalableTag<HWY_RVV_T(uint, SEW), SHIFT>())

// vector = f(vector), e.g. Not
#define HWY_RVV_RETV_ARGV(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,  \
                          SHIFT, MLEN, NAME, OP)                            \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL) NAME(HWY_RVV_V(BASE, SEW, LMUL) v) {   \
    return __riscv_v##OP##_v_##CHAR##SEW##LMUL(v, HWY_RVV_AVL(SEW, SHIFT)); \
  }

// vector = f(vector, scalar), e.g. detail::AddS
#define HWY_RVV_RETV_ARGVS(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,  \
                           SHIFT, MLEN, NAME, OP)                            \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                         \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) a, HWY_RVV_T(BASE, SEW) b) {           \
    return __riscv_v##OP##_##CHAR##SEW##LMUL(a, b, HWY_RVV_AVL(SEW, SHIFT)); \
  }

// vector = f(vector, vector), e.g. Add
#define HWY_RVV_RETV_ARGVV(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                           SHIFT, MLEN, NAME, OP)                           \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                        \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) a, HWY_RVV_V(BASE, SEW, LMUL) b) {    \
    return __riscv_v##OP##_vv_##CHAR##SEW##LMUL(a, b,                       \
                                                HWY_RVV_AVL(SEW, SHIFT));   \
  }

// vector = f(vector, mask, vector, vector), e.g. MaskedAddOr
#define HWY_RVV_RETV_ARGMVV(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,   \
                            SHIFT, MLEN, NAME, OP)                             \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                           \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) no, HWY_RVV_M(MLEN) m,                   \
           HWY_RVV_V(BASE, SEW, LMUL) a, HWY_RVV_V(BASE, SEW, LMUL) b) {       \
    return __riscv_v##OP##_vv_##CHAR##SEW##LMUL##_mu(m, no, a, b,              \
                                                     HWY_RVV_AVL(SEW, SHIFT)); \
  }

// mask = f(mask)
#define HWY_RVV_RETM_ARGM(SEW, SHIFT, MLEN, NAME, OP)              \
  HWY_API HWY_RVV_M(MLEN) NAME(HWY_RVV_M(MLEN) m) {                \
    return __riscv_vm##OP##_m_b##MLEN(m, HWY_RVV_AVL(SEW, SHIFT)); \
  }

// ================================================== INIT

// ------------------------------ Set

#define HWY_RVV_SET(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                    MLEN, NAME, OP)                                         \
  template <size_t N>                                                       \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                        \
      NAME(HWY_RVV_D(BASE, SEW, N, SHIFT) d, HWY_RVV_T(BASE, SEW) arg) {    \
    return __riscv_v##OP##_##CHAR##SEW##LMUL(arg, Lanes(d));                \
  }

HWY_RVV_FOREACH_UI(HWY_RVV_SET, Set, mv_v_x, _ALL_VIRT)
HWY_RVV_FOREACH_F(HWY_RVV_SET, Set, fmv_v_f, _ALL_VIRT)
#undef HWY_RVV_SET

// Treat bfloat16_t as int16_t (using the previously defined Set overloads);
// required for Zero and VFromD.
template <class D, HWY_IF_BF16_D(D)>
decltype(Set(RebindToSigned<D>(), 0)) Set(D d, hwy::bfloat16_t arg) {
  return Set(RebindToSigned<decltype(d)>(), BitCastScalar<int16_t>(arg));
}
#if !HWY_HAVE_FLOAT16  // Otherwise already defined above.
// WARNING: returns a different type than emulated bfloat16_t so that we can
// implement PromoteTo overloads for both bfloat16_t and float16_t, and also
// provide a Neg(hwy::float16_t) overload that coexists with Neg(int16_t).
template <class D, HWY_IF_F16_D(D)>
decltype(Set(RebindToUnsigned<D>(), 0)) Set(D d, hwy::float16_t arg) {
  return Set(RebindToUnsigned<decltype(d)>(), BitCastScalar<uint16_t>(arg));
}
#endif

template <class D>
using VFromD = decltype(Set(D(), TFromD<D>()));

// ------------------------------ Zero

template <class D>
HWY_API VFromD<D> Zero(D d) {
  // Cast to support bfloat16_t.
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Set(du, 0));
}

// ------------------------------ Undefined

// RVV vundefined is 'poisoned' such that even XORing a _variable_ initialized
// by it gives unpredictable results. It should only be used for maskoff, so
// keep it internal. For the Highway op, just use Zero (single instruction).
namespace detail {
#define HWY_RVV_UNDEFINED(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                          SHIFT, MLEN, NAME, OP)                           \
  template <size_t N>                                                      \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                       \
      NAME(HWY_RVV_D(BASE, SEW, N, SHIFT) /* tag */) {                     \
    return __riscv_v##OP##_##CHAR##SEW##LMUL(); /* no AVL */               \
  }

HWY_RVV_FOREACH(HWY_RVV_UNDEFINED, Undefined, undefined, _ALL)
#undef HWY_RVV_UNDEFINED
}  // namespace detail

template <class D>
HWY_API VFromD<D> Undefined(D d) {
  return Zero(d);
}

// ------------------------------ BitCast

namespace detail {

// Halves LMUL. (Use LMUL arg for the source so we can use _TRUNC.)
#define HWY_RVV_TRUNC(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                      MLEN, NAME, OP)                                         \
  HWY_API HWY_RVV_V(BASE, SEW, LMULH) NAME(HWY_RVV_V(BASE, SEW, LMUL) v) {    \
    return __riscv_v##OP##_v_##CHAR##SEW##LMUL##_##CHAR##SEW##LMULH(          \
        v); /* no AVL */                                                      \
  }
HWY_RVV_FOREACH(HWY_RVV_TRUNC, Trunc, lmul_trunc, _TRUNC)
#undef HWY_RVV_TRUNC

// Doubles LMUL to `d2` (the arg is only necessary for _VIRT).
#define HWY_RVV_EXT(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                    MLEN, NAME, OP)                                         \
  template <size_t N>                                                       \
  HWY_API HWY_RVV_V(BASE, SEW, LMULD)                                       \
      NAME(HWY_RVV_D(BASE, SEW, N, SHIFT + 1) /* d2 */,                     \
           HWY_RVV_V(BASE, SEW, LMUL) v) {                                  \
    return __riscv_v##OP##_v_##CHAR##SEW##LMUL##_##CHAR##SEW##LMULD(        \
        v); /* no AVL */                                                    \
  }
HWY_RVV_FOREACH(HWY_RVV_EXT, Ext, lmul_ext, _EXT)
#undef HWY_RVV_EXT

// For virtual LMUL e.g. 'uint32mf4_t', the return type should be mf2, which is
// the same as the actual input type.
#define HWY_RVV_EXT_VIRT(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                         SHIFT, MLEN, NAME, OP)                           \
  template <size_t N>                                                     \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                      \
      NAME(HWY_RVV_D(BASE, SEW, N, SHIFT + 1) /* d2 */,                   \
           HWY_RVV_V(BASE, SEW, LMUL) v) {                                \
    return v;                                                             \
  }
HWY_RVV_FOREACH(HWY_RVV_EXT_VIRT, Ext, lmul_ext, _VIRT)
#undef HWY_RVV_EXT_VIRT

template <class D, HWY_RVV_IF_EMULATED_D(D)>
VFromD<D> Ext(D d, VFromD<Half<D>> v) {
  const RebindToUnsigned<decltype(d)> du;
  const Half<decltype(du)> duh;
  return BitCast(d, Ext(du, BitCast(duh, v)));
}

// For BitCastToByte, the D arg is only to prevent duplicate definitions caused
// by _ALL_VIRT.

// There is no reinterpret from u8 <-> u8, so just return.
#define HWY_RVV_CAST_U8(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                        SHIFT, MLEN, NAME, OP)                           \
  template <typename T, size_t N>                                        \
  HWY_API vuint8##LMUL##_t BitCastToByte(Simd<T, N, SHIFT> /* d */,      \
                                         vuint8##LMUL##_t v) {           \
    return v;                                                            \
  }                                                                      \
  template <size_t N>                                                    \
  HWY_API vuint8##LMUL##_t BitCastFromByte(                              \
      HWY_RVV_D(BASE, SEW, N, SHIFT) /* d */, vuint8##LMUL##_t v) {      \
    return v;                                                            \
  }

// For i8, need a single reinterpret (HWY_RVV_CAST_IF does two).
#define HWY_RVV_CAST_I8(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                        SHIFT, MLEN, NAME, OP)                           \
  template <typename T, size_t N>                                        \
  HWY_API vuint8##LMUL##_t BitCastToByte(Simd<T, N, SHIFT> /* d */,      \
                                         vint8##LMUL##_t v) {            \
    return __riscv_vreinterpret_v_i8##LMUL##_u8##LMUL(v);                \
  }                                                                      \
  template <size_t N>                                                    \
  HWY_API vint8##LMUL##_t BitCastFromByte(                               \
      HWY_RVV_D(BASE, SEW, N, SHIFT) /* d */, vuint8##LMUL##_t v) {      \
    return __riscv_vreinterpret_v_u8##LMUL##_i8##LMUL(v);                \
  }

// Separate u/i because clang only provides signed <-> unsigned reinterpret for
// the same SEW.
#define HWY_RVV_CAST_U(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                       MLEN, NAME, OP)                                         \
  template <typename T, size_t N>                                              \
  HWY_API vuint8##LMUL##_t BitCastToByte(Simd<T, N, SHIFT> /* d */,            \
                                         HWY_RVV_V(BASE, SEW, LMUL) v) {       \
    return __riscv_v##OP##_v_##CHAR##SEW##LMUL##_u8##LMUL(v);                  \
  }                                                                            \
  template <size_t N>                                                          \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL) BitCastFromByte(                          \
      HWY_RVV_D(BASE, SEW, N, SHIFT) /* d */, vuint8##LMUL##_t v) {            \
    return __riscv_v##OP##_v_u8##LMUL##_##CHAR##SEW##LMUL(v);                  \
  }

// Signed/Float: first cast to/from unsigned
#define HWY_RVV_CAST_IF(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                        SHIFT, MLEN, NAME, OP)                           \
  template <typename T, size_t N>                                        \
  HWY_API vuint8##LMUL##_t BitCastToByte(Simd<T, N, SHIFT> /* d */,      \
                                         HWY_RVV_V(BASE, SEW, LMUL) v) { \
    return __riscv_v##OP##_v_u##SEW##LMUL##_u8##LMUL(                    \
        __riscv_v##OP##_v_##CHAR##SEW##LMUL##_u##SEW##LMUL(v));          \
  }                                                                      \
  template <size_t N>                                                    \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL) BitCastFromByte(                    \
      HWY_RVV_D(BASE, SEW, N, SHIFT) /* d */, vuint8##LMUL##_t v) {      \
    return __riscv_v##OP##_v_u##SEW##LMUL##_##CHAR##SEW##LMUL(           \
        __riscv_v##OP##_v_u8##LMUL##_u##SEW##LMUL(v));                   \
  }

// Additional versions for virtual LMUL using LMULH for byte vectors.
#define HWY_RVV_CAST_VIRT_U(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                            SHIFT, MLEN, NAME, OP)                           \
  template <typename T, size_t N>                                            \
  HWY_API vuint8##LMULH##_t BitCastToByte(Simd<T, N, SHIFT> /* d */,         \
                                          HWY_RVV_V(BASE, SEW, LMUL) v) {    \
    return detail::Trunc(__riscv_v##OP##_v_##CHAR##SEW##LMUL##_u8##LMUL(v)); \
  }                                                                          \
  template <size_t N>                                                        \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL) BitCastFromByte(                        \
      HWY_RVV_D(BASE, SEW, N, SHIFT) /* d */, vuint8##LMULH##_t v) {         \
    HWY_RVV_D(uint, 8, N, SHIFT + 1) d2;                                     \
    const vuint8##LMUL##_t v2 = detail::Ext(d2, v);                          \
    return __riscv_v##OP##_v_u8##LMUL##_##CHAR##SEW##LMUL(v2);               \
  }

// Signed/Float: first cast to/from unsigned
#define HWY_RVV_CAST_VIRT_IF(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                             SHIFT, MLEN, NAME, OP)                           \
  template <typename T, size_t N>                                             \
  HWY_API vuint8##LMULH##_t BitCastToByte(Simd<T, N, SHIFT> /* d */,          \
                                          HWY_RVV_V(BASE, SEW, LMUL) v) {     \
    return detail::Trunc(__riscv_v##OP##_v_u##SEW##LMUL##_u8##LMUL(           \
        __riscv_v##OP##_v_##CHAR##SEW##LMUL##_u##SEW##LMUL(v)));              \
  }                                                                           \
  template <size_t N>                                                         \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL) BitCastFromByte(                         \
      HWY_RVV_D(BASE, SEW, N, SHIFT) /* d */, vuint8##LMULH##_t v) {          \
    HWY_RVV_D(uint, 8, N, SHIFT + 1) d2;                                      \
    const vuint8##LMUL##_t v2 = detail::Ext(d2, v);                           \
    return __riscv_v##OP##_v_u##SEW##LMUL##_##CHAR##SEW##LMUL(                \
        __riscv_v##OP##_v_u8##LMUL##_u##SEW##LMUL(v2));                       \
  }

HWY_RVV_FOREACH_U08(HWY_RVV_CAST_U8, _, reinterpret, _ALL)
HWY_RVV_FOREACH_I08(HWY_RVV_CAST_I8, _, reinterpret, _ALL)
HWY_RVV_FOREACH_U163264(HWY_RVV_CAST_U, _, reinterpret, _ALL)
HWY_RVV_FOREACH_I163264(HWY_RVV_CAST_IF, _, reinterpret, _ALL)
HWY_RVV_FOREACH_U163264(HWY_RVV_CAST_VIRT_U, _, reinterpret, _VIRT)
HWY_RVV_FOREACH_I163264(HWY_RVV_CAST_VIRT_IF, _, reinterpret, _VIRT)
HWY_RVV_FOREACH_F(HWY_RVV_CAST_IF, _, reinterpret, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_CAST_VIRT_IF, _, reinterpret, _VIRT)
#if HWY_HAVE_FLOAT16     // HWY_RVV_FOREACH_F already covered float16_
#elif HWY_RVV_HAVE_F16C  // zvfhmin provides reinterpret* intrinsics:
HWY_RVV_FOREACH_F16_UNCONDITIONAL(HWY_RVV_CAST_IF, _, reinterpret, _ALL)
HWY_RVV_FOREACH_F16_UNCONDITIONAL(HWY_RVV_CAST_VIRT_IF, _, reinterpret, _VIRT)
#else
template <class D, HWY_IF_F16_D(D)>
HWY_INLINE VFromD<RebindToUnsigned<D>> BitCastFromByte(
    D /* d */, VFromD<Repartition<uint8_t, D>> v) {
  return BitCastFromByte(RebindToUnsigned<D>(), v);
}
#endif

#undef HWY_RVV_CAST_U8
#undef HWY_RVV_CAST_I8
#undef HWY_RVV_CAST_U
#undef HWY_RVV_CAST_IF
#undef HWY_RVV_CAST_VIRT_U
#undef HWY_RVV_CAST_VIRT_IF

template <class D, HWY_IF_BF16_D(D)>
HWY_INLINE VFromD<RebindToSigned<D>> BitCastFromByte(
    D d, VFromD<Repartition<uint8_t, D>> v) {
  return BitCastFromByte(RebindToSigned<decltype(d)>(), v);
}

}  // namespace detail

template <class D, class FromV>
HWY_API VFromD<D> BitCast(D d, FromV v) {
  return detail::BitCastFromByte(d, detail::BitCastToByte(d, v));
}

// ------------------------------ Iota

namespace detail {

#define HWY_RVV_IOTA(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT,  \
                     MLEN, NAME, OP)                                          \
  template <size_t N>                                                         \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL) NAME(HWY_RVV_D(BASE, SEW, N, SHIFT) d) { \
    return __riscv_v##OP##_##CHAR##SEW##LMUL(Lanes(d));                       \
  }

// For i8 lanes, this may well wrap around. Unsigned only is less error-prone.
HWY_RVV_FOREACH_U(HWY_RVV_IOTA, Iota0, id_v, _ALL_VIRT)
#undef HWY_RVV_IOTA

// Used by Expand.
#define HWY_RVV_MASKED_IOTA(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                            SHIFT, MLEN, NAME, OP)                           \
  template <size_t N>                                                        \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                         \
      NAME(HWY_RVV_D(BASE, SEW, N, SHIFT) d, HWY_RVV_M(MLEN) mask) {         \
    return __riscv_v##OP##_##CHAR##SEW##LMUL(mask, Lanes(d));                \
  }

HWY_RVV_FOREACH_U(HWY_RVV_MASKED_IOTA, MaskedIota, iota_m, _ALL_VIRT)
#undef HWY_RVV_MASKED_IOTA

}  // namespace detail

// ================================================== LOGICAL

// ------------------------------ Not

HWY_RVV_FOREACH_UI(HWY_RVV_RETV_ARGV, Not, not, _ALL)

template <class V, HWY_IF_FLOAT_V(V)>
HWY_API V Not(const V v) {
  using DF = DFromV<V>;
  using DU = RebindToUnsigned<DF>;
  return BitCast(DF(), Not(BitCast(DU(), v)));
}

// ------------------------------ And

// Non-vector version (ideally immediate) for use with Iota0
namespace detail {
HWY_RVV_FOREACH_UI(HWY_RVV_RETV_ARGVS, AndS, and_vx, _ALL)
}  // namespace detail

HWY_RVV_FOREACH_UI(HWY_RVV_RETV_ARGVV, And, and, _ALL)

template <class V, HWY_IF_FLOAT_V(V)>
HWY_API V And(const V a, const V b) {
  using DF = DFromV<V>;
  using DU = RebindToUnsigned<DF>;
  return BitCast(DF(), And(BitCast(DU(), a), BitCast(DU(), b)));
}

// ------------------------------ Or

HWY_RVV_FOREACH_UI(HWY_RVV_RETV_ARGVV, Or, or, _ALL)

template <class V, HWY_IF_FLOAT_V(V)>
HWY_API V Or(const V a, const V b) {
  using DF = DFromV<V>;
  using DU = RebindToUnsigned<DF>;
  return BitCast(DF(), Or(BitCast(DU(), a), BitCast(DU(), b)));
}

// ------------------------------ Xor

// Non-vector version (ideally immediate) for use with Iota0
namespace detail {
HWY_RVV_FOREACH_UI(HWY_RVV_RETV_ARGVS, XorS, xor_vx, _ALL)
}  // namespace detail

HWY_RVV_FOREACH_UI(HWY_RVV_RETV_ARGVV, Xor, xor, _ALL)

template <class V, HWY_IF_FLOAT_V(V)>
HWY_API V Xor(const V a, const V b) {
  using DF = DFromV<V>;
  using DU = RebindToUnsigned<DF>;
  return BitCast(DF(), Xor(BitCast(DU(), a), BitCast(DU(), b)));
}

// ------------------------------ AndNot
template <class V>
HWY_API V AndNot(const V not_a, const V b) {
  return And(Not(not_a), b);
}

// ------------------------------ Xor3
template <class V>
HWY_API V Xor3(V x1, V x2, V x3) {
  return Xor(x1, Xor(x2, x3));
}

// ------------------------------ Or3
template <class V>
HWY_API V Or3(V o1, V o2, V o3) {
  return Or(o1, Or(o2, o3));
}

// ------------------------------ OrAnd
template <class V>
HWY_API V OrAnd(const V o, const V a1, const V a2) {
  return Or(o, And(a1, a2));
}

// ------------------------------ CopySign

HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGVV, CopySign, fsgnj, _ALL)

template <class V>
HWY_API V CopySignToAbs(const V abs, const V sign) {
  // RVV can also handle abs < 0, so no extra action needed.
  return CopySign(abs, sign);
}

// ================================================== ARITHMETIC

// Per-target flags to prevent generic_ops-inl.h defining Add etc.
#ifdef HWY_NATIVE_OPERATOR_REPLACEMENTS
#undef HWY_NATIVE_OPERATOR_REPLACEMENTS
#else
#define HWY_NATIVE_OPERATOR_REPLACEMENTS
#endif

// ------------------------------ Add

namespace detail {
HWY_RVV_FOREACH_UI(HWY_RVV_RETV_ARGVS, AddS, add_vx, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGVS, AddS, fadd_vf, _ALL)
HWY_RVV_FOREACH_UI(HWY_RVV_RETV_ARGVS, ReverseSubS, rsub_vx, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGVS, ReverseSubS, frsub_vf, _ALL)
}  // namespace detail

HWY_RVV_FOREACH_UI(HWY_RVV_RETV_ARGVV, Add, add, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGVV, Add, fadd, _ALL)

// ------------------------------ Sub
namespace detail {
HWY_RVV_FOREACH_UI(HWY_RVV_RETV_ARGVS, SubS, sub_vx, _ALL)
}  // namespace detail

HWY_RVV_FOREACH_UI(HWY_RVV_RETV_ARGVV, Sub, sub, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGVV, Sub, fsub, _ALL)

// ------------------------------ Neg (ReverseSubS, Xor)

template <class V, HWY_IF_SIGNED_V(V)>
HWY_API V Neg(const V v) {
  return detail::ReverseSubS(v, 0);
}

// vector = f(vector), but argument is repeated
#define HWY_RVV_RETV_ARGV2(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                           SHIFT, MLEN, NAME, OP)                           \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL) NAME(HWY_RVV_V(BASE, SEW, LMUL) v) {   \
    return __riscv_v##OP##_vv_##CHAR##SEW##LMUL(v, v,                       \
                                                HWY_RVV_AVL(SEW, SHIFT));   \
  }

HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGV2, Neg, fsgnjn, _ALL)

#if !HWY_HAVE_FLOAT16

template <class V, HWY_IF_U16_D(DFromV<V>)>  // hwy::float16_t
HWY_API V Neg(V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;
  return BitCast(d, Xor(BitCast(du, v), Set(du, SignMask<TU>())));
}

#endif  // !HWY_HAVE_FLOAT16

// ------------------------------ SaturatedAdd

#ifdef HWY_NATIVE_I32_SATURATED_ADDSUB
#undef HWY_NATIVE_I32_SATURATED_ADDSUB
#else
#define HWY_NATIVE_I32_SATURATED_ADDSUB
#endif

#ifdef HWY_NATIVE_U32_SATURATED_ADDSUB
#undef HWY_NATIVE_U32_SATURATED_ADDSUB
#else
#define HWY_NATIVE_U32_SATURATED_ADDSUB
#endif

#ifdef HWY_NATIVE_I64_SATURATED_ADDSUB
#undef HWY_NATIVE_I64_SATURATED_ADDSUB
#else
#define HWY_NATIVE_I64_SATURATED_ADDSUB
#endif

#ifdef HWY_NATIVE_U64_SATURATED_ADDSUB
#undef HWY_NATIVE_U64_SATURATED_ADDSUB
#else
#define HWY_NATIVE_U64_SATURATED_ADDSUB
#endif

HWY_RVV_FOREACH_U(HWY_RVV_RETV_ARGVV, SaturatedAdd, saddu, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_RETV_ARGVV, SaturatedAdd, sadd, _ALL)

// ------------------------------ SaturatedSub

HWY_RVV_FOREACH_U(HWY_RVV_RETV_ARGVV, SaturatedSub, ssubu, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_RETV_ARGVV, SaturatedSub, ssub, _ALL)

// ------------------------------ AverageRound

#ifdef HWY_NATIVE_AVERAGE_ROUND_UI32
#undef HWY_NATIVE_AVERAGE_ROUND_UI32
#else
#define HWY_NATIVE_AVERAGE_ROUND_UI32
#endif

#ifdef HWY_NATIVE_AVERAGE_ROUND_UI64
#undef HWY_NATIVE_AVERAGE_ROUND_UI64
#else
#define HWY_NATIVE_AVERAGE_ROUND_UI64
#endif

// Define this to opt-out of the default behavior, which is AVOID on certain
// compiler versions. You can define only this to use VXRM, or define both this
// and HWY_RVV_AVOID_VXRM to always avoid VXRM.
#ifndef HWY_RVV_CHOOSE_VXRM

// Assume that GCC-13 defaults to 'avoid VXRM'. Tested with GCC 13.1.0.
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 1400
#define HWY_RVV_AVOID_VXRM
// Clang 16 with __riscv_v_intrinsic == 11000 may either require VXRM or avoid.
// Assume earlier versions avoid.
#elif HWY_COMPILER_CLANG && \
    (HWY_COMPILER_CLANG < 1600 || __riscv_v_intrinsic < 11000)
#define HWY_RVV_AVOID_VXRM
#endif

#endif  // HWY_RVV_CHOOSE_VXRM

// Adding __RISCV_VXRM_* was a backwards-incompatible change and it is not clear
// how to detect whether it is supported or required. #ifdef __RISCV_VXRM_RDN
// does not work because it seems to be a compiler built-in, but neither does
// __has_builtin(__RISCV_VXRM_RDN). The intrinsics version was also not updated,
// so we require a macro to opt out of the new intrinsics.
#ifdef HWY_RVV_AVOID_VXRM
#define HWY_RVV_INSERT_VXRM(vxrm, avl) avl
#define __RISCV_VXRM_RNU
#define __RISCV_VXRM_RDN
#else  // default: use new vxrm arguments
#define HWY_RVV_INSERT_VXRM(vxrm, avl) vxrm, avl
#endif

// Extra rounding mode = up argument.
#define HWY_RVV_RETV_AVERAGE(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,  \
                             SHIFT, MLEN, NAME, OP)                            \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                           \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) a, HWY_RVV_V(BASE, SEW, LMUL) b) {       \
    return __riscv_v##OP##_vv_##CHAR##SEW##LMUL(                               \
        a, b, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RNU, HWY_RVV_AVL(SEW, SHIFT))); \
  }

HWY_RVV_FOREACH_I(HWY_RVV_RETV_AVERAGE, AverageRound, aadd, _ALL)
HWY_RVV_FOREACH_U(HWY_RVV_RETV_AVERAGE, AverageRound, aaddu, _ALL)

#undef HWY_RVV_RETV_AVERAGE

// ------------------------------ ShiftLeft[Same]

// Intrinsics do not define .vi forms, so use .vx instead.
#define HWY_RVV_SHIFT(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT,  \
                      MLEN, NAME, OP)                                          \
  template <int kBits>                                                         \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL) NAME(HWY_RVV_V(BASE, SEW, LMUL) v) {      \
    return __riscv_v##OP##_vx_##CHAR##SEW##LMUL(v, kBits,                      \
                                                HWY_RVV_AVL(SEW, SHIFT));      \
  }                                                                            \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                           \
      NAME##Same(HWY_RVV_V(BASE, SEW, LMUL) v, int bits) {                     \
    return __riscv_v##OP##_vx_##CHAR##SEW##LMUL(v, static_cast<uint8_t>(bits), \
                                                HWY_RVV_AVL(SEW, SHIFT));      \
  }

HWY_RVV_FOREACH_UI(HWY_RVV_SHIFT, ShiftLeft, sll, _ALL)

// ------------------------------ ShiftRight[Same]

HWY_RVV_FOREACH_U(HWY_RVV_SHIFT, ShiftRight, srl, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_SHIFT, ShiftRight, sra, _ALL)

#undef HWY_RVV_SHIFT

// ------------------------------ RoundingShiftRight[Same]

#ifdef HWY_NATIVE_ROUNDING_SHR
#undef HWY_NATIVE_ROUNDING_SHR
#else
#define HWY_NATIVE_ROUNDING_SHR
#endif

// Intrinsics do not define .vi forms, so use .vx instead.
#define HWY_RVV_ROUNDING_SHR(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                             SHIFT, MLEN, NAME, OP)                           \
  template <int kBits>                                                        \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL) NAME(HWY_RVV_V(BASE, SEW, LMUL) v) {     \
    return __riscv_v##OP##_vx_##CHAR##SEW##LMUL(                              \
        v, kBits,                                                             \
        HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RNU, HWY_RVV_AVL(SEW, SHIFT)));      \
  }                                                                           \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                          \
      NAME##Same(HWY_RVV_V(BASE, SEW, LMUL) v, int bits) {                    \
    return __riscv_v##OP##_vx_##CHAR##SEW##LMUL(                              \
        v, static_cast<uint8_t>(bits),                                        \
        HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RNU, HWY_RVV_AVL(SEW, SHIFT)));      \
  }

HWY_RVV_FOREACH_U(HWY_RVV_ROUNDING_SHR, RoundingShiftRight, ssrl, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_ROUNDING_SHR, RoundingShiftRight, ssra, _ALL)

#undef HWY_RVV_ROUNDING_SHR

// ------------------------------ SumsOf8 (ShiftRight, Add)
template <class VU8, HWY_IF_U8_D(DFromV<VU8>)>
HWY_API VFromD<Repartition<uint64_t, DFromV<VU8>>> SumsOf8(const VU8 v) {
  const DFromV<VU8> du8;
  const RepartitionToWide<decltype(du8)> du16;
  const RepartitionToWide<decltype(du16)> du32;
  const RepartitionToWide<decltype(du32)> du64;
  using VU16 = VFromD<decltype(du16)>;

  const VU16 vFDB97531 = ShiftRight<8>(BitCast(du16, v));
  const VU16 vECA86420 = detail::AndS(BitCast(du16, v), 0xFF);
  const VU16 sFE_DC_BA_98_76_54_32_10 = Add(vFDB97531, vECA86420);

  const VU16 szz_FE_zz_BA_zz_76_zz_32 =
      BitCast(du16, ShiftRight<16>(BitCast(du32, sFE_DC_BA_98_76_54_32_10)));
  const VU16 sxx_FC_xx_B8_xx_74_xx_30 =
      Add(sFE_DC_BA_98_76_54_32_10, szz_FE_zz_BA_zz_76_zz_32);
  const VU16 szz_zz_xx_FC_zz_zz_xx_74 =
      BitCast(du16, ShiftRight<32>(BitCast(du64, sxx_FC_xx_B8_xx_74_xx_30)));
  const VU16 sxx_xx_xx_F8_xx_xx_xx_70 =
      Add(sxx_FC_xx_B8_xx_74_xx_30, szz_zz_xx_FC_zz_zz_xx_74);
  return detail::AndS(BitCast(du64, sxx_xx_xx_F8_xx_xx_xx_70), 0xFFFFull);
}

template <class VI8, HWY_IF_I8_D(DFromV<VI8>)>
HWY_API VFromD<Repartition<int64_t, DFromV<VI8>>> SumsOf8(const VI8 v) {
  const DFromV<VI8> di8;
  const RepartitionToWide<decltype(di8)> di16;
  const RepartitionToWide<decltype(di16)> di32;
  const RepartitionToWide<decltype(di32)> di64;
  const RebindToUnsigned<decltype(di32)> du32;
  const RebindToUnsigned<decltype(di64)> du64;
  using VI16 = VFromD<decltype(di16)>;

  const VI16 vFDB97531 = ShiftRight<8>(BitCast(di16, v));
  const VI16 vECA86420 = ShiftRight<8>(ShiftLeft<8>(BitCast(di16, v)));
  const VI16 sFE_DC_BA_98_76_54_32_10 = Add(vFDB97531, vECA86420);

  const VI16 sDC_zz_98_zz_54_zz_10_zz =
      BitCast(di16, ShiftLeft<16>(BitCast(du32, sFE_DC_BA_98_76_54_32_10)));
  const VI16 sFC_xx_B8_xx_74_xx_30_xx =
      Add(sFE_DC_BA_98_76_54_32_10, sDC_zz_98_zz_54_zz_10_zz);
  const VI16 sB8_xx_zz_zz_30_xx_zz_zz =
      BitCast(di16, ShiftLeft<32>(BitCast(du64, sFC_xx_B8_xx_74_xx_30_xx)));
  const VI16 sF8_xx_xx_xx_70_xx_xx_xx =
      Add(sFC_xx_B8_xx_74_xx_30_xx, sB8_xx_zz_zz_30_xx_zz_zz);
  return ShiftRight<48>(BitCast(di64, sF8_xx_xx_xx_70_xx_xx_xx));
}

// ------------------------------ RotateRight
template <int kBits, class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)>
HWY_API V RotateRight(const V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

  constexpr size_t kSizeInBits = sizeof(TFromV<V>) * 8;
  static_assert(0 <= kBits && kBits < kSizeInBits, "Invalid shift count");
  if (kBits == 0) return v;

  return Or(BitCast(d, ShiftRight<kBits>(BitCast(du, v))),
            ShiftLeft<HWY_MIN(kSizeInBits - 1, kSizeInBits - kBits)>(v));
}

// ------------------------------ Shl
#define HWY_RVV_SHIFT_VV(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,   \
                         SHIFT, MLEN, NAME, OP)                             \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                        \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) v, HWY_RVV_V(BASE, SEW, LMUL) bits) { \
    return __riscv_v##OP##_vv_##CHAR##SEW##LMUL(v, bits,                    \
                                                HWY_RVV_AVL(SEW, SHIFT));   \
  }

HWY_RVV_FOREACH_U(HWY_RVV_SHIFT_VV, Shl, sll, _ALL)

#define HWY_RVV_SHIFT_II(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,   \
                         SHIFT, MLEN, NAME, OP)                             \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                        \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) v, HWY_RVV_V(BASE, SEW, LMUL) bits) { \
    const HWY_RVV_D(uint, SEW, HWY_LANES(HWY_RVV_T(BASE, SEW)), SHIFT) du;  \
    return __riscv_v##OP##_vv_##CHAR##SEW##LMUL(v, BitCast(du, bits),       \
                                                HWY_RVV_AVL(SEW, SHIFT));   \
  }

HWY_RVV_FOREACH_I(HWY_RVV_SHIFT_II, Shl, sll, _ALL)

// ------------------------------ Shr

HWY_RVV_FOREACH_U(HWY_RVV_SHIFT_VV, Shr, srl, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_SHIFT_II, Shr, sra, _ALL)

#undef HWY_RVV_SHIFT_II
#undef HWY_RVV_SHIFT_VV

// ------------------------------ RoundingShr
#define HWY_RVV_ROUNDING_SHR_VV(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD,   \
                                LMULH, SHIFT, MLEN, NAME, OP)               \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                        \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) v, HWY_RVV_V(BASE, SEW, LMUL) bits) { \
    return __riscv_v##OP##_vv_##CHAR##SEW##LMUL(                            \
        v, bits,                                                            \
        HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RNU, HWY_RVV_AVL(SEW, SHIFT)));    \
  }

HWY_RVV_FOREACH_U(HWY_RVV_ROUNDING_SHR_VV, RoundingShr, ssrl, _ALL)

#define HWY_RVV_ROUNDING_SHR_II(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD,   \
                                LMULH, SHIFT, MLEN, NAME, OP)               \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                        \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) v, HWY_RVV_V(BASE, SEW, LMUL) bits) { \
    const HWY_RVV_D(uint, SEW, HWY_LANES(HWY_RVV_T(BASE, SEW)), SHIFT) du;  \
    return __riscv_v##OP##_vv_##CHAR##SEW##LMUL(                            \
        v, BitCast(du, bits),                                               \
        HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RNU, HWY_RVV_AVL(SEW, SHIFT)));    \
  }

HWY_RVV_FOREACH_I(HWY_RVV_ROUNDING_SHR_II, RoundingShr, ssra, _ALL)

#undef HWY_RVV_ROUNDING_SHR_VV
#undef HWY_RVV_ROUNDING_SHR_II

// ------------------------------ Min

namespace detail {

HWY_RVV_FOREACH_U(HWY_RVV_RETV_ARGVS, MinS, minu_vx, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_RETV_ARGVS, MinS, min_vx, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGVS, MinS, fmin_vf, _ALL)

}  // namespace detail

HWY_RVV_FOREACH_U(HWY_RVV_RETV_ARGVV, Min, minu, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_RETV_ARGVV, Min, min, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGVV, Min, fmin, _ALL)

// ------------------------------ Max

namespace detail {

HWY_RVV_FOREACH_U(HWY_RVV_RETV_ARGVS, MaxS, maxu_vx, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_RETV_ARGVS, MaxS, max_vx, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGVS, MaxS, fmax_vf, _ALL)

}  // namespace detail

HWY_RVV_FOREACH_U(HWY_RVV_RETV_ARGVV, Max, maxu, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_RETV_ARGVV, Max, max, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGVV, Max, fmax, _ALL)

// ------------------------------ Mul

// Per-target flags to prevent generic_ops-inl.h defining 8/64-bit operator*.
#ifdef HWY_NATIVE_MUL_8
#undef HWY_NATIVE_MUL_8
#else
#define HWY_NATIVE_MUL_8
#endif
#ifdef HWY_NATIVE_MUL_64
#undef HWY_NATIVE_MUL_64
#else
#define HWY_NATIVE_MUL_64
#endif

HWY_RVV_FOREACH_UI(HWY_RVV_RETV_ARGVV, Mul, mul, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGVV, Mul, fmul, _ALL)

// ------------------------------ MulHigh

HWY_RVV_FOREACH_I(HWY_RVV_RETV_ARGVV, MulHigh, mulh, _ALL)
HWY_RVV_FOREACH_U(HWY_RVV_RETV_ARGVV, MulHigh, mulhu, _ALL)

// ------------------------------ MulFixedPoint15

// Extra rounding mode = up argument.
#define HWY_RVV_MUL15(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT,  \
                      MLEN, NAME, OP)                                          \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                           \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) a, HWY_RVV_V(BASE, SEW, LMUL) b) {       \
    return __riscv_v##OP##_vv_##CHAR##SEW##LMUL(                               \
        a, b, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RNU, HWY_RVV_AVL(SEW, SHIFT))); \
  }

HWY_RVV_FOREACH_I16(HWY_RVV_MUL15, MulFixedPoint15, smul, _ALL)

#undef HWY_RVV_MUL15

// ------------------------------ Div
#ifdef HWY_NATIVE_INT_DIV
#undef HWY_NATIVE_INT_DIV
#else
#define HWY_NATIVE_INT_DIV
#endif

HWY_RVV_FOREACH_U(HWY_RVV_RETV_ARGVV, Div, divu, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_RETV_ARGVV, Div, div, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGVV, Div, fdiv, _ALL)

HWY_RVV_FOREACH_U(HWY_RVV_RETV_ARGVV, Mod, remu, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_RETV_ARGVV, Mod, rem, _ALL)

// ------------------------------ MaskedAddOr etc.

#ifdef HWY_NATIVE_MASKED_ARITH
#undef HWY_NATIVE_MASKED_ARITH
#else
#define HWY_NATIVE_MASKED_ARITH
#endif

HWY_RVV_FOREACH_U(HWY_RVV_RETV_ARGMVV, MaskedMinOr, minu, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_RETV_ARGMVV, MaskedMinOr, min, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGMVV, MaskedMinOr, fmin, _ALL)

HWY_RVV_FOREACH_U(HWY_RVV_RETV_ARGMVV, MaskedMaxOr, maxu, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_RETV_ARGMVV, MaskedMaxOr, max, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGMVV, MaskedMaxOr, fmax, _ALL)

HWY_RVV_FOREACH_UI(HWY_RVV_RETV_ARGMVV, MaskedAddOr, add, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGMVV, MaskedAddOr, fadd, _ALL)

HWY_RVV_FOREACH_UI(HWY_RVV_RETV_ARGMVV, MaskedSubOr, sub, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGMVV, MaskedSubOr, fsub, _ALL)

HWY_RVV_FOREACH_UI(HWY_RVV_RETV_ARGMVV, MaskedMulOr, mul, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGMVV, MaskedMulOr, fmul, _ALL)

HWY_RVV_FOREACH_U(HWY_RVV_RETV_ARGMVV, MaskedDivOr, divu, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_RETV_ARGMVV, MaskedDivOr, div, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGMVV, MaskedDivOr, fdiv, _ALL)

HWY_RVV_FOREACH_U(HWY_RVV_RETV_ARGMVV, MaskedModOr, remu, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_RETV_ARGMVV, MaskedModOr, rem, _ALL)

HWY_RVV_FOREACH_U(HWY_RVV_RETV_ARGMVV, MaskedSatAddOr, saddu, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_RETV_ARGMVV, MaskedSatAddOr, sadd, _ALL)

HWY_RVV_FOREACH_U(HWY_RVV_RETV_ARGMVV, MaskedSatSubOr, ssubu, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_RETV_ARGMVV, MaskedSatSubOr, ssub, _ALL)

// ------------------------------ ApproximateReciprocal
#ifdef HWY_NATIVE_F64_APPROX_RECIP
#undef HWY_NATIVE_F64_APPROX_RECIP
#else
#define HWY_NATIVE_F64_APPROX_RECIP
#endif

HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGV, ApproximateReciprocal, frec7, _ALL)

// ------------------------------ Sqrt
HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGV, Sqrt, fsqrt, _ALL)

// ------------------------------ ApproximateReciprocalSqrt
#ifdef HWY_NATIVE_F64_APPROX_RSQRT
#undef HWY_NATIVE_F64_APPROX_RSQRT
#else
#define HWY_NATIVE_F64_APPROX_RSQRT
#endif

HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGV, ApproximateReciprocalSqrt, frsqrt7, _ALL)

// ------------------------------ MulAdd

// Per-target flag to prevent generic_ops-inl.h from defining int MulAdd.
#ifdef HWY_NATIVE_INT_FMA
#undef HWY_NATIVE_INT_FMA
#else
#define HWY_NATIVE_INT_FMA
#endif

// Note: op is still named vv, not vvv.
#define HWY_RVV_FMA(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                    MLEN, NAME, OP)                                         \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                        \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) mul, HWY_RVV_V(BASE, SEW, LMUL) x,    \
           HWY_RVV_V(BASE, SEW, LMUL) add) {                                \
    return __riscv_v##OP##_vv_##CHAR##SEW##LMUL(add, mul, x,                \
                                                HWY_RVV_AVL(SEW, SHIFT));   \
  }

HWY_RVV_FOREACH_UI(HWY_RVV_FMA, MulAdd, macc, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_FMA, MulAdd, fmacc, _ALL)

// ------------------------------ NegMulAdd
HWY_RVV_FOREACH_UI(HWY_RVV_FMA, NegMulAdd, nmsac, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_FMA, NegMulAdd, fnmsac, _ALL)

// ------------------------------ MulSub
HWY_RVV_FOREACH_F(HWY_RVV_FMA, MulSub, fmsac, _ALL)

// ------------------------------ NegMulSub
HWY_RVV_FOREACH_F(HWY_RVV_FMA, NegMulSub, fnmacc, _ALL)

#undef HWY_RVV_FMA

// ================================================== COMPARE

// Comparisons set a mask bit to 1 if the condition is true, else 0. The XX in
// vboolXX_t is a power of two divisor for vector bits. SEW=8 / LMUL=1 = 1/8th
// of all bits; SEW=8 / LMUL=4 = half of all bits.

// mask = f(vector, vector)
#define HWY_RVV_RETM_ARGVV(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                           SHIFT, MLEN, NAME, OP)                           \
  HWY_API HWY_RVV_M(MLEN)                                                   \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) a, HWY_RVV_V(BASE, SEW, LMUL) b) {    \
    return __riscv_v##OP##_vv_##CHAR##SEW##LMUL##_b##MLEN(                  \
        a, b, HWY_RVV_AVL(SEW, SHIFT));                                     \
  }

// mask = f(vector, scalar)
#define HWY_RVV_RETM_ARGVS(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                           SHIFT, MLEN, NAME, OP)                           \
  HWY_API HWY_RVV_M(MLEN)                                                   \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) a, HWY_RVV_T(BASE, SEW) b) {          \
    return __riscv_v##OP##_##CHAR##SEW##LMUL##_b##MLEN(                     \
        a, b, HWY_RVV_AVL(SEW, SHIFT));                                     \
  }

// ------------------------------ Eq
HWY_RVV_FOREACH_UI(HWY_RVV_RETM_ARGVV, Eq, mseq, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETM_ARGVV, Eq, mfeq, _ALL)

namespace detail {
HWY_RVV_FOREACH_UI(HWY_RVV_RETM_ARGVS, EqS, mseq_vx, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETM_ARGVS, EqS, mfeq_vf, _ALL)
}  // namespace detail

// ------------------------------ Ne
HWY_RVV_FOREACH_UI(HWY_RVV_RETM_ARGVV, Ne, msne, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETM_ARGVV, Ne, mfne, _ALL)

namespace detail {
HWY_RVV_FOREACH_UI(HWY_RVV_RETM_ARGVS, NeS, msne_vx, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETM_ARGVS, NeS, mfne_vf, _ALL)
}  // namespace detail

// ------------------------------ Lt
HWY_RVV_FOREACH_U(HWY_RVV_RETM_ARGVV, Lt, msltu, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_RETM_ARGVV, Lt, mslt, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETM_ARGVV, Lt, mflt, _ALL)

namespace detail {
HWY_RVV_FOREACH_I(HWY_RVV_RETM_ARGVS, LtS, mslt_vx, _ALL)
HWY_RVV_FOREACH_U(HWY_RVV_RETM_ARGVS, LtS, msltu_vx, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETM_ARGVS, LtS, mflt_vf, _ALL)
}  // namespace detail

// ------------------------------ Le
HWY_RVV_FOREACH_U(HWY_RVV_RETM_ARGVV, Le, msleu, _ALL)
HWY_RVV_FOREACH_I(HWY_RVV_RETM_ARGVV, Le, msle, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_RETM_ARGVV, Le, mfle, _ALL)

#undef HWY_RVV_RETM_ARGVV
#undef HWY_RVV_RETM_ARGVS

// ------------------------------ Gt/Ge

template <class V>
HWY_API auto Ge(const V a, const V b) -> decltype(Le(a, b)) {
  return Le(b, a);
}

template <class V>
HWY_API auto Gt(const V a, const V b) -> decltype(Lt(a, b)) {
  return Lt(b, a);
}

// ------------------------------ TestBit
template <class V>
HWY_API auto TestBit(const V a, const V bit) -> decltype(Eq(a, bit)) {
  return detail::NeS(And(a, bit), 0);
}

// ------------------------------ Not
// NOLINTNEXTLINE
HWY_RVV_FOREACH_B(HWY_RVV_RETM_ARGM, Not, not )

// ------------------------------ And

// mask = f(mask_a, mask_b) (note arg2,arg1 order!)
#define HWY_RVV_RETM_ARGMM(SEW, SHIFT, MLEN, NAME, OP)                 \
  HWY_API HWY_RVV_M(MLEN) NAME(HWY_RVV_M(MLEN) a, HWY_RVV_M(MLEN) b) { \
    return __riscv_vm##OP##_mm_b##MLEN(b, a, HWY_RVV_AVL(SEW, SHIFT)); \
  }

HWY_RVV_FOREACH_B(HWY_RVV_RETM_ARGMM, And, and)

// ------------------------------ AndNot
HWY_RVV_FOREACH_B(HWY_RVV_RETM_ARGMM, AndNot, andn)

// ------------------------------ Or
HWY_RVV_FOREACH_B(HWY_RVV_RETM_ARGMM, Or, or)

// ------------------------------ Xor
HWY_RVV_FOREACH_B(HWY_RVV_RETM_ARGMM, Xor, xor)

// ------------------------------ ExclusiveNeither
HWY_RVV_FOREACH_B(HWY_RVV_RETM_ARGMM, ExclusiveNeither, xnor)

#undef HWY_RVV_RETM_ARGMM

// ------------------------------ IfThenElse

#define HWY_RVV_IF_THEN_ELSE(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                             SHIFT, MLEN, NAME, OP)                           \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                          \
      NAME(HWY_RVV_M(MLEN) m, HWY_RVV_V(BASE, SEW, LMUL) yes,                 \
           HWY_RVV_V(BASE, SEW, LMUL) no) {                                   \
    return __riscv_v##OP##_vvm_##CHAR##SEW##LMUL(no, yes, m,                  \
                                                 HWY_RVV_AVL(SEW, SHIFT));    \
  }

HWY_RVV_FOREACH(HWY_RVV_IF_THEN_ELSE, IfThenElse, merge, _ALL)

#undef HWY_RVV_IF_THEN_ELSE

// ------------------------------ IfThenElseZero
template <class M, class V>
HWY_API V IfThenElseZero(const M mask, const V yes) {
  return IfThenElse(mask, yes, Zero(DFromV<V>()));
}

// ------------------------------ IfThenZeroElse

#define HWY_RVV_IF_THEN_ZERO_ELSE(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, \
                                  LMULH, SHIFT, MLEN, NAME, OP)             \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                        \
      NAME(HWY_RVV_M(MLEN) m, HWY_RVV_V(BASE, SEW, LMUL) no) {              \
    return __riscv_v##OP##_##CHAR##SEW##LMUL(no, 0, m,                      \
                                             HWY_RVV_AVL(SEW, SHIFT));      \
  }

HWY_RVV_FOREACH_UI(HWY_RVV_IF_THEN_ZERO_ELSE, IfThenZeroElse, merge_vxm, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_IF_THEN_ZERO_ELSE, IfThenZeroElse, fmerge_vfm, _ALL)

#undef HWY_RVV_IF_THEN_ZERO_ELSE

// ------------------------------ MaskFromVec

template <class D>
using MFromD = decltype(Eq(Zero(D()), Zero(D())));

template <class V>
HWY_API MFromD<DFromV<V>> MaskFromVec(const V v) {
  return detail::NeS(v, 0);
}

// ------------------------------ IsNegative (MFromD)
#ifdef HWY_NATIVE_IS_NEGATIVE
#undef HWY_NATIVE_IS_NEGATIVE
#else
#define HWY_NATIVE_IS_NEGATIVE
#endif

// Generic for all vector lengths
template <class V, HWY_IF_NOT_UNSIGNED_V(V)>
HWY_API MFromD<DFromV<V>> IsNegative(V v) {
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;
  using TI = TFromD<decltype(di)>;

  return detail::LtS(BitCast(di, v), static_cast<TI>(0));
}

// ------------------------------ MaskFalse

// For mask ops including vmclr, elements past VL are tail-agnostic and cannot
// be relied upon, so define a variant of the generic_ops-inl implementation of
// MaskFalse that ensures all bits are zero as required by mask_test.
#ifdef HWY_NATIVE_MASK_FALSE
#undef HWY_NATIVE_MASK_FALSE
#else
#define HWY_NATIVE_MASK_FALSE
#endif

template <class D>
HWY_API MFromD<D> MaskFalse(D d) {
  const DFromV<VFromD<decltype(d)>> d_full;
  return MaskFromVec(Zero(d_full));
}

// ------------------------------ RebindMask
template <class D, typename MFrom>
HWY_API MFromD<D> RebindMask(const D /*d*/, const MFrom mask) {
  // No need to check lane size/LMUL are the same: if not, casting MFrom to
  // MFromD<D> would fail.
  return mask;
}

// ------------------------------ VecFromMask

// Returns mask ? ~0 : 0. No longer use sub.vx(Zero(), 1, mask) because per the
// default mask-agnostic policy, the result of inactive lanes may also be ~0.
#define HWY_RVV_VEC_FROM_MASK(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                              SHIFT, MLEN, NAME, OP)                           \
  template <size_t N>                                                          \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                           \
      NAME(HWY_RVV_D(BASE, SEW, N, SHIFT) d, HWY_RVV_M(MLEN) m) {              \
    /* MaskFalse requires we set all lanes for capped d and virtual LMUL. */   \
    const DFromV<VFromD<decltype(d)>> d_full;                                  \
    const RebindToSigned<decltype(d_full)> di;                                 \
    using TI = TFromD<decltype(di)>;                                           \
    return BitCast(d_full, __riscv_v##OP##_i##SEW##LMUL(Zero(di), TI{-1}, m,   \
                                                        Lanes(d_full)));       \
  }

HWY_RVV_FOREACH_UI(HWY_RVV_VEC_FROM_MASK, VecFromMask, merge_vxm, _ALL_VIRT)

#undef HWY_RVV_VEC_FROM_MASK

template <class D, HWY_IF_FLOAT_D(D)>
HWY_API VFromD<D> VecFromMask(const D d, MFromD<D> mask) {
  return BitCast(d, VecFromMask(RebindToUnsigned<D>(), mask));
}

// ------------------------------ IfVecThenElse (MaskFromVec)
template <class V>
HWY_API V IfVecThenElse(const V mask, const V yes, const V no) {
  return IfThenElse(MaskFromVec(mask), yes, no);
}

// ------------------------------ BroadcastSignBit
template <class V, HWY_IF_SIGNED_V(V)>
HWY_API V BroadcastSignBit(const V v) {
  return ShiftRight<sizeof(TFromV<V>) * 8 - 1>(v);
}

// ------------------------------ IfNegativeThenElse (BroadcastSignBit)
template <class V>
HWY_API V IfNegativeThenElse(V v, V yes, V no) {
  static_assert(IsSigned<TFromV<V>>(), "Only works for signed/float");
  return IfThenElse(IsNegative(v), yes, no);
}

// ------------------------------ FindFirstTrue

#define HWY_RVV_FIND_FIRST_TRUE(SEW, SHIFT, MLEN, NAME, OP)            \
  template <class D>                                                   \
  HWY_API intptr_t FindFirstTrue(D d, HWY_RVV_M(MLEN) m) {             \
    static_assert(MLenFromD(d) == MLEN, "Type mismatch");              \
    return __riscv_vfirst_m_b##MLEN(m, Lanes(d));                      \
  }                                                                    \
  template <class D>                                                   \
  HWY_API size_t FindKnownFirstTrue(D d, HWY_RVV_M(MLEN) m) {          \
    static_assert(MLenFromD(d) == MLEN, "Type mismatch");              \
    return static_cast<size_t>(__riscv_vfirst_m_b##MLEN(m, Lanes(d))); \
  }

HWY_RVV_FOREACH_B(HWY_RVV_FIND_FIRST_TRUE, , _)
#undef HWY_RVV_FIND_FIRST_TRUE

// ------------------------------ AllFalse
template <class D>
HWY_API bool AllFalse(D d, MFromD<D> m) {
  return FindFirstTrue(d, m) < 0;
}

// ------------------------------ AllTrue

#define HWY_RVV_ALL_TRUE(SEW, SHIFT, MLEN, NAME, OP)          \
  template <class D>                                          \
  HWY_API bool AllTrue(D d, HWY_RVV_M(MLEN) m) {              \
    static_assert(MLenFromD(d) == MLEN, "Type mismatch");     \
    return AllFalse(d, __riscv_vmnot_m_b##MLEN(m, Lanes(d))); \
  }

HWY_RVV_FOREACH_B(HWY_RVV_ALL_TRUE, _, _)
#undef HWY_RVV_ALL_TRUE

// ------------------------------ CountTrue

#define HWY_RVV_COUNT_TRUE(SEW, SHIFT, MLEN, NAME, OP)    \
  template <class D>                                      \
  HWY_API size_t CountTrue(D d, HWY_RVV_M(MLEN) m) {      \
    static_assert(MLenFromD(d) == MLEN, "Type mismatch"); \
    return __riscv_vcpop_m_b##MLEN(m, Lanes(d));          \
  }

HWY_RVV_FOREACH_B(HWY_RVV_COUNT_TRUE, _, _)
#undef HWY_RVV_COUNT_TRUE

// ------------------------------ PromoteMaskTo

#ifdef HWY_NATIVE_PROMOTE_MASK_TO
#undef HWY_NATIVE_PROMOTE_MASK_TO
#else
#define HWY_NATIVE_PROMOTE_MASK_TO
#endif

template <class DTo, class DFrom,
          HWY_IF_T_SIZE_GT_D(DTo, sizeof(TFromD<DFrom>)),
          hwy::EnableIf<IsSame<MFromD<DTo>, MFromD<DFrom>>()>* = nullptr>
HWY_API MFromD<DTo> PromoteMaskTo(DTo /*d_to*/, DFrom /*d_from*/,
                                  MFromD<DFrom> m) {
  return m;
}

// ------------------------------ DemoteMaskTo

#ifdef HWY_NATIVE_DEMOTE_MASK_TO
#undef HWY_NATIVE_DEMOTE_MASK_TO
#else
#define HWY_NATIVE_DEMOTE_MASK_TO
#endif

template <class DTo, class DFrom,
          HWY_IF_T_SIZE_LE_D(DTo, sizeof(TFromD<DFrom>) - 1),
          hwy::EnableIf<IsSame<MFromD<DTo>, MFromD<DFrom>>()>* = nullptr>
HWY_API MFromD<DTo> DemoteMaskTo(DTo /*d_to*/, DFrom /*d_from*/,
                                 MFromD<DFrom> m) {
  return m;
}

// ================================================== MEMORY

// ------------------------------ Load

#define HWY_RVV_LOAD(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                     MLEN, NAME, OP)                                         \
  template <size_t N>                                                        \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                         \
      NAME(HWY_RVV_D(BASE, SEW, N, SHIFT) d,                                 \
           const HWY_RVV_T(BASE, SEW) * HWY_RESTRICT p) {                    \
    return __riscv_v##OP##SEW##_v_##CHAR##SEW##LMUL(                         \
        detail::NativeLanePointer(p), Lanes(d));                             \
  }
HWY_RVV_FOREACH(HWY_RVV_LOAD, Load, le, _ALL_VIRT)
#undef HWY_RVV_LOAD

template <class D, HWY_RVV_IF_EMULATED_D(D)>
HWY_API VFromD<D> Load(D d, const TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Load(du, detail::U16LanePointer(p)));
}

// ------------------------------ LoadU
template <class D>
HWY_API VFromD<D> LoadU(D d, const TFromD<D>* HWY_RESTRICT p) {
  // RVV only requires element alignment, not vector alignment.
  return Load(d, p);
}

// ------------------------------ MaskedLoad

#define HWY_RVV_MASKED_LOAD(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                            SHIFT, MLEN, NAME, OP)                           \
  template <size_t N>                                                        \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                         \
      NAME(HWY_RVV_M(MLEN) m, HWY_RVV_D(BASE, SEW, N, SHIFT) d,              \
           const HWY_RVV_T(BASE, SEW) * HWY_RESTRICT p) {                    \
    return __riscv_v##OP##SEW##_v_##CHAR##SEW##LMUL##_mu(                    \
        m, Zero(d), detail::NativeLanePointer(p), Lanes(d));                 \
  }                                                                          \
  template <size_t N>                                                        \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                         \
      NAME##Or(HWY_RVV_V(BASE, SEW, LMUL) v, HWY_RVV_M(MLEN) m,              \
               HWY_RVV_D(BASE, SEW, N, SHIFT) d,                             \
               const HWY_RVV_T(BASE, SEW) * HWY_RESTRICT p) {                \
    return __riscv_v##OP##SEW##_v_##CHAR##SEW##LMUL##_mu(                    \
        m, v, detail::NativeLanePointer(p), Lanes(d));                       \
  }

HWY_RVV_FOREACH(HWY_RVV_MASKED_LOAD, MaskedLoad, le, _ALL_VIRT)
#undef HWY_RVV_MASKED_LOAD

template <class D, HWY_RVV_IF_EMULATED_D(D)>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D d,
                             const TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d,
                 MaskedLoad(RebindMask(du, m), du, detail::U16LanePointer(p)));
}

template <class D, HWY_RVV_IF_EMULATED_D(D)>
HWY_API VFromD<D> MaskedLoadOr(VFromD<D> no, MFromD<D> m, D d,
                               const TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, MaskedLoadOr(BitCast(du, no), RebindMask(du, m), du,
                                 detail::U16LanePointer(p)));
}

// ------------------------------ LoadN

// Native with avl is faster than the generic_ops using FirstN.
#ifdef HWY_NATIVE_LOAD_N
#undef HWY_NATIVE_LOAD_N
#else
#define HWY_NATIVE_LOAD_N
#endif

#define HWY_RVV_LOADN(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                      MLEN, NAME, OP)                                         \
  template <size_t N>                                                         \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                          \
      NAME(HWY_RVV_D(BASE, SEW, N, SHIFT) d,                                  \
           const HWY_RVV_T(BASE, SEW) * HWY_RESTRICT p, size_t num_lanes) {   \
    /* Use a tail-undisturbed load in LoadN as the tail-undisturbed load */   \
    /* operation below will leave any lanes past the first */                 \
    /* (lowest-indexed) HWY_MIN(num_lanes, Lanes(d)) lanes unchanged */       \
    return __riscv_v##OP##SEW##_v_##CHAR##SEW##LMUL##_tu(                     \
        Zero(d), detail::NativeLanePointer(p), CappedLanes(d, num_lanes));    \
  }                                                                           \
  template <size_t N>                                                         \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL) NAME##Or(                                \
      HWY_RVV_V(BASE, SEW, LMUL) no, HWY_RVV_D(BASE, SEW, N, SHIFT) d,        \
      const HWY_RVV_T(BASE, SEW) * HWY_RESTRICT p, size_t num_lanes) {        \
    /* Use a tail-undisturbed load in LoadNOr as the tail-undisturbed load */ \
    /* operation below will set any lanes past the first */                   \
    /* (lowest-indexed) HWY_MIN(num_lanes, Lanes(d)) lanes to the */          \
    /* corresponding lanes in no */                                           \
    return __riscv_v##OP##SEW##_v_##CHAR##SEW##LMUL##_tu(                     \
        no, detail::NativeLanePointer(p), CappedLanes(d, num_lanes));         \
  }

HWY_RVV_FOREACH(HWY_RVV_LOADN, LoadN, le, _ALL_VIRT)
#undef HWY_RVV_LOADN

template <class D, HWY_RVV_IF_EMULATED_D(D)>
HWY_API VFromD<D> LoadN(D d, const TFromD<D>* HWY_RESTRICT p,
                        size_t num_lanes) {
  const RebindToUnsigned<D> du;
  return BitCast(d, LoadN(du, detail::U16LanePointer(p), num_lanes));
}
template <class D, HWY_RVV_IF_EMULATED_D(D)>
HWY_API VFromD<D> LoadNOr(VFromD<D> v, D d, const TFromD<D>* HWY_RESTRICT p,
                          size_t num_lanes) {
  const RebindToUnsigned<D> du;
  return BitCast(
      d, LoadNOr(BitCast(du, v), du, detail::U16LanePointer(p), num_lanes));
}

// ------------------------------ Store

#define HWY_RVV_STORE(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                      MLEN, NAME, OP)                                         \
  template <size_t N>                                                         \
  HWY_API void NAME(HWY_RVV_V(BASE, SEW, LMUL) v,                             \
                    HWY_RVV_D(BASE, SEW, N, SHIFT) d,                         \
                    HWY_RVV_T(BASE, SEW) * HWY_RESTRICT p) {                  \
    return __riscv_v##OP##SEW##_v_##CHAR##SEW##LMUL(                          \
        detail::NativeLanePointer(p), v, Lanes(d));                           \
  }
HWY_RVV_FOREACH(HWY_RVV_STORE, Store, se, _ALL_VIRT)
#undef HWY_RVV_STORE

template <class D, HWY_RVV_IF_EMULATED_D(D)>
HWY_API void Store(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;
  Store(BitCast(du, v), du, detail::U16LanePointer(p));
}

// ------------------------------ BlendedStore

#define HWY_RVV_BLENDED_STORE(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                              SHIFT, MLEN, NAME, OP)                           \
  template <size_t N>                                                          \
  HWY_API void NAME(HWY_RVV_V(BASE, SEW, LMUL) v, HWY_RVV_M(MLEN) m,           \
                    HWY_RVV_D(BASE, SEW, N, SHIFT) d,                          \
                    HWY_RVV_T(BASE, SEW) * HWY_RESTRICT p) {                   \
    return __riscv_v##OP##SEW##_v_##CHAR##SEW##LMUL##_m(                       \
        m, detail::NativeLanePointer(p), v, Lanes(d));                         \
  }
HWY_RVV_FOREACH(HWY_RVV_BLENDED_STORE, BlendedStore, se, _ALL_VIRT)
#undef HWY_RVV_BLENDED_STORE

template <class D, HWY_RVV_IF_EMULATED_D(D)>
HWY_API void BlendedStore(VFromD<D> v, MFromD<D> m, D d,
                          TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;
  BlendedStore(BitCast(du, v), RebindMask(du, m), du,
               detail::U16LanePointer(p));
}

// ------------------------------ StoreN

namespace detail {

#define HWY_RVV_STOREN(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                       MLEN, NAME, OP)                                         \
  template <size_t N>                                                          \
  HWY_API void NAME(size_t count, HWY_RVV_V(BASE, SEW, LMUL) v,                \
                    HWY_RVV_D(BASE, SEW, N, SHIFT) /* d */,                    \
                    HWY_RVV_T(BASE, SEW) * HWY_RESTRICT p) {                   \
    return __riscv_v##OP##SEW##_v_##CHAR##SEW##LMUL(                           \
        detail::NativeLanePointer(p), v, count);                               \
  }
HWY_RVV_FOREACH(HWY_RVV_STOREN, StoreN, se, _ALL_VIRT)
#undef HWY_RVV_STOREN

template <class D, HWY_RVV_IF_EMULATED_D(D)>
HWY_API void StoreN(size_t count, VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;
  StoreN(count, BitCast(du, v), du, detail::U16LanePointer(p));
}

}  // namespace detail

#ifdef HWY_NATIVE_STORE_N
#undef HWY_NATIVE_STORE_N
#else
#define HWY_NATIVE_STORE_N
#endif

template <class D>
HWY_API void StoreN(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p,
                    size_t max_lanes_to_store) {
  // NOTE: Need to clamp max_lanes_to_store to Lanes(d), even if
  // MaxLanes(d) >= MaxLanes(DFromV<VFromD<D>>()) is true, as it is possible for
  // detail::StoreN(max_lanes_to_store, v, d, p) to store fewer than
  // Lanes(DFromV<VFromD<D>>()) lanes to p if
  // max_lanes_to_store > Lanes(DFromV<VFromD<D>>()) and
  // max_lanes_to_store < 2 * Lanes(DFromV<VFromD<D>>()) are both true.

  // Also need to make sure that no more than Lanes(d) lanes are stored to p
  // if Lanes(d) < Lanes(DFromV<VFromD<D>>()) is true, which is possible if
  // MaxLanes(d) < MaxLanes(DFromV<VFromD<D>>()) or
  // d.Pow2() < DFromV<VFromD<D>>().Pow2() is true.
  detail::StoreN(CappedLanes(d, max_lanes_to_store), v, d, p);
}

// ------------------------------ StoreU
template <class V, class D>
HWY_API void StoreU(const V v, D d, TFromD<D>* HWY_RESTRICT p) {
  // RVV only requires element alignment, not vector alignment.
  Store(v, d, p);
}

// ------------------------------ Stream
template <class V, class D, typename T>
HWY_API void Stream(const V v, D d, T* HWY_RESTRICT aligned) {
  Store(v, d, aligned);
}

// ------------------------------ ScatterOffset

#ifdef HWY_NATIVE_SCATTER
#undef HWY_NATIVE_SCATTER
#else
#define HWY_NATIVE_SCATTER
#endif

#define HWY_RVV_SCATTER(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,    \
                        SHIFT, MLEN, NAME, OP)                              \
  template <size_t N>                                                       \
  HWY_API void NAME(HWY_RVV_V(BASE, SEW, LMUL) v,                           \
                    HWY_RVV_D(BASE, SEW, N, SHIFT) d,                       \
                    HWY_RVV_T(BASE, SEW) * HWY_RESTRICT base,               \
                    HWY_RVV_V(int, SEW, LMUL) offset) {                     \
    const RebindToUnsigned<decltype(d)> du;                                 \
    return __riscv_v##OP##ei##SEW##_v_##CHAR##SEW##LMUL(                    \
        detail::NativeLanePointer(base), BitCast(du, offset), v, Lanes(d)); \
  }
HWY_RVV_FOREACH(HWY_RVV_SCATTER, ScatterOffset, sux, _ALL_VIRT)
#undef HWY_RVV_SCATTER

// ------------------------------ ScatterIndex
template <class D>
HWY_API void ScatterIndex(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT base,
                          VFromD<RebindToSigned<D>> indices) {
  constexpr size_t kBits = CeilLog2(sizeof(TFromD<D>));
  return ScatterOffset(v, d, base, ShiftLeft<kBits>(indices));
}

// ------------------------------ MaskedScatterIndex

#define HWY_RVV_MASKED_SCATTER(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, \
                               LMULH, SHIFT, MLEN, NAME, OP)             \
  template <size_t N>                                                    \
  HWY_API void NAME(HWY_RVV_V(BASE, SEW, LMUL) v, HWY_RVV_M(MLEN) m,     \
                    HWY_RVV_D(BASE, SEW, N, SHIFT) d,                    \
                    HWY_RVV_T(BASE, SEW) * HWY_RESTRICT base,            \
                    HWY_RVV_V(int, SEW, LMUL) indices) {                 \
    const RebindToUnsigned<decltype(d)> du;                              \
    constexpr size_t kBits = CeilLog2(sizeof(TFromD<decltype(d)>));      \
    return __riscv_v##OP##ei##SEW##_v_##CHAR##SEW##LMUL##_m(             \
        m, detail::NativeLanePointer(base),                              \
        ShiftLeft<kBits>(BitCast(du, indices)), v, Lanes(d));            \
  }
HWY_RVV_FOREACH(HWY_RVV_MASKED_SCATTER, MaskedScatterIndex, sux, _ALL_VIRT)
#undef HWY_RVV_MASKED_SCATTER

// ------------------------------ GatherOffset

#ifdef HWY_NATIVE_GATHER
#undef HWY_NATIVE_GATHER
#else
#define HWY_NATIVE_GATHER
#endif

#define HWY_RVV_GATHER(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                       MLEN, NAME, OP)                                         \
  template <size_t N>                                                          \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                           \
      NAME(HWY_RVV_D(BASE, SEW, N, SHIFT) d,                                   \
           const HWY_RVV_T(BASE, SEW) * HWY_RESTRICT base,                     \
           HWY_RVV_V(int, SEW, LMUL) offset) {                                 \
    const RebindToUnsigned<decltype(d)> du;                                    \
    return __riscv_v##OP##ei##SEW##_v_##CHAR##SEW##LMUL(                       \
        detail::NativeLanePointer(base), BitCast(du, offset), Lanes(d));       \
  }
HWY_RVV_FOREACH(HWY_RVV_GATHER, GatherOffset, lux, _ALL_VIRT)
#undef HWY_RVV_GATHER

// ------------------------------ GatherIndex

template <class D>
HWY_API VFromD<D> GatherIndex(D d, const TFromD<D>* HWY_RESTRICT base,
                              const VFromD<RebindToSigned<D>> index) {
  constexpr size_t kBits = CeilLog2(sizeof(TFromD<D>));
  return GatherOffset(d, base, ShiftLeft<kBits>(index));
}

// ------------------------------ MaskedGatherIndexOr

#define HWY_RVV_MASKED_GATHER(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                              SHIFT, MLEN, NAME, OP)                           \
  template <size_t N>                                                          \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                           \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) no, HWY_RVV_M(MLEN) m,                   \
           HWY_RVV_D(BASE, SEW, N, SHIFT) d,                                   \
           const HWY_RVV_T(BASE, SEW) * HWY_RESTRICT base,                     \
           HWY_RVV_V(int, SEW, LMUL) indices) {                                \
    const RebindToUnsigned<decltype(d)> du;                                    \
    const RebindToSigned<decltype(d)> di;                                      \
    (void)di; /* for HWY_DASSERT */                                            \
    constexpr size_t kBits = CeilLog2(SEW / 8);                                \
    HWY_DASSERT(AllFalse(di, Lt(indices, Zero(di))));                          \
    return __riscv_v##OP##ei##SEW##_v_##CHAR##SEW##LMUL##_mu(                  \
        m, no, detail::NativeLanePointer(base),                                \
        ShiftLeft<kBits>(BitCast(du, indices)), Lanes(d));                     \
  }
HWY_RVV_FOREACH(HWY_RVV_MASKED_GATHER, MaskedGatherIndexOr, lux, _ALL_VIRT)
#undef HWY_RVV_MASKED_GATHER

template <class D>
HWY_API VFromD<D> MaskedGatherIndex(MFromD<D> m, D d, const TFromD<D>* base,
                                    VFromD<RebindToSigned<D>> indices) {
  return MaskedGatherIndexOr(Zero(d), m, d, base, indices);
}

// ================================================== CONVERT

// ------------------------------ PromoteTo

// SEW is for the input.
#define HWY_RVV_PROMOTE(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,     \
                        SHIFT, MLEN, NAME, OP)                               \
  template <size_t N>                                                        \
  HWY_API HWY_RVV_V(BASE, SEWD, LMULD) NAME(                                 \
      HWY_RVV_D(BASE, SEWD, N, SHIFT + 1) d, HWY_RVV_V(BASE, SEW, LMUL) v) { \
    return __riscv_v##OP##CHAR##SEWD##LMULD(v, Lanes(d));                    \
  }

HWY_RVV_FOREACH_U08(HWY_RVV_PROMOTE, PromoteTo, zext_vf2_, _EXT_VIRT)
HWY_RVV_FOREACH_U16(HWY_RVV_PROMOTE, PromoteTo, zext_vf2_, _EXT_VIRT)
HWY_RVV_FOREACH_U32(HWY_RVV_PROMOTE, PromoteTo, zext_vf2_, _EXT_VIRT)
HWY_RVV_FOREACH_I08(HWY_RVV_PROMOTE, PromoteTo, sext_vf2_, _EXT_VIRT)
HWY_RVV_FOREACH_I16(HWY_RVV_PROMOTE, PromoteTo, sext_vf2_, _EXT_VIRT)
HWY_RVV_FOREACH_I32(HWY_RVV_PROMOTE, PromoteTo, sext_vf2_, _EXT_VIRT)
HWY_RVV_FOREACH_F32(HWY_RVV_PROMOTE, PromoteTo, fwcvt_f_f_v_, _EXT_VIRT)

#if HWY_HAVE_FLOAT16 || HWY_RVV_HAVE_F16C

HWY_RVV_FOREACH_F16_UNCONDITIONAL(HWY_RVV_PROMOTE, PromoteTo, fwcvt_f_f_v_,
                                  _EXT_VIRT)

// Per-target flag to prevent generic_ops-inl.h from defining f16 conversions.
#ifdef HWY_NATIVE_F16C
#undef HWY_NATIVE_F16C
#else
#define HWY_NATIVE_F16C
#endif
#endif  // HWY_HAVE_FLOAT16 || HWY_RVV_HAVE_F16C

#undef HWY_RVV_PROMOTE

// The above X-macro cannot handle 4x promotion nor type switching.
// TODO(janwas): use BASE2 arg to allow the latter.
#define HWY_RVV_PROMOTE(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN, LMUL, LMUL_IN, \
                        SHIFT, ADD)                                            \
  template <size_t N>                                                          \
  HWY_API HWY_RVV_V(BASE, BITS, LMUL)                                          \
      PromoteTo(HWY_RVV_D(BASE, BITS, N, SHIFT + ADD) d,                       \
                HWY_RVV_V(BASE_IN, BITS_IN, LMUL_IN) v) {                      \
    return __riscv_v##OP##CHAR##BITS##LMUL(v, Lanes(d));                       \
  }

#define HWY_RVV_PROMOTE_X2(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN)        \
  HWY_RVV_PROMOTE(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN, m1, mf2, -2, 1) \
  HWY_RVV_PROMOTE(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN, m1, mf2, -1, 1) \
  HWY_RVV_PROMOTE(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN, m2, m1, 0, 1)   \
  HWY_RVV_PROMOTE(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN, m4, m2, 1, 1)   \
  HWY_RVV_PROMOTE(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN, m8, m4, 2, 1)

#define HWY_RVV_PROMOTE_X4(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN)        \
  HWY_RVV_PROMOTE(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN, m1, mf4, -2, 2) \
  HWY_RVV_PROMOTE(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN, m2, mf2, -1, 2) \
  HWY_RVV_PROMOTE(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN, m4, m1, 0, 2)   \
  HWY_RVV_PROMOTE(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN, m8, m2, 1, 2)

#define HWY_RVV_PROMOTE_X4_FROM_U8(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN) \
  HWY_RVV_PROMOTE(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN, mf2, mf8, -3, 2) \
  HWY_RVV_PROMOTE_X4(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN)

#define HWY_RVV_PROMOTE_X8(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN)        \
  HWY_RVV_PROMOTE(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN, m1, mf8, -3, 3) \
  HWY_RVV_PROMOTE(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN, m2, mf4, -2, 3) \
  HWY_RVV_PROMOTE(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN, m4, mf2, -1, 3) \
  HWY_RVV_PROMOTE(OP, BASE, CHAR, BITS, BASE_IN, BITS_IN, m8, m1, 0, 3)

HWY_RVV_PROMOTE_X8(zext_vf8_, uint, u, 64, uint, 8)
HWY_RVV_PROMOTE_X8(sext_vf8_, int, i, 64, int, 8)

HWY_RVV_PROMOTE_X4_FROM_U8(zext_vf4_, uint, u, 32, uint, 8)
HWY_RVV_PROMOTE_X4_FROM_U8(sext_vf4_, int, i, 32, int, 8)
HWY_RVV_PROMOTE_X4(zext_vf4_, uint, u, 64, uint, 16)
HWY_RVV_PROMOTE_X4(sext_vf4_, int, i, 64, int, 16)

// i32 to f64
HWY_RVV_PROMOTE_X2(fwcvt_f_x_v_, float, f, 64, int, 32)

// u32 to f64
HWY_RVV_PROMOTE_X2(fwcvt_f_xu_v_, float, f, 64, uint, 32)

// f32 to i64
HWY_RVV_PROMOTE_X2(fwcvt_rtz_x_f_v_, int, i, 64, float, 32)

// f32 to u64
HWY_RVV_PROMOTE_X2(fwcvt_rtz_xu_f_v_, uint, u, 64, float, 32)

#undef HWY_RVV_PROMOTE_X8
#undef HWY_RVV_PROMOTE_X4_FROM_U8
#undef HWY_RVV_PROMOTE_X4
#undef HWY_RVV_PROMOTE_X2
#undef HWY_RVV_PROMOTE

// I16->I64 or U16->U64 PromoteTo with virtual LMUL
template <size_t N>
HWY_API auto PromoteTo(Simd<int64_t, N, -1> d,
                       VFromD<Rebind<int16_t, decltype(d)>> v)
    -> VFromD<decltype(d)> {
  return PromoteTo(ScalableTag<int64_t>(), v);
}

template <size_t N>
HWY_API auto PromoteTo(Simd<uint64_t, N, -1> d,
                       VFromD<Rebind<uint16_t, decltype(d)>> v)
    -> VFromD<decltype(d)> {
  return PromoteTo(ScalableTag<uint64_t>(), v);
}

// Unsigned to signed: cast for unsigned promote.
template <class D, HWY_IF_I16_D(D)>
HWY_API VFromD<D> PromoteTo(D d, VFromD<Rebind<uint8_t, D>> v) {
  return BitCast(d, PromoteTo(RebindToUnsigned<decltype(d)>(), v));
}

template <class D, HWY_IF_I32_D(D)>
HWY_API VFromD<D> PromoteTo(D d, VFromD<Rebind<uint8_t, D>> v) {
  return BitCast(d, PromoteTo(RebindToUnsigned<decltype(d)>(), v));
}

template <class D, HWY_IF_I32_D(D)>
HWY_API VFromD<D> PromoteTo(D d, VFromD<Rebind<uint16_t, D>> v) {
  return BitCast(d, PromoteTo(RebindToUnsigned<decltype(d)>(), v));
}

template <class D, HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteTo(D d, VFromD<Rebind<uint32_t, D>> v) {
  return BitCast(d, PromoteTo(RebindToUnsigned<decltype(d)>(), v));
}

template <class D, HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteTo(D d, VFromD<Rebind<uint16_t, D>> v) {
  return BitCast(d, PromoteTo(RebindToUnsigned<decltype(d)>(), v));
}

template <class D, HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteTo(D d, VFromD<Rebind<uint8_t, D>> v) {
  return BitCast(d, PromoteTo(RebindToUnsigned<decltype(d)>(), v));
}

template <class D, HWY_IF_F32_D(D)>
HWY_API VFromD<D> PromoteTo(D d, VFromD<Rebind<hwy::bfloat16_t, D>> v) {
  const RebindToSigned<decltype(d)> di32;
  const Rebind<uint16_t, decltype(d)> du16;
  return BitCast(d, ShiftLeft<16>(PromoteTo(di32, BitCast(du16, v))));
}

// ------------------------------ DemoteTo U

// SEW is for the source so we can use _DEMOTE_VIRT.
#define HWY_RVV_DEMOTE(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                       MLEN, NAME, OP)                                         \
  template <size_t N>                                                          \
  HWY_API HWY_RVV_V(BASE, SEWH, LMULH) NAME(                                   \
      HWY_RVV_D(BASE, SEWH, N, SHIFT - 1) d, HWY_RVV_V(BASE, SEW, LMUL) v) {   \
    return __riscv_v##OP##CHAR##SEWH##LMULH(                                   \
        v, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, Lanes(d)));                \
  }

// Unsigned -> unsigned
HWY_RVV_FOREACH_U16(HWY_RVV_DEMOTE, DemoteTo, nclipu_wx_, _DEMOTE_VIRT)
HWY_RVV_FOREACH_U32(HWY_RVV_DEMOTE, DemoteTo, nclipu_wx_, _DEMOTE_VIRT)
HWY_RVV_FOREACH_U64(HWY_RVV_DEMOTE, DemoteTo, nclipu_wx_, _DEMOTE_VIRT)

// SEW is for the source so we can use _DEMOTE_VIRT.
#define HWY_RVV_DEMOTE_I_TO_U(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                              SHIFT, MLEN, NAME, OP)                           \
  template <size_t N>                                                          \
  HWY_API HWY_RVV_V(uint, SEWH, LMULH) NAME(                                   \
      HWY_RVV_D(uint, SEWH, N, SHIFT - 1) dn, HWY_RVV_V(int, SEW, LMUL) v) {   \
    const HWY_RVV_D(uint, SEW, N, SHIFT) du;                                   \
    /* First clamp negative numbers to zero to match x86 packus. */            \
    return DemoteTo(dn, BitCast(du, detail::MaxS(v, 0)));                      \
  }
HWY_RVV_FOREACH_I64(HWY_RVV_DEMOTE_I_TO_U, DemoteTo, _, _DEMOTE_VIRT)
HWY_RVV_FOREACH_I32(HWY_RVV_DEMOTE_I_TO_U, DemoteTo, _, _DEMOTE_VIRT)
HWY_RVV_FOREACH_I16(HWY_RVV_DEMOTE_I_TO_U, DemoteTo, _, _DEMOTE_VIRT)
#undef HWY_RVV_DEMOTE_I_TO_U

template <size_t N>
HWY_API vuint8mf8_t DemoteTo(Simd<uint8_t, N, -3> d, const vint32mf2_t v) {
  return __riscv_vnclipu_wx_u8mf8(
      DemoteTo(Simd<uint16_t, N, -2>(), v), 0,
      HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, Lanes(d)));
}
template <size_t N>
HWY_API vuint8mf4_t DemoteTo(Simd<uint8_t, N, -2> d, const vint32m1_t v) {
  return __riscv_vnclipu_wx_u8mf4(
      DemoteTo(Simd<uint16_t, N, -1>(), v), 0,
      HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, Lanes(d)));
}
template <size_t N>
HWY_API vuint8mf2_t DemoteTo(Simd<uint8_t, N, -1> d, const vint32m2_t v) {
  return __riscv_vnclipu_wx_u8mf2(
      DemoteTo(Simd<uint16_t, N, 0>(), v), 0,
      HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, Lanes(d)));
}
template <size_t N>
HWY_API vuint8m1_t DemoteTo(Simd<uint8_t, N, 0> d, const vint32m4_t v) {
  return __riscv_vnclipu_wx_u8m1(
      DemoteTo(Simd<uint16_t, N, 1>(), v), 0,
      HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, Lanes(d)));
}
template <size_t N>
HWY_API vuint8m2_t DemoteTo(Simd<uint8_t, N, 1> d, const vint32m8_t v) {
  return __riscv_vnclipu_wx_u8m2(
      DemoteTo(Simd<uint16_t, N, 2>(), v), 0,
      HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, Lanes(d)));
}

template <size_t N>
HWY_API vuint8mf8_t DemoteTo(Simd<uint8_t, N, -3> d, const vuint32mf2_t v) {
  return __riscv_vnclipu_wx_u8mf8(
      DemoteTo(Simd<uint16_t, N, -2>(), v), 0,
      HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, Lanes(d)));
}
template <size_t N>
HWY_API vuint8mf4_t DemoteTo(Simd<uint8_t, N, -2> d, const vuint32m1_t v) {
  return __riscv_vnclipu_wx_u8mf4(
      DemoteTo(Simd<uint16_t, N, -1>(), v), 0,
      HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, Lanes(d)));
}
template <size_t N>
HWY_API vuint8mf2_t DemoteTo(Simd<uint8_t, N, -1> d, const vuint32m2_t v) {
  return __riscv_vnclipu_wx_u8mf2(
      DemoteTo(Simd<uint16_t, N, 0>(), v), 0,
      HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, Lanes(d)));
}
template <size_t N>
HWY_API vuint8m1_t DemoteTo(Simd<uint8_t, N, 0> d, const vuint32m4_t v) {
  return __riscv_vnclipu_wx_u8m1(
      DemoteTo(Simd<uint16_t, N, 1>(), v), 0,
      HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, Lanes(d)));
}
template <size_t N>
HWY_API vuint8m2_t DemoteTo(Simd<uint8_t, N, 1> d, const vuint32m8_t v) {
  return __riscv_vnclipu_wx_u8m2(
      DemoteTo(Simd<uint16_t, N, 2>(), v), 0,
      HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, Lanes(d)));
}

template <class D, HWY_IF_U8_D(D)>
HWY_API VFromD<D> DemoteTo(D d, VFromD<Rebind<int64_t, D>> v) {
  return DemoteTo(d, DemoteTo(Rebind<uint32_t, D>(), v));
}

template <class D, HWY_IF_U8_D(D)>
HWY_API VFromD<D> DemoteTo(D d, VFromD<Rebind<uint64_t, D>> v) {
  return DemoteTo(d, DemoteTo(Rebind<uint32_t, D>(), v));
}

template <class D, HWY_IF_U16_D(D)>
HWY_API VFromD<D> DemoteTo(D d, VFromD<Rebind<int64_t, D>> v) {
  return DemoteTo(d, DemoteTo(Rebind<uint32_t, D>(), v));
}

template <class D, HWY_IF_U16_D(D)>
HWY_API VFromD<D> DemoteTo(D d, VFromD<Rebind<uint64_t, D>> v) {
  return DemoteTo(d, DemoteTo(Rebind<uint32_t, D>(), v));
}

HWY_API vuint8mf8_t U8FromU32(const vuint32mf2_t v) {
  const size_t avl = Lanes(ScalableTag<uint8_t, -3>());
  return __riscv_vnclipu_wx_u8mf8(
      __riscv_vnclipu_wx_u16mf4(v, 0,
                                HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl)),
      0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}
HWY_API vuint8mf4_t U8FromU32(const vuint32m1_t v) {
  const size_t avl = Lanes(ScalableTag<uint8_t, -2>());
  return __riscv_vnclipu_wx_u8mf4(
      __riscv_vnclipu_wx_u16mf2(v, 0,
                                HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl)),
      0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}
HWY_API vuint8mf2_t U8FromU32(const vuint32m2_t v) {
  const size_t avl = Lanes(ScalableTag<uint8_t, -1>());
  return __riscv_vnclipu_wx_u8mf2(
      __riscv_vnclipu_wx_u16m1(v, 0,
                               HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl)),
      0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}
HWY_API vuint8m1_t U8FromU32(const vuint32m4_t v) {
  const size_t avl = Lanes(ScalableTag<uint8_t, 0>());
  return __riscv_vnclipu_wx_u8m1(
      __riscv_vnclipu_wx_u16m2(v, 0,
                               HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl)),
      0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}
HWY_API vuint8m2_t U8FromU32(const vuint32m8_t v) {
  const size_t avl = Lanes(ScalableTag<uint8_t, 1>());
  return __riscv_vnclipu_wx_u8m2(
      __riscv_vnclipu_wx_u16m4(v, 0,
                               HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl)),
      0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

// ------------------------------ Truncations

template <size_t N>
HWY_API vuint8mf8_t TruncateTo(Simd<uint8_t, N, -3> d,
                               const VFromD<Simd<uint64_t, N, 0>> v) {
  const size_t avl = Lanes(d);
  const vuint64m1_t v1 = __riscv_vand(v, 0xFF, avl);
  const vuint32mf2_t v2 = __riscv_vnclipu_wx_u32mf2(
      v1, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  const vuint16mf4_t v3 = __riscv_vnclipu_wx_u16mf4(
      v2, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  return __riscv_vnclipu_wx_u8mf8(v3, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint8mf4_t TruncateTo(Simd<uint8_t, N, -2> d,
                               const VFromD<Simd<uint64_t, N, 1>> v) {
  const size_t avl = Lanes(d);
  const vuint64m2_t v1 = __riscv_vand(v, 0xFF, avl);
  const vuint32m1_t v2 = __riscv_vnclipu_wx_u32m1(
      v1, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  const vuint16mf2_t v3 = __riscv_vnclipu_wx_u16mf2(
      v2, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  return __riscv_vnclipu_wx_u8mf4(v3, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint8mf2_t TruncateTo(Simd<uint8_t, N, -1> d,
                               const VFromD<Simd<uint64_t, N, 2>> v) {
  const size_t avl = Lanes(d);
  const vuint64m4_t v1 = __riscv_vand(v, 0xFF, avl);
  const vuint32m2_t v2 = __riscv_vnclipu_wx_u32m2(
      v1, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  const vuint16m1_t v3 = __riscv_vnclipu_wx_u16m1(
      v2, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  return __riscv_vnclipu_wx_u8mf2(v3, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint8m1_t TruncateTo(Simd<uint8_t, N, 0> d,
                              const VFromD<Simd<uint64_t, N, 3>> v) {
  const size_t avl = Lanes(d);
  const vuint64m8_t v1 = __riscv_vand(v, 0xFF, avl);
  const vuint32m4_t v2 = __riscv_vnclipu_wx_u32m4(
      v1, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  const vuint16m2_t v3 = __riscv_vnclipu_wx_u16m2(
      v2, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  return __riscv_vnclipu_wx_u8m1(v3, 0,
                                 HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint16mf4_t TruncateTo(Simd<uint16_t, N, -3> d,
                                const VFromD<Simd<uint64_t, N, -1>> v) {
  const size_t avl = Lanes(d);
  const vuint64m1_t v1 = __riscv_vand(v, 0xFFFF, avl);
  const vuint32mf2_t v2 = __riscv_vnclipu_wx_u32mf2(
      v1, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  return __riscv_vnclipu_wx_u16mf4(v2, 0,
                                   HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint16mf4_t TruncateTo(Simd<uint16_t, N, -2> d,
                                const VFromD<Simd<uint64_t, N, 0>> v) {
  const size_t avl = Lanes(d);
  const vuint64m1_t v1 = __riscv_vand(v, 0xFFFF, avl);
  const vuint32mf2_t v2 = __riscv_vnclipu_wx_u32mf2(
      v1, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  return __riscv_vnclipu_wx_u16mf4(v2, 0,
                                   HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint16mf2_t TruncateTo(Simd<uint16_t, N, -1> d,
                                const VFromD<Simd<uint64_t, N, 1>> v) {
  const size_t avl = Lanes(d);
  const vuint64m2_t v1 = __riscv_vand(v, 0xFFFF, avl);
  const vuint32m1_t v2 = __riscv_vnclipu_wx_u32m1(
      v1, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  return __riscv_vnclipu_wx_u16mf2(v2, 0,
                                   HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint16m1_t TruncateTo(Simd<uint16_t, N, 0> d,
                               const VFromD<Simd<uint64_t, N, 2>> v) {
  const size_t avl = Lanes(d);
  const vuint64m4_t v1 = __riscv_vand(v, 0xFFFF, avl);
  const vuint32m2_t v2 = __riscv_vnclipu_wx_u32m2(
      v1, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  return __riscv_vnclipu_wx_u16m1(v2, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint16m2_t TruncateTo(Simd<uint16_t, N, 1> d,
                               const VFromD<Simd<uint64_t, N, 3>> v) {
  const size_t avl = Lanes(d);
  const vuint64m8_t v1 = __riscv_vand(v, 0xFFFF, avl);
  const vuint32m4_t v2 = __riscv_vnclipu_wx_u32m4(
      v1, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  return __riscv_vnclipu_wx_u16m2(v2, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint32mf2_t TruncateTo(Simd<uint32_t, N, -2> d,
                                const VFromD<Simd<uint64_t, N, -1>> v) {
  const size_t avl = Lanes(d);
  const vuint64m1_t v1 = __riscv_vand(v, 0xFFFFFFFFu, avl);
  return __riscv_vnclipu_wx_u32mf2(v1, 0,
                                   HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint32mf2_t TruncateTo(Simd<uint32_t, N, -1> d,
                                const VFromD<Simd<uint64_t, N, 0>> v) {
  const size_t avl = Lanes(d);
  const vuint64m1_t v1 = __riscv_vand(v, 0xFFFFFFFFu, avl);
  return __riscv_vnclipu_wx_u32mf2(v1, 0,
                                   HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint32m1_t TruncateTo(Simd<uint32_t, N, 0> d,
                               const VFromD<Simd<uint64_t, N, 1>> v) {
  const size_t avl = Lanes(d);
  const vuint64m2_t v1 = __riscv_vand(v, 0xFFFFFFFFu, avl);
  return __riscv_vnclipu_wx_u32m1(v1, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint32m2_t TruncateTo(Simd<uint32_t, N, 1> d,
                               const VFromD<Simd<uint64_t, N, 2>> v) {
  const size_t avl = Lanes(d);
  const vuint64m4_t v1 = __riscv_vand(v, 0xFFFFFFFFu, avl);
  return __riscv_vnclipu_wx_u32m2(v1, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint32m4_t TruncateTo(Simd<uint32_t, N, 2> d,
                               const VFromD<Simd<uint64_t, N, 3>> v) {
  const size_t avl = Lanes(d);
  const vuint64m8_t v1 = __riscv_vand(v, 0xFFFFFFFFu, avl);
  return __riscv_vnclipu_wx_u32m4(v1, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint8mf8_t TruncateTo(Simd<uint8_t, N, -3> d,
                               const VFromD<Simd<uint32_t, N, -1>> v) {
  const size_t avl = Lanes(d);
  const vuint32mf2_t v1 = __riscv_vand(v, 0xFF, avl);
  const vuint16mf4_t v2 = __riscv_vnclipu_wx_u16mf4(
      v1, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  return __riscv_vnclipu_wx_u8mf8(v2, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint8mf4_t TruncateTo(Simd<uint8_t, N, -2> d,
                               const VFromD<Simd<uint32_t, N, 0>> v) {
  const size_t avl = Lanes(d);
  const vuint32m1_t v1 = __riscv_vand(v, 0xFF, avl);
  const vuint16mf2_t v2 = __riscv_vnclipu_wx_u16mf2(
      v1, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  return __riscv_vnclipu_wx_u8mf4(v2, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint8mf2_t TruncateTo(Simd<uint8_t, N, -1> d,
                               const VFromD<Simd<uint32_t, N, 1>> v) {
  const size_t avl = Lanes(d);
  const vuint32m2_t v1 = __riscv_vand(v, 0xFF, avl);
  const vuint16m1_t v2 = __riscv_vnclipu_wx_u16m1(
      v1, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  return __riscv_vnclipu_wx_u8mf2(v2, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint8m1_t TruncateTo(Simd<uint8_t, N, 0> d,
                              const VFromD<Simd<uint32_t, N, 2>> v) {
  const size_t avl = Lanes(d);
  const vuint32m4_t v1 = __riscv_vand(v, 0xFF, avl);
  const vuint16m2_t v2 = __riscv_vnclipu_wx_u16m2(
      v1, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  return __riscv_vnclipu_wx_u8m1(v2, 0,
                                 HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint8m2_t TruncateTo(Simd<uint8_t, N, 1> d,
                              const VFromD<Simd<uint32_t, N, 3>> v) {
  const size_t avl = Lanes(d);
  const vuint32m8_t v1 = __riscv_vand(v, 0xFF, avl);
  const vuint16m4_t v2 = __riscv_vnclipu_wx_u16m4(
      v1, 0, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
  return __riscv_vnclipu_wx_u8m2(v2, 0,
                                 HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint16mf4_t TruncateTo(Simd<uint16_t, N, -3> d,
                                const VFromD<Simd<uint32_t, N, -2>> v) {
  const size_t avl = Lanes(d);
  const vuint32mf2_t v1 = __riscv_vand(v, 0xFFFF, avl);
  return __riscv_vnclipu_wx_u16mf4(v1, 0,
                                   HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint16mf4_t TruncateTo(Simd<uint16_t, N, -2> d,
                                const VFromD<Simd<uint32_t, N, -1>> v) {
  const size_t avl = Lanes(d);
  const vuint32mf2_t v1 = __riscv_vand(v, 0xFFFF, avl);
  return __riscv_vnclipu_wx_u16mf4(v1, 0,
                                   HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint16mf2_t TruncateTo(Simd<uint16_t, N, -1> d,
                                const VFromD<Simd<uint32_t, N, 0>> v) {
  const size_t avl = Lanes(d);
  const vuint32m1_t v1 = __riscv_vand(v, 0xFFFF, avl);
  return __riscv_vnclipu_wx_u16mf2(v1, 0,
                                   HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint16m1_t TruncateTo(Simd<uint16_t, N, 0> d,
                               const VFromD<Simd<uint32_t, N, 1>> v) {
  const size_t avl = Lanes(d);
  const vuint32m2_t v1 = __riscv_vand(v, 0xFFFF, avl);
  return __riscv_vnclipu_wx_u16m1(v1, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint16m2_t TruncateTo(Simd<uint16_t, N, 1> d,
                               const VFromD<Simd<uint32_t, N, 2>> v) {
  const size_t avl = Lanes(d);
  const vuint32m4_t v1 = __riscv_vand(v, 0xFFFF, avl);
  return __riscv_vnclipu_wx_u16m2(v1, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint16m4_t TruncateTo(Simd<uint16_t, N, 2> d,
                               const VFromD<Simd<uint32_t, N, 3>> v) {
  const size_t avl = Lanes(d);
  const vuint32m8_t v1 = __riscv_vand(v, 0xFFFF, avl);
  return __riscv_vnclipu_wx_u16m4(v1, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint8mf8_t TruncateTo(Simd<uint8_t, N, -3> d,
                               const VFromD<Simd<uint16_t, N, -2>> v) {
  const size_t avl = Lanes(d);
  const vuint16mf4_t v1 = __riscv_vand(v, 0xFF, avl);
  return __riscv_vnclipu_wx_u8mf8(v1, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint8mf4_t TruncateTo(Simd<uint8_t, N, -2> d,
                               const VFromD<Simd<uint16_t, N, -1>> v) {
  const size_t avl = Lanes(d);
  const vuint16mf2_t v1 = __riscv_vand(v, 0xFF, avl);
  return __riscv_vnclipu_wx_u8mf4(v1, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint8mf2_t TruncateTo(Simd<uint8_t, N, -1> d,
                               const VFromD<Simd<uint16_t, N, 0>> v) {
  const size_t avl = Lanes(d);
  const vuint16m1_t v1 = __riscv_vand(v, 0xFF, avl);
  return __riscv_vnclipu_wx_u8mf2(v1, 0,
                                  HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint8m1_t TruncateTo(Simd<uint8_t, N, 0> d,
                              const VFromD<Simd<uint16_t, N, 1>> v) {
  const size_t avl = Lanes(d);
  const vuint16m2_t v1 = __riscv_vand(v, 0xFF, avl);
  return __riscv_vnclipu_wx_u8m1(v1, 0,
                                 HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint8m2_t TruncateTo(Simd<uint8_t, N, 1> d,
                              const VFromD<Simd<uint16_t, N, 2>> v) {
  const size_t avl = Lanes(d);
  const vuint16m4_t v1 = __riscv_vand(v, 0xFF, avl);
  return __riscv_vnclipu_wx_u8m2(v1, 0,
                                 HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

template <size_t N>
HWY_API vuint8m4_t TruncateTo(Simd<uint8_t, N, 2> d,
                              const VFromD<Simd<uint16_t, N, 3>> v) {
  const size_t avl = Lanes(d);
  const vuint16m8_t v1 = __riscv_vand(v, 0xFF, avl);
  return __riscv_vnclipu_wx_u8m4(v1, 0,
                                 HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RDN, avl));
}

// ------------------------------ DemoteTo I

HWY_RVV_FOREACH_I16(HWY_RVV_DEMOTE, DemoteTo, nclip_wx_, _DEMOTE_VIRT)
HWY_RVV_FOREACH_I32(HWY_RVV_DEMOTE, DemoteTo, nclip_wx_, _DEMOTE_VIRT)
HWY_RVV_FOREACH_I64(HWY_RVV_DEMOTE, DemoteTo, nclip_wx_, _DEMOTE_VIRT)

template <size_t N>
HWY_API vint8mf8_t DemoteTo(Simd<int8_t, N, -3> d, const vint32mf2_t v) {
  return DemoteTo(d, DemoteTo(Simd<int16_t, N, -2>(), v));
}
template <size_t N>
HWY_API vint8mf4_t DemoteTo(Simd<int8_t, N, -2> d, const vint32m1_t v) {
  return DemoteTo(d, DemoteTo(Simd<int16_t, N, -1>(), v));
}
template <size_t N>
HWY_API vint8mf2_t DemoteTo(Simd<int8_t, N, -1> d, const vint32m2_t v) {
  return DemoteTo(d, DemoteTo(Simd<int16_t, N, 0>(), v));
}
template <size_t N>
HWY_API vint8m1_t DemoteTo(Simd<int8_t, N, 0> d, const vint32m4_t v) {
  return DemoteTo(d, DemoteTo(Simd<int16_t, N, 1>(), v));
}
template <size_t N>
HWY_API vint8m2_t DemoteTo(Simd<int8_t, N, 1> d, const vint32m8_t v) {
  return DemoteTo(d, DemoteTo(Simd<int16_t, N, 2>(), v));
}

template <class D, HWY_IF_I8_D(D)>
HWY_API VFromD<D> DemoteTo(D d, VFromD<Rebind<int64_t, D>> v) {
  return DemoteTo(d, DemoteTo(Rebind<int32_t, D>(), v));
}

template <class D, HWY_IF_I16_D(D)>
HWY_API VFromD<D> DemoteTo(D d, VFromD<Rebind<int64_t, D>> v) {
  return DemoteTo(d, DemoteTo(Rebind<int32_t, D>(), v));
}

#undef HWY_RVV_DEMOTE

// ------------------------------ DemoteTo F

// SEW is for the source so we can use _DEMOTE_VIRT.
#define HWY_RVV_DEMOTE_F(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,    \
                         SHIFT, MLEN, NAME, OP)                              \
  template <size_t N>                                                        \
  HWY_API HWY_RVV_V(BASE, SEWH, LMULH) NAME(                                 \
      HWY_RVV_D(BASE, SEWH, N, SHIFT - 1) d, HWY_RVV_V(BASE, SEW, LMUL) v) { \
    return __riscv_v##OP##SEWH##LMULH(v, Lanes(d));                          \
  }

#if HWY_HAVE_FLOAT16 || HWY_RVV_HAVE_F16C
HWY_RVV_FOREACH_F32(HWY_RVV_DEMOTE_F, DemoteTo, fncvt_f_f_w_f, _DEMOTE_VIRT)
#endif
HWY_RVV_FOREACH_F64(HWY_RVV_DEMOTE_F, DemoteTo, fncvt_f_f_w_f, _DEMOTE_VIRT)

namespace detail {
HWY_RVV_FOREACH_F64(HWY_RVV_DEMOTE_F, DemoteToF32WithRoundToOdd,
                    fncvt_rod_f_f_w_f, _DEMOTE_VIRT)
}  // namespace detail

#undef HWY_RVV_DEMOTE_F

// TODO(janwas): add BASE2 arg to allow generating this via DEMOTE_F.
template <size_t N>
HWY_API vint32mf2_t DemoteTo(Simd<int32_t, N, -2> d, const vfloat64m1_t v) {
  return __riscv_vfncvt_rtz_x_f_w_i32mf2(v, Lanes(d));
}
template <size_t N>
HWY_API vint32mf2_t DemoteTo(Simd<int32_t, N, -1> d, const vfloat64m1_t v) {
  return __riscv_vfncvt_rtz_x_f_w_i32mf2(v, Lanes(d));
}
template <size_t N>
HWY_API vint32m1_t DemoteTo(Simd<int32_t, N, 0> d, const vfloat64m2_t v) {
  return __riscv_vfncvt_rtz_x_f_w_i32m1(v, Lanes(d));
}
template <size_t N>
HWY_API vint32m2_t DemoteTo(Simd<int32_t, N, 1> d, const vfloat64m4_t v) {
  return __riscv_vfncvt_rtz_x_f_w_i32m2(v, Lanes(d));
}
template <size_t N>
HWY_API vint32m4_t DemoteTo(Simd<int32_t, N, 2> d, const vfloat64m8_t v) {
  return __riscv_vfncvt_rtz_x_f_w_i32m4(v, Lanes(d));
}

template <size_t N>
HWY_API vuint32mf2_t DemoteTo(Simd<uint32_t, N, -2> d, const vfloat64m1_t v) {
  return __riscv_vfncvt_rtz_xu_f_w_u32mf2(v, Lanes(d));
}
template <size_t N>
HWY_API vuint32mf2_t DemoteTo(Simd<uint32_t, N, -1> d, const vfloat64m1_t v) {
  return __riscv_vfncvt_rtz_xu_f_w_u32mf2(v, Lanes(d));
}
template <size_t N>
HWY_API vuint32m1_t DemoteTo(Simd<uint32_t, N, 0> d, const vfloat64m2_t v) {
  return __riscv_vfncvt_rtz_xu_f_w_u32m1(v, Lanes(d));
}
template <size_t N>
HWY_API vuint32m2_t DemoteTo(Simd<uint32_t, N, 1> d, const vfloat64m4_t v) {
  return __riscv_vfncvt_rtz_xu_f_w_u32m2(v, Lanes(d));
}
template <size_t N>
HWY_API vuint32m4_t DemoteTo(Simd<uint32_t, N, 2> d, const vfloat64m8_t v) {
  return __riscv_vfncvt_rtz_xu_f_w_u32m4(v, Lanes(d));
}

template <size_t N>
HWY_API vfloat32mf2_t DemoteTo(Simd<float, N, -2> d, const vint64m1_t v) {
  return __riscv_vfncvt_f_x_w_f32mf2(v, Lanes(d));
}
template <size_t N>
HWY_API vfloat32mf2_t DemoteTo(Simd<float, N, -1> d, const vint64m1_t v) {
  return __riscv_vfncvt_f_x_w_f32mf2(v, Lanes(d));
}
template <size_t N>
HWY_API vfloat32m1_t DemoteTo(Simd<float, N, 0> d, const vint64m2_t v) {
  return __riscv_vfncvt_f_x_w_f32m1(v, Lanes(d));
}
template <size_t N>
HWY_API vfloat32m2_t DemoteTo(Simd<float, N, 1> d, const vint64m4_t v) {
  return __riscv_vfncvt_f_x_w_f32m2(v, Lanes(d));
}
template <size_t N>
HWY_API vfloat32m4_t DemoteTo(Simd<float, N, 2> d, const vint64m8_t v) {
  return __riscv_vfncvt_f_x_w_f32m4(v, Lanes(d));
}

template <size_t N>
HWY_API vfloat32mf2_t DemoteTo(Simd<float, N, -2> d, const vuint64m1_t v) {
  return __riscv_vfncvt_f_xu_w_f32mf2(v, Lanes(d));
}
template <size_t N>
HWY_API vfloat32mf2_t DemoteTo(Simd<float, N, -1> d, const vuint64m1_t v) {
  return __riscv_vfncvt_f_xu_w_f32mf2(v, Lanes(d));
}
template <size_t N>
HWY_API vfloat32m1_t DemoteTo(Simd<float, N, 0> d, const vuint64m2_t v) {
  return __riscv_vfncvt_f_xu_w_f32m1(v, Lanes(d));
}
template <size_t N>
HWY_API vfloat32m2_t DemoteTo(Simd<float, N, 1> d, const vuint64m4_t v) {
  return __riscv_vfncvt_f_xu_w_f32m2(v, Lanes(d));
}
template <size_t N>
HWY_API vfloat32m4_t DemoteTo(Simd<float, N, 2> d, const vuint64m8_t v) {
  return __riscv_vfncvt_f_xu_w_f32m4(v, Lanes(d));
}

// Narrows f32 bits to bf16 using round to even.
// SEW is for the source so we can use _DEMOTE_VIRT.
#ifdef HWY_RVV_AVOID_VXRM
#define HWY_RVV_DEMOTE_16_NEAREST_EVEN(BASE, CHAR, SEW, SEWD, SEWH, LMUL,    \
                                       LMULD, LMULH, SHIFT, MLEN, NAME, OP)  \
  template <size_t N>                                                        \
  HWY_API HWY_RVV_V(BASE, SEWH, LMULH) NAME(                                 \
      HWY_RVV_D(BASE, SEWH, N, SHIFT - 1) d, HWY_RVV_V(BASE, SEW, LMUL) v) { \
    const auto round =                                                       \
        detail::AddS(detail::AndS(ShiftRight<16>(v), 1u), 0x7FFFu);          \
    v = Add(v, round);                                                       \
    /* The default rounding mode appears to be RNU=0, which adds the LSB. */ \
    /* Prevent further rounding by clearing the bits we want to truncate. */ \
    v = detail::AndS(v, 0xFFFF0000u);                                        \
    return __riscv_v##OP##CHAR##SEWH##LMULH(v, 16, Lanes(d));                \
  }

#else
#define HWY_RVV_DEMOTE_16_NEAREST_EVEN(BASE, CHAR, SEW, SEWD, SEWH, LMUL,    \
                                       LMULD, LMULH, SHIFT, MLEN, NAME, OP)  \
  template <size_t N>                                                        \
  HWY_API HWY_RVV_V(BASE, SEWH, LMULH) NAME(                                 \
      HWY_RVV_D(BASE, SEWH, N, SHIFT - 1) d, HWY_RVV_V(BASE, SEW, LMUL) v) { \
    return __riscv_v##OP##CHAR##SEWH##LMULH(                                 \
        v, 16, HWY_RVV_INSERT_VXRM(__RISCV_VXRM_RNE, Lanes(d)));             \
  }
#endif  // HWY_RVV_AVOID_VXRM
namespace detail {
HWY_RVV_FOREACH_U32(HWY_RVV_DEMOTE_16_NEAREST_EVEN, DemoteTo16NearestEven,
                    nclipu_wx_, _DEMOTE_VIRT)
}
#undef HWY_RVV_DEMOTE_16_NEAREST_EVEN

#ifdef HWY_NATIVE_DEMOTE_F32_TO_BF16
#undef HWY_NATIVE_DEMOTE_F32_TO_BF16
#else
#define HWY_NATIVE_DEMOTE_F32_TO_BF16
#endif

template <class DBF16, HWY_IF_BF16_D(DBF16)>
HWY_API VFromD<DBF16> DemoteTo(DBF16 d, VFromD<Rebind<float, DBF16>> v) {
  const DFromV<decltype(v)> df;
  const RebindToUnsigned<decltype(df)> du32;
  const RebindToUnsigned<decltype(d)> du16;
  // Consider an f32 mantissa with the upper 7 bits set, followed by a 1-bit
  // and at least one other bit set. This will round to 0 and increment the
  // exponent. If the exponent was already 0xFF (NaN), then the result is -inf;
  // there no wraparound because nclipu saturates. Note that in this case, the
  // input cannot have been inf because its mantissa bits are zero. To avoid
  // converting NaN to inf, we canonicalize the NaN to prevent the rounding.
  const decltype(v) canonicalized =
      IfThenElse(Eq(v, v), v, BitCast(df, Set(du32, 0x7F800000)));
  return BitCast(
      d, detail::DemoteTo16NearestEven(du16, BitCast(du32, canonicalized)));
}

#ifdef HWY_NATIVE_DEMOTE_F64_TO_F16
#undef HWY_NATIVE_DEMOTE_F64_TO_F16
#else
#define HWY_NATIVE_DEMOTE_F64_TO_F16
#endif

template <class D, HWY_IF_F16_D(D)>
HWY_API VFromD<D> DemoteTo(D df16, VFromD<Rebind<double, D>> v) {
  const Rebind<float, decltype(df16)> df32;
  return DemoteTo(df16, detail::DemoteToF32WithRoundToOdd(df32, v));
}

// ------------------------------ ConvertTo F

#define HWY_RVV_CONVERT(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,       \
                        SHIFT, MLEN, NAME, OP)                                 \
  template <size_t N>                                                          \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL) ConvertTo(                                \
      HWY_RVV_D(BASE, SEW, N, SHIFT) d, HWY_RVV_V(int, SEW, LMUL) v) {         \
    return __riscv_vfcvt_f_x_v_f##SEW##LMUL(v, Lanes(d));                      \
  }                                                                            \
  template <size_t N>                                                          \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL) ConvertTo(                                \
      HWY_RVV_D(BASE, SEW, N, SHIFT) d, HWY_RVV_V(uint, SEW, LMUL) v) {        \
    return __riscv_vfcvt_f_xu_v_f##SEW##LMUL(v, Lanes(d));                     \
  }                                                                            \
  /* Truncates (rounds toward zero). */                                        \
  template <size_t N>                                                          \
  HWY_API HWY_RVV_V(int, SEW, LMUL) ConvertTo(HWY_RVV_D(int, SEW, N, SHIFT) d, \
                                              HWY_RVV_V(BASE, SEW, LMUL) v) {  \
    return __riscv_vfcvt_rtz_x_f_v_i##SEW##LMUL(v, Lanes(d));                  \
  }                                                                            \
  template <size_t N>                                                          \
  HWY_API HWY_RVV_V(uint, SEW, LMUL) ConvertTo(                                \
      HWY_RVV_D(uint, SEW, N, SHIFT) d, HWY_RVV_V(BASE, SEW, LMUL) v) {        \
    return __riscv_vfcvt_rtz_xu_f_v_u##SEW##LMUL(v, Lanes(d));                 \
  }

HWY_RVV_FOREACH_F(HWY_RVV_CONVERT, _, _, _ALL_VIRT)
#undef HWY_RVV_CONVERT

// Uses default rounding mode. Must be separate because there is no D arg.
#define HWY_RVV_NEAREST(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,       \
                        SHIFT, MLEN, NAME, OP)                                 \
  HWY_API HWY_RVV_V(int, SEW, LMUL) NearestInt(HWY_RVV_V(BASE, SEW, LMUL) v) { \
    return __riscv_vfcvt_x_f_v_i##SEW##LMUL(v, HWY_RVV_AVL(SEW, SHIFT));       \
  }
HWY_RVV_FOREACH_F(HWY_RVV_NEAREST, _, _, _ALL)
#undef HWY_RVV_NEAREST

template <size_t N>
HWY_API vint32mf2_t DemoteToNearestInt(Simd<int32_t, N, -2> d,
                                       const vfloat64m1_t v) {
  return __riscv_vfncvt_x_f_w_i32mf2(v, Lanes(d));
}
template <size_t N>
HWY_API vint32mf2_t DemoteToNearestInt(Simd<int32_t, N, -1> d,
                                       const vfloat64m1_t v) {
  return __riscv_vfncvt_x_f_w_i32mf2(v, Lanes(d));
}
template <size_t N>
HWY_API vint32m1_t DemoteToNearestInt(Simd<int32_t, N, 0> d,
                                      const vfloat64m2_t v) {
  return __riscv_vfncvt_x_f_w_i32m1(v, Lanes(d));
}
template <size_t N>
HWY_API vint32m2_t DemoteToNearestInt(Simd<int32_t, N, 1> d,
                                      const vfloat64m4_t v) {
  return __riscv_vfncvt_x_f_w_i32m2(v, Lanes(d));
}
template <size_t N>
HWY_API vint32m4_t DemoteToNearestInt(Simd<int32_t, N, 2> d,
                                      const vfloat64m8_t v) {
  return __riscv_vfncvt_x_f_w_i32m4(v, Lanes(d));
}

// ================================================== COMBINE

namespace detail {

// For x86-compatible behaviour mandated by Highway API: TableLookupBytes
// offsets are implicitly relative to the start of their 128-bit block.
template <typename T, size_t N, int kPow2>
HWY_INLINE size_t LanesPerBlock(Simd<T, N, kPow2> d) {
  // kMinVecBytes is the minimum size of VFromD<decltype(d)> in bytes
  constexpr size_t kMinVecBytes =
      ScaleByPower(16, HWY_MAX(HWY_MIN(kPow2, 3), -3));
  // kMinVecLanes is the minimum number of lanes in VFromD<decltype(d)>
  constexpr size_t kMinVecLanes = (kMinVecBytes + sizeof(T) - 1) / sizeof(T);
  // kMaxLpb is the maximum number of lanes per block
  constexpr size_t kMaxLpb = HWY_MIN(16 / sizeof(T), MaxLanes(d));

  // If kMaxLpb <= kMinVecLanes is true, then kMaxLpb <= Lanes(d) is true
  if (kMaxLpb <= kMinVecLanes) return kMaxLpb;

  // Fractional LMUL: Lanes(d) may be smaller than kMaxLpb, so honor that.
  const size_t lanes_per_vec = Lanes(d);
  return HWY_MIN(lanes_per_vec, kMaxLpb);
}

template <class D, class V>
HWY_INLINE V OffsetsOf128BitBlocks(const D d, const V iota0) {
  using T = MakeUnsigned<TFromV<V>>;
  return AndS(iota0, static_cast<T>(~(LanesPerBlock(d) - 1)));
}

template <size_t kLanes, class D>
HWY_INLINE MFromD<D> FirstNPerBlock(D /* tag */) {
  const RebindToUnsigned<D> du;
  const RebindToSigned<D> di;
  using TU = TFromD<decltype(du)>;
  const auto idx_mod = AndS(Iota0(du), static_cast<TU>(LanesPerBlock(du) - 1));
  return LtS(BitCast(di, idx_mod), static_cast<TFromD<decltype(di)>>(kLanes));
}

#define HWY_RVV_SLIDE_UP(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,  \
                         SHIFT, MLEN, NAME, OP)                            \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                       \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) dst, HWY_RVV_V(BASE, SEW, LMUL) src, \
           size_t lanes) {                                                 \
    return __riscv_v##OP##_vx_##CHAR##SEW##LMUL(dst, src, lanes,           \
                                                HWY_RVV_AVL(SEW, SHIFT));  \
  }

#define HWY_RVV_SLIDE_DOWN(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                           SHIFT, MLEN, NAME, OP)                           \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                        \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) src, size_t lanes) {                  \
    return __riscv_v##OP##_vx_##CHAR##SEW##LMUL(src, lanes,                 \
                                                HWY_RVV_AVL(SEW, SHIFT));   \
  }

HWY_RVV_FOREACH(HWY_RVV_SLIDE_UP, SlideUp, slideup, _ALL)
HWY_RVV_FOREACH(HWY_RVV_SLIDE_DOWN, SlideDown, slidedown, _ALL)

#undef HWY_RVV_SLIDE_UP
#undef HWY_RVV_SLIDE_DOWN

}  // namespace detail

// ------------------------------ SlideUpLanes
template <class D>
HWY_API VFromD<D> SlideUpLanes(D d, VFromD<D> v, size_t amt) {
  return detail::SlideUp(Zero(d), v, amt);
}

// ------------------------------ SlideDownLanes
template <class D>
HWY_API VFromD<D> SlideDownLanes(D d, VFromD<D> v, size_t amt) {
  v = detail::SlideDown(v, amt);
  // Zero out upper lanes if v is a partial vector
  if (MaxLanes(d) < MaxLanes(DFromV<decltype(v)>())) {
    v = detail::SlideUp(v, Zero(d), Lanes(d) - amt);
  }
  return v;
}

// ------------------------------ ConcatUpperLower
template <class D, class V>
HWY_API V ConcatUpperLower(D d, const V hi, const V lo) {
  const size_t half = Lanes(d) / 2;
  const V hi_down = detail::SlideDown(hi, half);
  return detail::SlideUp(lo, hi_down, half);
}

// ------------------------------ ConcatLowerLower
template <class D, class V>
HWY_API V ConcatLowerLower(D d, const V hi, const V lo) {
  return detail::SlideUp(lo, hi, Lanes(d) / 2);
}

// ------------------------------ ConcatUpperUpper
template <class D, class V>
HWY_API V ConcatUpperUpper(D d, const V hi, const V lo) {
  const size_t half = Lanes(d) / 2;
  const V hi_down = detail::SlideDown(hi, half);
  const V lo_down = detail::SlideDown(lo, half);
  return detail::SlideUp(lo_down, hi_down, half);
}

// ------------------------------ ConcatLowerUpper
template <class D, class V>
HWY_API V ConcatLowerUpper(D d, const V hi, const V lo) {
  const size_t half = Lanes(d) / 2;
  const V lo_down = detail::SlideDown(lo, half);
  return detail::SlideUp(lo_down, hi, half);
}

// ------------------------------ Combine
template <class D2, class V>
HWY_API VFromD<D2> Combine(D2 d2, const V hi, const V lo) {
  return detail::SlideUp(detail::Ext(d2, lo), detail::Ext(d2, hi),
                         Lanes(d2) / 2);
}

// ------------------------------ ZeroExtendVector
template <class D2, class V>
HWY_API VFromD<D2> ZeroExtendVector(D2 d2, const V lo) {
  return Combine(d2, Xor(lo, lo), lo);
}

// ------------------------------ Lower/UpperHalf

namespace detail {

// RVV may only support LMUL >= SEW/64; returns whether that holds for D. Note
// that SEW = sizeof(T)*8 and LMUL = 1 << d.Pow2(). Add 3 to Pow2 to avoid
// negative shift counts.
template <class D>
constexpr bool IsSupportedLMUL(D d) {
  return (size_t{1} << (d.Pow2() + 3)) >= sizeof(TFromD<D>);
}

}  // namespace detail

// If IsSupportedLMUL, just 'truncate' i.e. halve LMUL.
template <class DH, hwy::EnableIf<detail::IsSupportedLMUL(DH())>* = nullptr>
HWY_API VFromD<DH> LowerHalf(const DH /* tag */, const VFromD<Twice<DH>> v) {
  return detail::Trunc(v);
}

// Otherwise, there is no corresponding intrinsic type (e.g. vuint64mf2_t), and
// the hardware may set "vill" if we attempt such an LMUL. However, the V
// extension on application processors requires Zvl128b, i.e. VLEN >= 128, so it
// still makes sense to have half of an SEW=64 vector. We instead just return
// the vector, and rely on the kPow2 in DH to halve the return value of Lanes().
template <class DH, class V,
          hwy::EnableIf<!detail::IsSupportedLMUL(DH())>* = nullptr>
HWY_API V LowerHalf(const DH /* tag */, const V v) {
  return v;
}

// Same, but without D arg
template <class V>
HWY_API VFromD<Half<DFromV<V>>> LowerHalf(const V v) {
  return LowerHalf(Half<DFromV<V>>(), v);
}

template <class DH>
HWY_API VFromD<DH> UpperHalf(const DH d2, const VFromD<Twice<DH>> v) {
  return LowerHalf(d2, detail::SlideDown(v, Lanes(d2)));
}

// ================================================== SWIZZLE

namespace detail {
// Special instruction for 1 lane is presumably faster?
#define HWY_RVV_SLIDE1(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                       MLEN, NAME, OP)                                         \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL) NAME(HWY_RVV_V(BASE, SEW, LMUL) v) {      \
    return __riscv_v##OP##_##CHAR##SEW##LMUL(v, 0, HWY_RVV_AVL(SEW, SHIFT));   \
  }

HWY_RVV_FOREACH_UI(HWY_RVV_SLIDE1, Slide1Up, slide1up_vx, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_SLIDE1, Slide1Up, fslide1up_vf, _ALL)
HWY_RVV_FOREACH_UI(HWY_RVV_SLIDE1, Slide1Down, slide1down_vx, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_SLIDE1, Slide1Down, fslide1down_vf, _ALL)
#undef HWY_RVV_SLIDE1
}  // namespace detail

// ------------------------------ Slide1Up and Slide1Down
#ifdef HWY_NATIVE_SLIDE1_UP_DOWN
#undef HWY_NATIVE_SLIDE1_UP_DOWN
#else
#define HWY_NATIVE_SLIDE1_UP_DOWN
#endif

template <class D>
HWY_API VFromD<D> Slide1Up(D /*d*/, VFromD<D> v) {
  return detail::Slide1Up(v);
}

template <class D>
HWY_API VFromD<D> Slide1Down(D d, VFromD<D> v) {
  v = detail::Slide1Down(v);
  // Zero out upper lanes if v is a partial vector
  if (MaxLanes(d) < MaxLanes(DFromV<decltype(v)>())) {
    v = detail::SlideUp(v, Zero(d), Lanes(d) - 1);
  }
  return v;
}

// ------------------------------ GetLane

#define HWY_RVV_GET_LANE(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,     \
                         SHIFT, MLEN, NAME, OP)                               \
  HWY_API HWY_RVV_T(BASE, SEW) NAME(HWY_RVV_V(BASE, SEW, LMUL) v) {           \
    return __riscv_v##OP##_s_##CHAR##SEW##LMUL##_##CHAR##SEW(v); /* no AVL */ \
  }

HWY_RVV_FOREACH_UI(HWY_RVV_GET_LANE, GetLane, mv_x, _ALL)
HWY_RVV_FOREACH_F(HWY_RVV_GET_LANE, GetLane, fmv_f, _ALL)
#undef HWY_RVV_GET_LANE

// ------------------------------ ExtractLane
template <class V>
HWY_API TFromV<V> ExtractLane(const V v, size_t i) {
  return GetLane(detail::SlideDown(v, i));
}

// ------------------------------ Additional mask logical operations

HWY_RVV_FOREACH_B(HWY_RVV_RETM_ARGM, SetOnlyFirst, sof)
HWY_RVV_FOREACH_B(HWY_RVV_RETM_ARGM, SetBeforeFirst, sbf)
HWY_RVV_FOREACH_B(HWY_RVV_RETM_ARGM, SetAtOrBeforeFirst, sif)

#define HWY_RVV_SET_AT_OR_AFTER_FIRST(SEW, SHIFT, MLEN, NAME, OP) \
  HWY_API HWY_RVV_M(MLEN) SetAtOrAfterFirst(HWY_RVV_M(MLEN) m) {  \
    return Not(SetBeforeFirst(m));                                \
  }

HWY_RVV_FOREACH_B(HWY_RVV_SET_AT_OR_AFTER_FIRST, _, _)
#undef HWY_RVV_SET_AT_OR_AFTER_FIRST

// ------------------------------ InsertLane

// T template arg because TFromV<V> might not match the hwy::float16_t argument.
template <class V, typename T, HWY_IF_NOT_T_SIZE_V(V, 1)>
HWY_API V InsertLane(const V v, size_t i, T t) {
  const Rebind<T, DFromV<V>> d;
  const RebindToUnsigned<decltype(d)> du;  // Iota0 is unsigned only
  using TU = TFromD<decltype(du)>;
  const auto is_i = detail::EqS(detail::Iota0(du), static_cast<TU>(i));
  return IfThenElse(RebindMask(d, is_i), Set(d, t), v);
}

// For 8-bit lanes, Iota0 might overflow.
template <class V, typename T, HWY_IF_T_SIZE_V(V, 1)>
HWY_API V InsertLane(const V v, size_t i, T t) {
  const Rebind<T, DFromV<V>> d;
  const auto zero = Zero(d);
  const auto one = Set(d, 1);
  const auto ge_i = Eq(detail::SlideUp(zero, one, i), one);
  const auto is_i = SetOnlyFirst(ge_i);
  return IfThenElse(RebindMask(d, is_i), Set(d, t), v);
}

// ------------------------------ OddEven

namespace detail {

// Faster version using a wide constant instead of Iota0 + AndS.
template <class D, HWY_IF_NOT_T_SIZE_D(D, 8)>
HWY_INLINE MFromD<D> IsEven(D d) {
  const RebindToUnsigned<decltype(d)> du;
  const RepartitionToWide<decltype(du)> duw;
  return RebindMask(d, detail::NeS(BitCast(du, Set(duw, 1)), 0u));
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_INLINE MFromD<D> IsEven(D d) {
  const RebindToUnsigned<decltype(d)> du;  // Iota0 is unsigned only
  return detail::EqS(detail::AndS(detail::Iota0(du), 1), 0);
}

// Also provide the negated form because there is no native CompressNot.
template <class D, HWY_IF_NOT_T_SIZE_D(D, 8)>
HWY_INLINE MFromD<D> IsOdd(D d) {
  const RebindToUnsigned<decltype(d)> du;
  const RepartitionToWide<decltype(du)> duw;
  return RebindMask(d, detail::EqS(BitCast(du, Set(duw, 1)), 0u));
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_INLINE MFromD<D> IsOdd(D d) {
  const RebindToUnsigned<decltype(d)> du;  // Iota0 is unsigned only
  return detail::NeS(detail::AndS(detail::Iota0(du), 1), 0);
}

}  // namespace detail

template <class V>
HWY_API V OddEven(const V a, const V b) {
  return IfThenElse(detail::IsEven(DFromV<V>()), b, a);
}

// ------------------------------ DupEven (OddEven)
template <class V>
HWY_API V DupEven(const V v) {
  const V up = detail::Slide1Up(v);
  return OddEven(up, v);
}

// ------------------------------ DupOdd (OddEven)
template <class V>
HWY_API V DupOdd(const V v) {
  const V down = detail::Slide1Down(v);
  return OddEven(v, down);
}

// ------------------------------ InterleaveEven (OddEven)
template <class D>
HWY_API VFromD<D> InterleaveEven(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return OddEven(detail::Slide1Up(b), a);
}

// ------------------------------ InterleaveOdd (OddEven)
template <class D>
HWY_API VFromD<D> InterleaveOdd(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return OddEven(b, detail::Slide1Down(a));
}

// ------------------------------ OddEvenBlocks
template <class V>
HWY_API V OddEvenBlocks(const V a, const V b) {
  const RebindToUnsigned<DFromV<V>> du;  // Iota0 is unsigned only
  constexpr size_t kShift = CeilLog2(16 / sizeof(TFromV<V>));
  const auto idx_block = ShiftRight<kShift>(detail::Iota0(du));
  const auto is_even = detail::EqS(detail::AndS(idx_block, 1), 0);
  return IfThenElse(is_even, b, a);
}

// ------------------------------ SwapAdjacentBlocks
template <class V>
HWY_API V SwapAdjacentBlocks(const V v) {
  const DFromV<V> d;
  const size_t lpb = detail::LanesPerBlock(d);
  const V down = detail::SlideDown(v, lpb);
  const V up = detail::SlideUp(v, v, lpb);
  return OddEvenBlocks(up, down);
}

// ------------------------------ TableLookupLanes

template <class D, class VI>
HWY_API VFromD<RebindToUnsigned<D>> IndicesFromVec(D d, VI vec) {
  static_assert(sizeof(TFromD<D>) == sizeof(TFromV<VI>), "Index != lane");
  const RebindToUnsigned<decltype(d)> du;  // instead of <D>: avoids unused d.
  const auto indices = BitCast(du, vec);
#if HWY_IS_DEBUG_BUILD
  using TU = TFromD<decltype(du)>;
  const size_t twice_num_of_lanes = Lanes(d) * 2;
  HWY_DASSERT(AllTrue(
      du, Eq(indices,
             detail::AndS(indices, static_cast<TU>(twice_num_of_lanes - 1)))));
#endif
  return indices;
}

template <class D, typename TI>
HWY_API VFromD<RebindToUnsigned<D>> SetTableIndices(D d, const TI* idx) {
  static_assert(sizeof(TFromD<D>) == sizeof(TI), "Index size must match lane");
  return IndicesFromVec(d, LoadU(Rebind<TI, D>(), idx));
}

#define HWY_RVV_TABLE(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                      MLEN, NAME, OP)                                         \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                          \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) v, HWY_RVV_V(uint, SEW, LMUL) idx) {    \
    return __riscv_v##OP##_vv_##CHAR##SEW##LMUL(v, idx,                       \
                                                HWY_RVV_AVL(SEW, SHIFT));     \
  }

// TableLookupLanes is supported for all types, but beware that indices are
// likely to wrap around for 8-bit lanes. When using TableLookupLanes inside
// this file, ensure that it is safe or use TableLookupLanes16 instead.
HWY_RVV_FOREACH(HWY_RVV_TABLE, TableLookupLanes, rgather, _ALL)
#undef HWY_RVV_TABLE

namespace detail {

#define HWY_RVV_TABLE16(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,     \
                        SHIFT, MLEN, NAME, OP)                               \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                         \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) v, HWY_RVV_V(uint, SEWD, LMULD) idx) { \
    return __riscv_v##OP##_vv_##CHAR##SEW##LMUL(v, idx,                      \
                                                HWY_RVV_AVL(SEW, SHIFT));    \
  }

HWY_RVV_FOREACH_UI08(HWY_RVV_TABLE16, TableLookupLanes16, rgatherei16, _EXT)
#undef HWY_RVV_TABLE16

// Used by Expand.
#define HWY_RVV_MASKED_TABLE(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,  \
                             SHIFT, MLEN, NAME, OP)                            \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                           \
      NAME(HWY_RVV_M(MLEN) mask, HWY_RVV_V(BASE, SEW, LMUL) maskedoff,         \
           HWY_RVV_V(BASE, SEW, LMUL) v, HWY_RVV_V(uint, SEW, LMUL) idx) {     \
    return __riscv_v##OP##_vv_##CHAR##SEW##LMUL##_mu(mask, maskedoff, v, idx,  \
                                                     HWY_RVV_AVL(SEW, SHIFT)); \
  }

HWY_RVV_FOREACH(HWY_RVV_MASKED_TABLE, MaskedTableLookupLanes, rgather, _ALL)
#undef HWY_RVV_MASKED_TABLE

#define HWY_RVV_MASKED_TABLE16(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD,       \
                               LMULH, SHIFT, MLEN, NAME, OP)                   \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                           \
      NAME(HWY_RVV_M(MLEN) mask, HWY_RVV_V(BASE, SEW, LMUL) maskedoff,         \
           HWY_RVV_V(BASE, SEW, LMUL) v, HWY_RVV_V(uint, SEWD, LMULD) idx) {   \
    return __riscv_v##OP##_vv_##CHAR##SEW##LMUL##_mu(mask, maskedoff, v, idx,  \
                                                     HWY_RVV_AVL(SEW, SHIFT)); \
  }

HWY_RVV_FOREACH_UI08(HWY_RVV_MASKED_TABLE16, MaskedTableLookupLanes16,
                     rgatherei16, _EXT)
#undef HWY_RVV_MASKED_TABLE16

}  // namespace detail

// ------------------------------ Reverse (TableLookupLanes)
template <class D, HWY_IF_T_SIZE_D(D, 1), HWY_IF_POW2_LE_D(D, 2)>
HWY_API VFromD<D> Reverse(D d, VFromD<D> v) {
  const Rebind<uint16_t, decltype(d)> du16;
  const size_t N = Lanes(d);
  const auto idx =
      detail::ReverseSubS(detail::Iota0(du16), static_cast<uint16_t>(N - 1));
  return detail::TableLookupLanes16(v, idx);
}

template <class D, HWY_IF_T_SIZE_D(D, 1), HWY_IF_POW2_GT_D(D, 2)>
HWY_API VFromD<D> Reverse(D d, VFromD<D> v) {
  const Half<decltype(d)> dh;
  const Rebind<uint16_t, decltype(dh)> du16;
  const size_t half_n = Lanes(dh);
  const auto idx = detail::ReverseSubS(detail::Iota0(du16),
                                       static_cast<uint16_t>(half_n - 1));
  const auto reversed_lo = detail::TableLookupLanes16(LowerHalf(dh, v), idx);
  const auto reversed_hi = detail::TableLookupLanes16(UpperHalf(dh, v), idx);
  return Combine(d, reversed_lo, reversed_hi);
}

template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 2) | (1 << 4) | (1 << 8))>
HWY_API VFromD<D> Reverse(D /* tag */, VFromD<D> v) {
  const RebindToUnsigned<D> du;
  using TU = TFromD<decltype(du)>;
  const size_t N = Lanes(du);
  const auto idx =
      detail::ReverseSubS(detail::Iota0(du), static_cast<TU>(N - 1));
  return TableLookupLanes(v, idx);
}

// ------------------------------ ResizeBitCast

// Extends or truncates a vector to match the given d.
namespace detail {

template <class D>
HWY_INLINE VFromD<D> ChangeLMUL(D /* d */, VFromD<D> v) {
  return v;
}

// Sanity check: when calling ChangeLMUL, the caller (ResizeBitCast) already
// BitCast to the same lane type. Note that V may use the native lane type for
// f16, so convert D to that before checking.
#define HWY_RVV_IF_SAME_T_DV(D, V) \
  hwy::EnableIf<IsSame<NativeLaneType<TFromD<D>>, TFromV<V>>()>* = nullptr

// LMUL of VFromD<D> < LMUL of V: need to truncate v
template <class D, class V,  // HWY_RVV_IF_SAME_T_DV(D, V),
          HWY_IF_POW2_LE_D(DFromV<VFromD<D>>, DFromV<V>().Pow2() - 1)>
HWY_INLINE VFromD<D> ChangeLMUL(D d, V v) {
  const DFromV<V> d_from;
  const Half<decltype(d_from)> dh_from;
  static_assert(
      DFromV<VFromD<decltype(dh_from)>>().Pow2() < DFromV<V>().Pow2(),
      "The LMUL of VFromD<decltype(dh_from)> must be less than the LMUL of V");
  static_assert(
      DFromV<VFromD<D>>().Pow2() <= DFromV<VFromD<decltype(dh_from)>>().Pow2(),
      "The LMUL of VFromD<D> must be less than or equal to the LMUL of "
      "VFromD<decltype(dh_from)>");
  return ChangeLMUL(d, Trunc(v));
}

// LMUL of VFromD<D> > LMUL of V: need to extend v
template <class D, class V,  // HWY_RVV_IF_SAME_T_DV(D, V),
          HWY_IF_POW2_GT_D(DFromV<VFromD<D>>, DFromV<V>().Pow2())>
HWY_INLINE VFromD<D> ChangeLMUL(D d, V v) {
  const DFromV<V> d_from;
  const Twice<decltype(d_from)> dt_from;
  static_assert(DFromV<VFromD<decltype(dt_from)>>().Pow2() > DFromV<V>().Pow2(),
                "The LMUL of VFromD<decltype(dt_from)> must be greater than "
                "the LMUL of V");
  static_assert(
      DFromV<VFromD<D>>().Pow2() >= DFromV<VFromD<decltype(dt_from)>>().Pow2(),
      "The LMUL of VFromD<D> must be greater than or equal to the LMUL of "
      "VFromD<decltype(dt_from)>");
  return ChangeLMUL(d, Ext(dt_from, v));
}

#undef HWY_RVV_IF_SAME_T_DV

}  // namespace detail

template <class DTo, class VFrom>
HWY_API VFromD<DTo> ResizeBitCast(DTo /*dto*/, VFrom v) {
  const DFromV<decltype(v)> d_from;
  const Repartition<uint8_t, decltype(d_from)> du8_from;
  const DFromV<VFromD<DTo>> d_to;
  const Repartition<uint8_t, decltype(d_to)> du8_to;
  return BitCast(d_to, detail::ChangeLMUL(du8_to, BitCast(du8_from, v)));
}

// ------------------------------ Reverse2 (RotateRight, OddEven)

// Per-target flags to prevent generic_ops-inl.h defining 8-bit Reverse2/4/8.
#ifdef HWY_NATIVE_REVERSE2_8
#undef HWY_NATIVE_REVERSE2_8
#else
#define HWY_NATIVE_REVERSE2_8
#endif

// Shifting and adding requires fewer instructions than blending, but casting to
// u32 only works for LMUL in [1/2, 8].

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Reverse2(D d, const VFromD<D> v) {
  const detail::AdjustSimdTagToMinVecPow2<Repartition<uint16_t, D>> du16;
  return ResizeBitCast(d, RotateRight<8>(ResizeBitCast(du16, v)));
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Reverse2(D d, const VFromD<D> v) {
  const detail::AdjustSimdTagToMinVecPow2<Repartition<uint32_t, D>> du32;
  return ResizeBitCast(d, RotateRight<16>(ResizeBitCast(du32, v)));
}

// Shifting and adding requires fewer instructions than blending, but casting to
// u64 does not work for LMUL < 1.
template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> Reverse2(D d, const VFromD<D> v) {
  const detail::AdjustSimdTagToMinVecPow2<Repartition<uint64_t, D>> du64;
  return ResizeBitCast(d, RotateRight<32>(ResizeBitCast(du64, v)));
}

template <class D, class V = VFromD<D>, HWY_IF_T_SIZE_D(D, 8)>
HWY_API V Reverse2(D /* tag */, const V v) {
  const V up = detail::Slide1Up(v);
  const V down = detail::Slide1Down(v);
  return OddEven(up, down);
}

// ------------------------------ Reverse4 (TableLookupLanes)

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Reverse4(D d, const VFromD<D> v) {
  const detail::AdjustSimdTagToMinVecPow2<Repartition<uint16_t, D>> du16;
  return ResizeBitCast(d, Reverse2(du16, ResizeBitCast(du16, Reverse2(d, v))));
}

template <class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Reverse4(D d, const VFromD<D> v) {
  const RebindToUnsigned<D> du;
  const auto idx = detail::XorS(detail::Iota0(du), 3);
  return BitCast(d, TableLookupLanes(BitCast(du, v), idx));
}

// ------------------------------ Reverse8 (TableLookupLanes)

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Reverse8(D d, const VFromD<D> v) {
  const detail::AdjustSimdTagToMinVecPow2<Repartition<uint32_t, D>> du32;
  return ResizeBitCast(d, Reverse2(du32, ResizeBitCast(du32, Reverse4(d, v))));
}

template <class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Reverse8(D d, const VFromD<D> v) {
  const RebindToUnsigned<D> du;
  const auto idx = detail::XorS(detail::Iota0(du), 7);
  return BitCast(d, TableLookupLanes(BitCast(du, v), idx));
}

// ------------------------------ ReverseBlocks (Reverse, Shuffle01)
template <class D, class V = VFromD<D>>
HWY_API V ReverseBlocks(D d, V v) {
  const detail::AdjustSimdTagToMinVecPow2<Repartition<uint64_t, D>> du64;
  const size_t N = Lanes(du64);
  const auto rev =
      detail::ReverseSubS(detail::Iota0(du64), static_cast<uint64_t>(N - 1));
  // Swap lo/hi u64 within each block
  const auto idx = detail::XorS(rev, 1);
  return ResizeBitCast(d, TableLookupLanes(ResizeBitCast(du64, v), idx));
}

// ------------------------------ Compress

// RVV supports all lane types natively.
#ifdef HWY_NATIVE_COMPRESS8
#undef HWY_NATIVE_COMPRESS8
#else
#define HWY_NATIVE_COMPRESS8
#endif

template <typename T>
struct CompressIsPartition {
  enum { value = 0 };
};

#define HWY_RVV_COMPRESS(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                         SHIFT, MLEN, NAME, OP)                           \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                      \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) v, HWY_RVV_M(MLEN) mask) {          \
    return __riscv_v##OP##_vm_##CHAR##SEW##LMUL(v, mask,                  \
                                                HWY_RVV_AVL(SEW, SHIFT)); \
  }

HWY_RVV_FOREACH(HWY_RVV_COMPRESS, Compress, compress, _ALL)
#undef HWY_RVV_COMPRESS

// ------------------------------ Expand

#ifdef HWY_NATIVE_EXPAND
#undef HWY_NATIVE_EXPAND
#else
#define HWY_NATIVE_EXPAND
#endif

// >= 2-byte lanes: idx lanes will not overflow.
template <class V, class M, HWY_IF_NOT_T_SIZE_V(V, 1)>
HWY_API V Expand(V v, const M mask) {
  const DFromV<V> d;
  const RebindToUnsigned<decltype(d)> du;
  const auto idx = detail::MaskedIota(du, RebindMask(du, mask));
  const V zero = Zero(d);
  return detail::MaskedTableLookupLanes(mask, zero, v, idx);
}

// 1-byte lanes, LMUL < 8: promote idx to u16.
template <class V, class M, HWY_IF_T_SIZE_V(V, 1), class D = DFromV<V>,
          HWY_IF_POW2_LE_D(D, 2)>
HWY_API V Expand(V v, const M mask) {
  const D d;
  const Rebind<uint16_t, decltype(d)> du16;
  const auto idx = detail::MaskedIota(du16, RebindMask(du16, mask));
  const V zero = Zero(d);
  return detail::MaskedTableLookupLanes16(mask, zero, v, idx);
}

// 1-byte lanes, max LMUL: unroll 2x.
template <class V, class M, HWY_IF_T_SIZE_V(V, 1), class D = DFromV<V>,
          HWY_IF_POW2_GT_D(DFromV<V>, 2)>
HWY_API V Expand(V v, const M mask) {
  const D d;
  const Half<D> dh;
  const auto v0 = LowerHalf(dh, v);
  // TODO(janwas): skip vec<->mask if we can cast masks.
  const V vmask = VecFromMask(d, mask);
  const auto m0 = MaskFromVec(LowerHalf(dh, vmask));

  // Cannot just use UpperHalf, must shift by the number of inputs consumed.
  const size_t count = CountTrue(dh, m0);
  const auto v1 = detail::Trunc(detail::SlideDown(v, count));
  const auto m1 = MaskFromVec(UpperHalf(dh, vmask));
  return Combine(d, Expand(v1, m1), Expand(v0, m0));
}

// ------------------------------ LoadExpand
template <class D>
HWY_API VFromD<D> LoadExpand(MFromD<D> mask, D d,
                             const TFromD<D>* HWY_RESTRICT unaligned) {
  return Expand(LoadU(d, unaligned), mask);
}

// ------------------------------ CompressNot
template <class V, class M>
HWY_API V CompressNot(V v, const M mask) {
  return Compress(v, Not(mask));
}

// ------------------------------ CompressBlocksNot
template <class V, class M>
HWY_API V CompressBlocksNot(V v, const M mask) {
  return CompressNot(v, mask);
}

// ------------------------------ CompressStore
template <class V, class M, class D>
HWY_API size_t CompressStore(const V v, const M mask, const D d,
                             TFromD<D>* HWY_RESTRICT unaligned) {
  StoreU(Compress(v, mask), d, unaligned);
  return CountTrue(d, mask);
}

// ------------------------------ CompressBlendedStore
template <class V, class M, class D>
HWY_API size_t CompressBlendedStore(const V v, const M mask, const D d,
                                    TFromD<D>* HWY_RESTRICT unaligned) {
  const size_t count = CountTrue(d, mask);
  StoreN(Compress(v, mask), d, unaligned, count);
  return count;
}

// ================================================== COMPARE (2)

// ------------------------------ FindLastTrue

template <class D>
HWY_API intptr_t FindLastTrue(D d, MFromD<D> m) {
  const RebindToSigned<decltype(d)> di;
  const intptr_t fft_rev_idx =
      FindFirstTrue(d, MaskFromVec(Reverse(di, VecFromMask(di, m))));
  return (fft_rev_idx >= 0)
             ? (static_cast<intptr_t>(Lanes(d) - 1) - fft_rev_idx)
             : intptr_t{-1};
}

template <class D>
HWY_API size_t FindKnownLastTrue(D d, MFromD<D> m) {
  const RebindToSigned<decltype(d)> di;
  const size_t fft_rev_idx =
      FindKnownFirstTrue(d, MaskFromVec(Reverse(di, VecFromMask(di, m))));
  return Lanes(d) - 1 - fft_rev_idx;
}

// ------------------------------ ConcatOdd (Compress)

namespace detail {

#define HWY_RVV_NARROW(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                       MLEN, NAME, OP)                                         \
  template <size_t kShift>                                                     \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL) NAME(HWY_RVV_V(BASE, SEWD, LMULD) v) {    \
    return __riscv_v##OP##_wx_##CHAR##SEW##LMUL(v, kShift,                     \
                                                HWY_RVV_AVL(SEWD, SHIFT + 1)); \
  }

HWY_RVV_FOREACH_U08(HWY_RVV_NARROW, Narrow, nsrl, _EXT)
HWY_RVV_FOREACH_U16(HWY_RVV_NARROW, Narrow, nsrl, _EXT)
HWY_RVV_FOREACH_U32(HWY_RVV_NARROW, Narrow, nsrl, _EXT)
#undef HWY_RVV_NARROW

}  // namespace detail

// Casting to wider and narrowing is the fastest for < 64-bit lanes.
template <class D, HWY_IF_NOT_T_SIZE_D(D, 8), HWY_IF_POW2_LE_D(D, 2)>
HWY_API VFromD<D> ConcatOdd(D d, VFromD<D> hi, VFromD<D> lo) {
  constexpr size_t kBits = sizeof(TFromD<D>) * 8;
  const Twice<decltype(d)> dt;
  const RepartitionToWide<RebindToUnsigned<decltype(dt)>> dtuw;
  const VFromD<decltype(dtuw)> hl = BitCast(dtuw, Combine(dt, hi, lo));
  return BitCast(d, detail::Narrow<kBits>(hl));
}

// 64-bit: Combine+Compress.
template <class D, HWY_IF_T_SIZE_D(D, 8), HWY_IF_POW2_LE_D(D, 2)>
HWY_API VFromD<D> ConcatOdd(D d, VFromD<D> hi, VFromD<D> lo) {
  const Twice<decltype(d)> dt;
  const VFromD<decltype(dt)> hl = Combine(dt, hi, lo);
  return LowerHalf(d, Compress(hl, detail::IsOdd(dt)));
}

// Any type, max LMUL: Compress both, then Combine.
template <class D, HWY_IF_POW2_GT_D(D, 2)>
HWY_API VFromD<D> ConcatOdd(D d, VFromD<D> hi, VFromD<D> lo) {
  const Half<decltype(d)> dh;
  const MFromD<D> is_odd = detail::IsOdd(d);
  const VFromD<decltype(d)> hi_odd = Compress(hi, is_odd);
  const VFromD<decltype(d)> lo_odd = Compress(lo, is_odd);
  return Combine(d, LowerHalf(dh, hi_odd), LowerHalf(dh, lo_odd));
}

// ------------------------------ ConcatEven (Compress)

// Casting to wider and narrowing is the fastest for < 64-bit lanes.
template <class D, HWY_IF_NOT_T_SIZE_D(D, 8), HWY_IF_POW2_LE_D(D, 2)>
HWY_API VFromD<D> ConcatEven(D d, VFromD<D> hi, VFromD<D> lo) {
  const Twice<decltype(d)> dt;
  const RepartitionToWide<RebindToUnsigned<decltype(dt)>> dtuw;
  const VFromD<decltype(dtuw)> hl = BitCast(dtuw, Combine(dt, hi, lo));
  return BitCast(d, detail::Narrow<0>(hl));
}

// 64-bit: Combine+Compress.
template <class D, HWY_IF_T_SIZE_D(D, 8), HWY_IF_POW2_LE_D(D, 2)>
HWY_API VFromD<D> ConcatEven(D d, VFromD<D> hi, VFromD<D> lo) {
  const Twice<decltype(d)> dt;
  const VFromD<decltype(dt)> hl = Combine(dt, hi, lo);
  return LowerHalf(d, Compress(hl, detail::IsEven(dt)));
}

// Any type, max LMUL: Compress both, then Combine.
template <class D, HWY_IF_POW2_GT_D(D, 2)>
HWY_API VFromD<D> ConcatEven(D d, VFromD<D> hi, VFromD<D> lo) {
  const Half<decltype(d)> dh;
  const MFromD<D> is_even = detail::IsEven(d);
  const VFromD<decltype(d)> hi_even = Compress(hi, is_even);
  const VFromD<decltype(d)> lo_even = Compress(lo, is_even);
  return Combine(d, LowerHalf(dh, hi_even), LowerHalf(dh, lo_even));
}

// ------------------------------ PromoteEvenTo/PromoteOddTo
#include "hwy/ops/inside-inl.h"

// ================================================== BLOCKWISE

// ------------------------------ CombineShiftRightBytes
template <size_t kBytes, class D, class V = VFromD<D>>
HWY_API V CombineShiftRightBytes(const D d, const V hi, V lo) {
  const Repartition<uint8_t, decltype(d)> d8;
  const auto hi8 = BitCast(d8, hi);
  const auto lo8 = BitCast(d8, lo);
  const auto hi_up = detail::SlideUp(hi8, hi8, 16 - kBytes);
  const auto lo_down = detail::SlideDown(lo8, kBytes);
  const auto is_lo = detail::FirstNPerBlock<16 - kBytes>(d8);
  return BitCast(d, IfThenElse(is_lo, lo_down, hi_up));
}

// ------------------------------ CombineShiftRightLanes
template <size_t kLanes, class D, class V = VFromD<D>>
HWY_API V CombineShiftRightLanes(const D d, const V hi, V lo) {
  constexpr size_t kLanesUp = 16 / sizeof(TFromV<V>) - kLanes;
  const auto hi_up = detail::SlideUp(hi, hi, kLanesUp);
  const auto lo_down = detail::SlideDown(lo, kLanes);
  const auto is_lo = detail::FirstNPerBlock<kLanesUp>(d);
  return IfThenElse(is_lo, lo_down, hi_up);
}

// ------------------------------ Shuffle2301 (ShiftLeft)
template <class V>
HWY_API V Shuffle2301(const V v) {
  const DFromV<V> d;
  static_assert(sizeof(TFromD<decltype(d)>) == 4, "Defined for 32-bit types");
  const Repartition<uint64_t, decltype(d)> du64;
  const auto v64 = BitCast(du64, v);
  return BitCast(d, Or(ShiftRight<32>(v64), ShiftLeft<32>(v64)));
}

// ------------------------------ Shuffle2103
template <class V>
HWY_API V Shuffle2103(const V v) {
  const DFromV<V> d;
  static_assert(sizeof(TFromD<decltype(d)>) == 4, "Defined for 32-bit types");
  return CombineShiftRightLanes<3>(d, v, v);
}

// ------------------------------ Shuffle0321
template <class V>
HWY_API V Shuffle0321(const V v) {
  const DFromV<V> d;
  static_assert(sizeof(TFromD<decltype(d)>) == 4, "Defined for 32-bit types");
  return CombineShiftRightLanes<1>(d, v, v);
}

// ------------------------------ Shuffle1032
template <class V>
HWY_API V Shuffle1032(const V v) {
  const DFromV<V> d;
  static_assert(sizeof(TFromD<decltype(d)>) == 4, "Defined for 32-bit types");
  return CombineShiftRightLanes<2>(d, v, v);
}

// ------------------------------ Shuffle01
template <class V>
HWY_API V Shuffle01(const V v) {
  const DFromV<V> d;
  static_assert(sizeof(TFromD<decltype(d)>) == 8, "Defined for 64-bit types");
  return CombineShiftRightLanes<1>(d, v, v);
}

// ------------------------------ Shuffle0123
template <class V>
HWY_API V Shuffle0123(const V v) {
  return Shuffle2301(Shuffle1032(v));
}

// ------------------------------ TableLookupBytes

template <class VT, class VI>
HWY_API VI TableLookupBytes(const VT vt, const VI vi) {
  const DFromV<VT> dt;  // T=table, I=index.
  const DFromV<VI> di;
  const Repartition<uint8_t, decltype(dt)> dt8;
  const Repartition<uint8_t, decltype(di)> di8;
  // Required for producing half-vectors with table lookups from a full vector.
  // If we instead run at the LMUL of the index vector, lookups into the table
  // would be truncated. Thus we run at the larger of the two LMULs and truncate
  // the result vector to the original index LMUL.
  constexpr int kPow2T = dt8.Pow2();
  constexpr int kPow2I = di8.Pow2();
  const Simd<uint8_t, MaxLanes(di8), HWY_MAX(kPow2T, kPow2I)> dm8;  // m=max
  const auto vmt = detail::ChangeLMUL(dm8, BitCast(dt8, vt));
  const auto vmi = detail::ChangeLMUL(dm8, BitCast(di8, vi));
  auto offsets = detail::OffsetsOf128BitBlocks(dm8, detail::Iota0(dm8));
  // If the table is shorter, wrap around offsets so they do not reference
  // undefined lanes in the newly extended vmt.
  if (kPow2T < kPow2I) {
    offsets = detail::AndS(offsets, static_cast<uint8_t>(Lanes(dt8) - 1));
  }
  const auto out = TableLookupLanes(vmt, Add(vmi, offsets));
  return BitCast(di, detail::ChangeLMUL(di8, out));
}

template <class VT, class VI>
HWY_API VI TableLookupBytesOr0(const VT vt, const VI idx) {
  const DFromV<VI> di;
  const Repartition<int8_t, decltype(di)> di8;
  const auto idx8 = BitCast(di8, idx);
  const auto lookup = TableLookupBytes(vt, idx8);
  return BitCast(di, IfThenZeroElse(detail::LtS(idx8, 0), lookup));
}

// ------------------------------ TwoTablesLookupLanes

// WARNING: 8-bit lanes may lead to unexpected results because idx is the same
// size and may overflow.
template <class D, HWY_IF_POW2_LE_D(D, 2)>
HWY_API VFromD<D> TwoTablesLookupLanes(D d, VFromD<D> a, VFromD<D> b,
                                       VFromD<RebindToUnsigned<D>> idx) {
  const Twice<decltype(d)> dt;
  const RebindToUnsigned<decltype(dt)> dt_u;
  const auto combined_tbl = Combine(dt, b, a);
  const auto combined_idx = Combine(dt_u, idx, idx);
  return LowerHalf(d, TableLookupLanes(combined_tbl, combined_idx));
}

template <class D, HWY_IF_POW2_GT_D(D, 2)>
HWY_API VFromD<D> TwoTablesLookupLanes(D d, VFromD<D> a, VFromD<D> b,
                                       VFromD<RebindToUnsigned<D>> idx) {
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;

  const size_t num_of_lanes = Lanes(d);
  const auto idx_mod = detail::AndS(idx, static_cast<TU>(num_of_lanes - 1));
  const auto sel_a_mask = Ne(idx, idx_mod);  // FALSE if a

  const auto a_lookup_result = TableLookupLanes(a, idx_mod);
  return detail::MaskedTableLookupLanes(sel_a_mask, a_lookup_result, b,
                                        idx_mod);
}

template <class V>
HWY_API V TwoTablesLookupLanes(V a, V b,
                               VFromD<RebindToUnsigned<DFromV<V>>> idx) {
  const DFromV<decltype(a)> d;
  return TwoTablesLookupLanes(d, a, b, idx);
}

// ------------------------------ Broadcast

// 8-bit requires 16-bit tables.
template <int kLane, class V, class D = DFromV<V>, HWY_IF_T_SIZE_D(D, 1),
          HWY_IF_POW2_LE_D(D, 2)>
HWY_API V Broadcast(const V v) {
  const D d;
  HWY_DASSERT(0 <= kLane && kLane < detail::LanesPerBlock(d));

  const Rebind<uint16_t, decltype(d)> du16;
  VFromD<decltype(du16)> idx =
      detail::OffsetsOf128BitBlocks(d, detail::Iota0(du16));
  if (kLane != 0) {
    idx = detail::AddS(idx, kLane);
  }
  return detail::TableLookupLanes16(v, idx);
}

// 8-bit and max LMUL: split into halves.
template <int kLane, class V, class D = DFromV<V>, HWY_IF_T_SIZE_D(D, 1),
          HWY_IF_POW2_GT_D(D, 2)>
HWY_API V Broadcast(const V v) {
  const D d;
  HWY_DASSERT(0 <= kLane && kLane < detail::LanesPerBlock(d));

  const Half<decltype(d)> dh;
  using VH = VFromD<decltype(dh)>;
  const Rebind<uint16_t, decltype(dh)> du16;
  VFromD<decltype(du16)> idx =
      detail::OffsetsOf128BitBlocks(d, detail::Iota0(du16));
  if (kLane != 0) {
    idx = detail::AddS(idx, kLane);
  }
  const VH lo = detail::TableLookupLanes16(LowerHalf(dh, v), idx);
  const VH hi = detail::TableLookupLanes16(UpperHalf(dh, v), idx);
  return Combine(d, hi, lo);
}

template <int kLane, class V, class D = DFromV<V>,
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 2) | (1 << 4) | (1 << 8))>
HWY_API V Broadcast(const V v) {
  const D d;
  HWY_DASSERT(0 <= kLane && kLane < detail::LanesPerBlock(d));

  const RebindToUnsigned<decltype(d)> du;
  auto idx = detail::OffsetsOf128BitBlocks(d, detail::Iota0(du));
  if (kLane != 0) {
    idx = detail::AddS(idx, kLane);
  }
  return TableLookupLanes(v, idx);
}

// ------------------------------ BroadcastLane
#ifdef HWY_NATIVE_BROADCASTLANE
#undef HWY_NATIVE_BROADCASTLANE
#else
#define HWY_NATIVE_BROADCASTLANE
#endif

namespace detail {

#define HWY_RVV_BROADCAST_LANE(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD,  \
                               LMULH, SHIFT, MLEN, NAME, OP)              \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                      \
      NAME(HWY_RVV_V(BASE, SEW, LMUL) v, size_t idx) {                    \
    return __riscv_v##OP##_vx_##CHAR##SEW##LMUL(v, idx,                   \
                                                HWY_RVV_AVL(SEW, SHIFT)); \
  }

HWY_RVV_FOREACH(HWY_RVV_BROADCAST_LANE, BroadcastLane, rgather, _ALL)
#undef HWY_RVV_BROADCAST_LANE

}  // namespace detail

template <int kLane, class V>
HWY_API V BroadcastLane(V v) {
  static_assert(0 <= kLane && kLane < HWY_MAX_LANES_V(V), "Invalid lane");
  return detail::BroadcastLane(v, static_cast<size_t>(kLane));
}

// ------------------------------ InsertBlock
#ifdef HWY_NATIVE_BLK_INSERT_EXTRACT
#undef HWY_NATIVE_BLK_INSERT_EXTRACT
#else
#define HWY_NATIVE_BLK_INSERT_EXTRACT
#endif

template <int kBlockIdx, class V>
HWY_API V InsertBlock(V v, VFromD<BlockDFromD<DFromV<V>>> blk_to_insert) {
  const DFromV<decltype(v)> d;
  using TU = If<(sizeof(TFromV<V>) == 1 && DFromV<V>().Pow2() >= -2), uint16_t,
                MakeUnsigned<TFromV<V>>>;
  using TIdx = If<sizeof(TU) == 1, uint16_t, TU>;

  const Repartition<TU, decltype(d)> du;
  const Rebind<TIdx, decltype(du)> d_idx;
  static_assert(0 <= kBlockIdx && kBlockIdx < d.MaxBlocks(),
                "Invalid block index");
  constexpr size_t kMaxLanesPerBlock = 16 / sizeof(TU);

  constexpr size_t kBlkByteOffset =
      static_cast<size_t>(kBlockIdx) * kMaxLanesPerBlock;
  const auto vu = BitCast(du, v);
  const auto vblk = ResizeBitCast(du, blk_to_insert);
  const auto vblk_shifted = detail::SlideUp(vblk, vblk, kBlkByteOffset);
  const auto insert_mask = RebindMask(
      du, detail::LtS(detail::SubS(detail::Iota0(d_idx),
                                   static_cast<TIdx>(kBlkByteOffset)),
                      static_cast<TIdx>(kMaxLanesPerBlock)));

  return BitCast(d, IfThenElse(insert_mask, vblk_shifted, vu));
}

// ------------------------------ BroadcastBlock
template <int kBlockIdx, class V, HWY_IF_POW2_LE_D(DFromV<V>, -3)>
HWY_API V BroadcastBlock(V v) {
  const DFromV<decltype(v)> d;
  const Repartition<uint8_t, decltype(d)> du8;
  const Rebind<uint16_t, decltype(d)> du16;

  static_assert(0 <= kBlockIdx && kBlockIdx < d.MaxBlocks(),
                "Invalid block index");

  const auto idx = detail::AddS(detail::AndS(detail::Iota0(du16), uint16_t{15}),
                                static_cast<uint16_t>(kBlockIdx * 16));
  return BitCast(d, detail::TableLookupLanes16(BitCast(du8, v), idx));
}

template <int kBlockIdx, class V, HWY_IF_POW2_GT_D(DFromV<V>, -3)>
HWY_API V BroadcastBlock(V v) {
  const DFromV<decltype(v)> d;
  using TU = If<sizeof(TFromV<V>) == 1, uint16_t, MakeUnsigned<TFromV<V>>>;
  const Repartition<TU, decltype(d)> du;

  static_assert(0 <= kBlockIdx && kBlockIdx < d.MaxBlocks(),
                "Invalid block index");
  constexpr size_t kMaxLanesPerBlock = 16 / sizeof(TU);

  const auto idx = detail::AddS(
      detail::AndS(detail::Iota0(du), static_cast<TU>(kMaxLanesPerBlock - 1)),
      static_cast<TU>(static_cast<size_t>(kBlockIdx) * kMaxLanesPerBlock));
  return BitCast(d, TableLookupLanes(BitCast(du, v), idx));
}

// ------------------------------ ExtractBlock
template <int kBlockIdx, class V>
HWY_API VFromD<BlockDFromD<DFromV<V>>> ExtractBlock(V v) {
  const DFromV<decltype(v)> d;
  const BlockDFromD<decltype(d)> d_block;

  static_assert(0 <= kBlockIdx && kBlockIdx < d.MaxBlocks(),
                "Invalid block index");
  constexpr size_t kMaxLanesPerBlock = 16 / sizeof(TFromD<decltype(d)>);
  constexpr size_t kBlkByteOffset =
      static_cast<size_t>(kBlockIdx) * kMaxLanesPerBlock;

  return ResizeBitCast(d_block, detail::SlideDown(v, kBlkByteOffset));
}

// ------------------------------ ShiftLeftLanes

template <size_t kLanes, class D, class V = VFromD<D>>
HWY_API V ShiftLeftLanes(const D d, const V v) {
  const RebindToSigned<decltype(d)> di;
  const RebindToUnsigned<decltype(d)> du;
  using TI = TFromD<decltype(di)>;
  const auto shifted = detail::SlideUp(v, v, kLanes);
  // Match x86 semantics by zeroing lower lanes in 128-bit blocks
  const auto idx_mod =
      detail::AndS(BitCast(di, detail::Iota0(du)),
                   static_cast<TI>(detail::LanesPerBlock(di) - 1));
  const auto clear = detail::LtS(idx_mod, static_cast<TI>(kLanes));
  return IfThenZeroElse(clear, shifted);
}

template <size_t kLanes, class V>
HWY_API V ShiftLeftLanes(const V v) {
  return ShiftLeftLanes<kLanes>(DFromV<V>(), v);
}

// ------------------------------ ShiftLeftBytes

template <int kBytes, class D>
HWY_API VFromD<D> ShiftLeftBytes(D d, const VFromD<D> v) {
  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, ShiftLeftLanes<kBytes>(BitCast(d8, v)));
}

template <int kBytes, class V>
HWY_API V ShiftLeftBytes(const V v) {
  return ShiftLeftBytes<kBytes>(DFromV<V>(), v);
}

// ------------------------------ ShiftRightLanes
template <size_t kLanes, typename T, size_t N, int kPow2,
          class V = VFromD<Simd<T, N, kPow2>>>
HWY_API V ShiftRightLanes(const Simd<T, N, kPow2> d, V v) {
  const RebindToSigned<decltype(d)> di;
  const RebindToUnsigned<decltype(d)> du;
  using TI = TFromD<decltype(di)>;
  // For partial vectors, clear upper lanes so we shift in zeros.
  if (N <= 16 / sizeof(T)) {
    v = detail::SlideUp(v, Zero(d), N);
  }

  const auto shifted = detail::SlideDown(v, kLanes);
  // Match x86 semantics by zeroing upper lanes in 128-bit blocks
  const size_t lpb = detail::LanesPerBlock(di);
  const auto idx_mod =
      detail::AndS(BitCast(di, detail::Iota0(du)), static_cast<TI>(lpb - 1));
  const auto keep = detail::LtS(idx_mod, static_cast<TI>(lpb - kLanes));
  return IfThenElseZero(keep, shifted);
}

// ------------------------------ ShiftRightBytes
template <int kBytes, class D, class V = VFromD<D>>
HWY_API V ShiftRightBytes(const D d, const V v) {
  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, ShiftRightLanes<kBytes>(d8, BitCast(d8, v)));
}

// ------------------------------ InterleaveWholeLower
#ifdef HWY_NATIVE_INTERLEAVE_WHOLE
#undef HWY_NATIVE_INTERLEAVE_WHOLE
#else
#define HWY_NATIVE_INTERLEAVE_WHOLE
#endif

namespace detail {
// Returns double-length vector with interleaved lanes.
template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_POW2_GT_D(D, -3)>
HWY_API VFromD<D> InterleaveWhole(D d, VFromD<Half<D>> a, VFromD<Half<D>> b) {
  const RebindToUnsigned<decltype(d)> du;
  using TW = MakeWide<TFromD<decltype(du)>>;
  const Rebind<TW, Half<decltype(du)>> dw;
  const Half<decltype(du)> duh;  // cast inputs to unsigned so we zero-extend

  const VFromD<decltype(dw)> aw = PromoteTo(dw, BitCast(duh, a));
  const VFromD<decltype(dw)> bw = PromoteTo(dw, BitCast(duh, b));
  return BitCast(d, Or(aw, BitCast(dw, detail::Slide1Up(BitCast(du, bw)))));
}
// 64-bit: cannot PromoteTo, but can Ext.
template <class D, HWY_IF_T_SIZE_D(D, 8), HWY_IF_POW2_LE_D(D, 2)>
HWY_API VFromD<D> InterleaveWhole(D d, VFromD<Half<D>> a, VFromD<Half<D>> b) {
  const RebindToUnsigned<decltype(d)> du;
  const auto idx = ShiftRight<1>(detail::Iota0(du));
  return OddEven(TableLookupLanes(detail::Ext(d, b), idx),
                 TableLookupLanes(detail::Ext(d, a), idx));
}
template <class D, HWY_IF_T_SIZE_D(D, 8), HWY_IF_POW2_GT_D(D, 2)>
HWY_API VFromD<D> InterleaveWhole(D d, VFromD<Half<D>> a, VFromD<Half<D>> b) {
  const Half<D> dh;
  const Half<decltype(dh)> dq;
  const VFromD<decltype(dh)> i0 =
      InterleaveWhole(dh, LowerHalf(dq, a), LowerHalf(dq, b));
  const VFromD<decltype(dh)> i1 =
      InterleaveWhole(dh, UpperHalf(dq, a), UpperHalf(dq, b));
  return Combine(d, i1, i0);
}

}  // namespace detail

template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2) | (1 << 4))>
HWY_API VFromD<D> InterleaveWholeLower(D d, VFromD<D> a, VFromD<D> b) {
  const RebindToUnsigned<decltype(d)> du;
  const detail::AdjustSimdTagToMinVecPow2<RepartitionToWide<decltype(du)>> dw;
  const RepartitionToNarrow<decltype(dw)> du_src;

  const VFromD<D> aw =
      ResizeBitCast(d, PromoteLowerTo(dw, ResizeBitCast(du_src, a)));
  const VFromD<D> bw =
      ResizeBitCast(d, PromoteLowerTo(dw, ResizeBitCast(du_src, b)));
  return Or(aw, detail::Slide1Up(bw));
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> InterleaveWholeLower(D d, VFromD<D> a, VFromD<D> b) {
  const RebindToUnsigned<decltype(d)> du;
  const auto idx = ShiftRight<1>(detail::Iota0(du));
  return OddEven(TableLookupLanes(b, idx), TableLookupLanes(a, idx));
}

// ------------------------------ InterleaveWholeUpper

template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2) | (1 << 4))>
HWY_API VFromD<D> InterleaveWholeUpper(D d, VFromD<D> a, VFromD<D> b) {
  // Use Lanes(d) / 2 instead of Lanes(Half<D>()) as Lanes(Half<D>()) can only
  // be called if (d.Pow2() >= -2 && d.Pow2() == DFromV<VFromD<D>>().Pow2()) is
  // true and and as the results of InterleaveWholeUpper are
  // implementation-defined if Lanes(d) is less than 2.
  const size_t half_N = Lanes(d) / 2;
  return InterleaveWholeLower(d, detail::SlideDown(a, half_N),
                              detail::SlideDown(b, half_N));
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> InterleaveWholeUpper(D d, VFromD<D> a, VFromD<D> b) {
  // Use Lanes(d) / 2 instead of Lanes(Half<D>()) as Lanes(Half<D>()) can only
  // be called if (d.Pow2() >= -2 && d.Pow2() == DFromV<VFromD<D>>().Pow2()) is
  // true and as the results of InterleaveWholeUpper are implementation-defined
  // if Lanes(d) is less than 2.
  const size_t half_N = Lanes(d) / 2;
  const RebindToUnsigned<decltype(d)> du;
  const auto idx = detail::AddS(ShiftRight<1>(detail::Iota0(du)),
                                static_cast<uint64_t>(half_N));
  return OddEven(TableLookupLanes(b, idx), TableLookupLanes(a, idx));
}

// ------------------------------ InterleaveLower (InterleaveWholeLower)

namespace detail {

// Definitely at least 128 bit: match x86 semantics (independent blocks). Using
// InterleaveWhole and 64-bit Compress avoids 8-bit overflow.
template <class D, class V, HWY_IF_POW2_LE_D(D, 2)>
HWY_INLINE V InterleaveLowerBlocks(D d, const V a, const V b) {
  static_assert(IsSame<TFromD<D>, TFromV<V>>(), "D/V mismatch");
  const Twice<D> dt;
  const RebindToUnsigned<decltype(dt)> dt_u;
  const VFromD<decltype(dt)> interleaved = detail::InterleaveWhole(dt, a, b);
  // Keep only even 128-bit blocks. This is faster than u64 ConcatEven
  // because we only have a single vector.
  constexpr size_t kShift = CeilLog2(16 / sizeof(TFromD<D>));
  const VFromD<decltype(dt_u)> idx_block =
      ShiftRight<kShift>(detail::Iota0(dt_u));
  const MFromD<decltype(dt_u)> is_even =
      detail::EqS(detail::AndS(idx_block, 1), 0);
  return BitCast(d, LowerHalf(Compress(BitCast(dt_u, interleaved), is_even)));
}
template <class D, class V, HWY_IF_POW2_GT_D(D, 2)>
HWY_INLINE V InterleaveLowerBlocks(D d, const V a, const V b) {
  const Half<D> dh;
  const VFromD<decltype(dh)> i0 =
      InterleaveLowerBlocks(dh, LowerHalf(dh, a), LowerHalf(dh, b));
  const VFromD<decltype(dh)> i1 =
      InterleaveLowerBlocks(dh, UpperHalf(dh, a), UpperHalf(dh, b));
  return Combine(d, i1, i0);
}

// As above, for the upper half of blocks.
template <class D, class V, HWY_IF_POW2_LE_D(D, 2)>
HWY_INLINE V InterleaveUpperBlocks(D d, const V a, const V b) {
  static_assert(IsSame<TFromD<D>, TFromV<V>>(), "D/V mismatch");
  const Twice<D> dt;
  const RebindToUnsigned<decltype(dt)> dt_u;
  const VFromD<decltype(dt)> interleaved = detail::InterleaveWhole(dt, a, b);
  // Keep only odd 128-bit blocks. This is faster than u64 ConcatEven
  // because we only have a single vector.
  constexpr size_t kShift = CeilLog2(16 / sizeof(TFromD<D>));
  const VFromD<decltype(dt_u)> idx_block =
      ShiftRight<kShift>(detail::Iota0(dt_u));
  const MFromD<decltype(dt_u)> is_odd =
      detail::EqS(detail::AndS(idx_block, 1), 1);
  return BitCast(d, LowerHalf(Compress(BitCast(dt_u, interleaved), is_odd)));
}
template <class D, class V, HWY_IF_POW2_GT_D(D, 2)>
HWY_INLINE V InterleaveUpperBlocks(D d, const V a, const V b) {
  const Half<D> dh;
  const VFromD<decltype(dh)> i0 =
      InterleaveUpperBlocks(dh, LowerHalf(dh, a), LowerHalf(dh, b));
  const VFromD<decltype(dh)> i1 =
      InterleaveUpperBlocks(dh, UpperHalf(dh, a), UpperHalf(dh, b));
  return Combine(d, i1, i0);
}

// RVV vectors are at least 128 bit when there is no fractional LMUL nor cap.
// Used by functions with per-block behavior such as InterleaveLower.
template <typename T, size_t N, int kPow2>
constexpr bool IsGE128(Simd<T, N, kPow2> /* d */) {
  return N * sizeof(T) >= 16 && kPow2 >= 0;
}

// Definitely less than 128-bit only if there is a small cap; fractional LMUL
// might not be enough if vectors are large.
template <typename T, size_t N, int kPow2>
constexpr bool IsLT128(Simd<T, N, kPow2> /* d */) {
  return N * sizeof(T) < 16;
}

}  // namespace detail

#define HWY_RVV_IF_GE128_D(D) hwy::EnableIf<detail::IsGE128(D())>* = nullptr
#define HWY_RVV_IF_LT128_D(D) hwy::EnableIf<detail::IsLT128(D())>* = nullptr
#define HWY_RVV_IF_CAN128_D(D) \
  hwy::EnableIf<!detail::IsLT128(D()) && !detail::IsGE128(D())>* = nullptr

template <class D, class V, HWY_RVV_IF_GE128_D(D)>
HWY_API V InterleaveLower(D d, const V a, const V b) {
  return detail::InterleaveLowerBlocks(d, a, b);
}

// Single block: interleave without extra Compress.
template <class D, class V, HWY_RVV_IF_LT128_D(D)>
HWY_API V InterleaveLower(D d, const V a, const V b) {
  static_assert(IsSame<TFromD<D>, TFromV<V>>(), "D/V mismatch");
  return InterleaveWholeLower(d, a, b);
}

// Could be either; branch at runtime.
template <class D, class V, HWY_RVV_IF_CAN128_D(D)>
HWY_API V InterleaveLower(D d, const V a, const V b) {
  if (Lanes(d) * sizeof(TFromD<D>) <= 16) {
    return InterleaveWholeLower(d, a, b);
  }
  // Fractional LMUL: use LMUL=1 to ensure we can cast to u64.
  const ScalableTag<TFromD<D>, HWY_MAX(d.Pow2(), 0)> d1;
  return ResizeBitCast(d, detail::InterleaveLowerBlocks(
                              d1, ResizeBitCast(d1, a), ResizeBitCast(d1, b)));
}

template <class V>
HWY_API V InterleaveLower(const V a, const V b) {
  return InterleaveLower(DFromV<V>(), a, b);
}

// ------------------------------ InterleaveUpper (Compress)

template <class D, class V, HWY_RVV_IF_GE128_D(D)>
HWY_API V InterleaveUpper(D d, const V a, const V b) {
  return detail::InterleaveUpperBlocks(d, a, b);
}

// Single block: interleave without extra Compress.
template <class D, class V, HWY_RVV_IF_LT128_D(D)>
HWY_API V InterleaveUpper(D d, const V a, const V b) {
  static_assert(IsSame<TFromD<D>, TFromV<V>>(), "D/V mismatch");
  return InterleaveWholeUpper(d, a, b);
}

// Could be either; branch at runtime.
template <class D, class V, HWY_RVV_IF_CAN128_D(D)>
HWY_API V InterleaveUpper(D d, const V a, const V b) {
  if (Lanes(d) * sizeof(TFromD<D>) <= 16) {
    return InterleaveWholeUpper(d, a, b);
  }
  // Fractional LMUL: use LMUL=1 to ensure we can cast to u64.
  const ScalableTag<TFromD<D>, HWY_MAX(d.Pow2(), 0)> d1;
  return ResizeBitCast(d, detail::InterleaveUpperBlocks(
                              d1, ResizeBitCast(d1, a), ResizeBitCast(d1, b)));
}

// ------------------------------ ZipLower

template <class V, class DW = RepartitionToWide<DFromV<V>>>
HWY_API VFromD<DW> ZipLower(DW dw, V a, V b) {
  const RepartitionToNarrow<DW> dn;
  static_assert(IsSame<TFromD<decltype(dn)>, TFromV<V>>(), "D/V mismatch");
  return BitCast(dw, InterleaveLower(dn, a, b));
}

template <class V, class DW = RepartitionToWide<DFromV<V>>>
HWY_API VFromD<DW> ZipLower(V a, V b) {
  return BitCast(DW(), InterleaveLower(a, b));
}

// ------------------------------ ZipUpper
template <class DW, class V>
HWY_API VFromD<DW> ZipUpper(DW dw, V a, V b) {
  const RepartitionToNarrow<DW> dn;
  static_assert(IsSame<TFromD<decltype(dn)>, TFromV<V>>(), "D/V mismatch");
  return BitCast(dw, InterleaveUpper(dn, a, b));
}

// ================================================== REDUCE

// We have ReduceSum, generic_ops-inl.h defines SumOfLanes via Set.
#ifdef HWY_NATIVE_REDUCE_SCALAR
#undef HWY_NATIVE_REDUCE_SCALAR
#else
#define HWY_NATIVE_REDUCE_SCALAR
#endif

// scalar = f(vector, zero_m1)
#define HWY_RVV_REDUCE(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                       MLEN, NAME, OP)                                         \
  template <size_t N>                                                          \
  HWY_API HWY_RVV_T(BASE, SEW)                                                 \
      NAME(HWY_RVV_D(BASE, SEW, N, SHIFT) d, HWY_RVV_V(BASE, SEW, LMUL) v,     \
           HWY_RVV_V(BASE, SEW, m1) v0) {                                      \
    return GetLane(__riscv_v##OP##_vs_##CHAR##SEW##LMUL##_##CHAR##SEW##m1(     \
        v, v0, Lanes(d)));                                                     \
  }

// detail::RedSum, detail::RedMin, and detail::RedMax is more efficient
// for N=4 I8/U8 reductions on RVV than the default implementations of the
// the N=4 I8/U8 ReduceSum/ReduceMin/ReduceMax operations in generic_ops-inl.h
#undef HWY_IF_REDUCE_D
#define HWY_IF_REDUCE_D(D) hwy::EnableIf<HWY_MAX_LANES_D(D) != 1>* = nullptr

#ifdef HWY_NATIVE_REDUCE_SUM_4_UI8
#undef HWY_NATIVE_REDUCE_SUM_4_UI8
#else
#define HWY_NATIVE_REDUCE_SUM_4_UI8
#endif

#ifdef HWY_NATIVE_REDUCE_MINMAX_4_UI8
#undef HWY_NATIVE_REDUCE_MINMAX_4_UI8
#else
#define HWY_NATIVE_REDUCE_MINMAX_4_UI8
#endif

// ------------------------------ ReduceSum

namespace detail {
HWY_RVV_FOREACH_UI(HWY_RVV_REDUCE, RedSum, redsum, _ALL_VIRT)
HWY_RVV_FOREACH_F(HWY_RVV_REDUCE, RedSum, fredusum, _ALL_VIRT)
}  // namespace detail

template <class D, HWY_IF_REDUCE_D(D)>
HWY_API TFromD<D> ReduceSum(D d, const VFromD<D> v) {
  const auto v0 = Zero(ScalableTag<TFromD<D>>());  // always m1
  return detail::RedSum(d, v, v0);
}

// ------------------------------ ReduceMin
namespace detail {
HWY_RVV_FOREACH_U(HWY_RVV_REDUCE, RedMin, redminu, _ALL_VIRT)
HWY_RVV_FOREACH_I(HWY_RVV_REDUCE, RedMin, redmin, _ALL_VIRT)
HWY_RVV_FOREACH_F(HWY_RVV_REDUCE, RedMin, fredmin, _ALL_VIRT)
}  // namespace detail

template <class D, typename T = TFromD<D>, HWY_IF_REDUCE_D(D)>
HWY_API T ReduceMin(D d, const VFromD<D> v) {
  const ScalableTag<T> d1;  // always m1
  return detail::RedMin(d, v, Set(d1, HighestValue<T>()));
}

// ------------------------------ ReduceMax
namespace detail {
HWY_RVV_FOREACH_U(HWY_RVV_REDUCE, RedMax, redmaxu, _ALL_VIRT)
HWY_RVV_FOREACH_I(HWY_RVV_REDUCE, RedMax, redmax, _ALL_VIRT)
HWY_RVV_FOREACH_F(HWY_RVV_REDUCE, RedMax, fredmax, _ALL_VIRT)
}  // namespace detail

template <class D, typename T = TFromD<D>, HWY_IF_REDUCE_D(D)>
HWY_API T ReduceMax(D d, const VFromD<D> v) {
  const ScalableTag<T> d1;  // always m1
  return detail::RedMax(d, v, Set(d1, LowestValue<T>()));
}

#undef HWY_RVV_REDUCE

// ------------------------------ SumOfLanes

template <class D, HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> SumOfLanes(D d, VFromD<D> v) {
  return Set(d, ReduceSum(d, v));
}
template <class D, HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> MinOfLanes(D d, VFromD<D> v) {
  return Set(d, ReduceMin(d, v));
}
template <class D, HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> MaxOfLanes(D d, VFromD<D> v) {
  return Set(d, ReduceMax(d, v));
}

// ================================================== Ops with dependencies

// ------------------------------ LoadInterleaved2

// Per-target flag to prevent generic_ops-inl.h from defining LoadInterleaved2.
#ifdef HWY_NATIVE_LOAD_STORE_INTERLEAVED
#undef HWY_NATIVE_LOAD_STORE_INTERLEAVED
#else
#define HWY_NATIVE_LOAD_STORE_INTERLEAVED
#endif

// Requires Clang 16+, GCC 14+; otherwise emulated in generic_ops-inl.h.
#if HWY_HAVE_TUPLE

#define HWY_RVV_GET(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT,   \
                    MLEN, NAME, OP)                                           \
  template <size_t kIndex>                                                    \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                          \
      NAME##2(HWY_RVV_TUP(BASE, SEW, LMUL, 2) tup) {                          \
    return __riscv_v##OP##_v_##CHAR##SEW##LMUL##x2_##CHAR##SEW##LMUL(tup,     \
                                                                     kIndex); \
  }                                                                           \
  template <size_t kIndex>                                                    \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                          \
      NAME##3(HWY_RVV_TUP(BASE, SEW, LMUL, 3) tup) {                          \
    return __riscv_v##OP##_v_##CHAR##SEW##LMUL##x3_##CHAR##SEW##LMUL(tup,     \
                                                                     kIndex); \
  }                                                                           \
  template <size_t kIndex>                                                    \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                          \
      NAME##4(HWY_RVV_TUP(BASE, SEW, LMUL, 4) tup) {                          \
    return __riscv_v##OP##_v_##CHAR##SEW##LMUL##x4_##CHAR##SEW##LMUL(tup,     \
                                                                     kIndex); \
  }

HWY_RVV_FOREACH(HWY_RVV_GET, Get, get, _LE2)
#undef HWY_RVV_GET

#define HWY_RVV_SET(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                    MLEN, NAME, OP)                                         \
  template <size_t kIndex>                                                  \
  HWY_API HWY_RVV_TUP(BASE, SEW, LMUL, 2) NAME##2(                          \
      HWY_RVV_TUP(BASE, SEW, LMUL, 2) tup, HWY_RVV_V(BASE, SEW, LMUL) v) {  \
    return __riscv_v##OP##_v_##CHAR##SEW##LMUL##_##CHAR##SEW##LMUL##x2(     \
        tup, kIndex, v);                                                    \
  }                                                                         \
  template <size_t kIndex>                                                  \
  HWY_API HWY_RVV_TUP(BASE, SEW, LMUL, 3) NAME##3(                          \
      HWY_RVV_TUP(BASE, SEW, LMUL, 3) tup, HWY_RVV_V(BASE, SEW, LMUL) v) {  \
    return __riscv_v##OP##_v_##CHAR##SEW##LMUL##_##CHAR##SEW##LMUL##x3(     \
        tup, kIndex, v);                                                    \
  }                                                                         \
  template <size_t kIndex>                                                  \
  HWY_API HWY_RVV_TUP(BASE, SEW, LMUL, 4) NAME##4(                          \
      HWY_RVV_TUP(BASE, SEW, LMUL, 4) tup, HWY_RVV_V(BASE, SEW, LMUL) v) {  \
    return __riscv_v##OP##_v_##CHAR##SEW##LMUL##_##CHAR##SEW##LMUL##x4(     \
        tup, kIndex, v);                                                    \
  }

HWY_RVV_FOREACH(HWY_RVV_SET, Set, set, _LE2)
#undef HWY_RVV_SET

// RVV does not provide vcreate, so implement using Set.
#define HWY_RVV_CREATE(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                       MLEN, NAME, OP)                                         \
  template <size_t N>                                                          \
  HWY_API HWY_RVV_TUP(BASE, SEW, LMUL, 2)                                      \
      NAME##2(HWY_RVV_D(BASE, SEW, N, SHIFT) /*d*/,                            \
              HWY_RVV_V(BASE, SEW, LMUL) v0, HWY_RVV_V(BASE, SEW, LMUL) v1) {  \
    HWY_RVV_TUP(BASE, SEW, LMUL, 2) tup{};                                     \
    tup = Set2<0>(tup, v0);                                                    \
    tup = Set2<1>(tup, v1);                                                    \
    return tup;                                                                \
  }                                                                            \
  template <size_t N>                                                          \
  HWY_API HWY_RVV_TUP(BASE, SEW, LMUL, 3) NAME##3(                             \
      HWY_RVV_D(BASE, SEW, N, SHIFT) /*d*/, HWY_RVV_V(BASE, SEW, LMUL) v0,     \
      HWY_RVV_V(BASE, SEW, LMUL) v1, HWY_RVV_V(BASE, SEW, LMUL) v2) {          \
    HWY_RVV_TUP(BASE, SEW, LMUL, 3) tup{};                                     \
    tup = Set3<0>(tup, v0);                                                    \
    tup = Set3<1>(tup, v1);                                                    \
    tup = Set3<2>(tup, v2);                                                    \
    return tup;                                                                \
  }                                                                            \
  template <size_t N>                                                          \
  HWY_API HWY_RVV_TUP(BASE, SEW, LMUL, 4)                                      \
      NAME##4(HWY_RVV_D(BASE, SEW, N, SHIFT) /*d*/,                            \
              HWY_RVV_V(BASE, SEW, LMUL) v0, HWY_RVV_V(BASE, SEW, LMUL) v1,    \
              HWY_RVV_V(BASE, SEW, LMUL) v2, HWY_RVV_V(BASE, SEW, LMUL) v3) {  \
    HWY_RVV_TUP(BASE, SEW, LMUL, 4) tup{};                                     \
    tup = Set4<0>(tup, v0);                                                    \
    tup = Set4<1>(tup, v1);                                                    \
    tup = Set4<2>(tup, v2);                                                    \
    tup = Set4<3>(tup, v3);                                                    \
    return tup;                                                                \
  }

HWY_RVV_FOREACH(HWY_RVV_CREATE, Create, xx, _LE2_VIRT)
#undef HWY_RVV_CREATE

template <class D>
using Vec2 = decltype(Create2(D(), Zero(D()), Zero(D())));
template <class D>
using Vec3 = decltype(Create3(D(), Zero(D()), Zero(D()), Zero(D())));
template <class D>
using Vec4 = decltype(Create4(D(), Zero(D()), Zero(D()), Zero(D()), Zero(D())));

#define HWY_RVV_LOAD2(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                      MLEN, NAME, OP)                                         \
  template <size_t N>                                                         \
  HWY_API void NAME(HWY_RVV_D(BASE, SEW, N, SHIFT) d,                         \
                    const HWY_RVV_T(BASE, SEW) * HWY_RESTRICT unaligned,      \
                    HWY_RVV_V(BASE, SEW, LMUL) & v0,                          \
                    HWY_RVV_V(BASE, SEW, LMUL) & v1) {                        \
    const HWY_RVV_TUP(BASE, SEW, LMUL, 2) tup =                               \
        __riscv_v##OP##e##SEW##_v_##CHAR##SEW##LMUL##x2(unaligned, Lanes(d)); \
    v0 = Get2<0>(tup);                                                        \
    v1 = Get2<1>(tup);                                                        \
  }
// Segments are limited to 8 registers, so we can only go up to LMUL=2.
HWY_RVV_FOREACH(HWY_RVV_LOAD2, LoadInterleaved2, lseg2, _LE2_VIRT)
#undef HWY_RVV_LOAD2

// ------------------------------ LoadInterleaved3

#define HWY_RVV_LOAD3(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                      MLEN, NAME, OP)                                         \
  template <size_t N>                                                         \
  HWY_API void NAME(HWY_RVV_D(BASE, SEW, N, SHIFT) d,                         \
                    const HWY_RVV_T(BASE, SEW) * HWY_RESTRICT unaligned,      \
                    HWY_RVV_V(BASE, SEW, LMUL) & v0,                          \
                    HWY_RVV_V(BASE, SEW, LMUL) & v1,                          \
                    HWY_RVV_V(BASE, SEW, LMUL) & v2) {                        \
    const HWY_RVV_TUP(BASE, SEW, LMUL, 3) tup =                               \
        __riscv_v##OP##e##SEW##_v_##CHAR##SEW##LMUL##x3(unaligned, Lanes(d)); \
    v0 = Get3<0>(tup);                                                        \
    v1 = Get3<1>(tup);                                                        \
    v2 = Get3<2>(tup);                                                        \
  }
// Segments are limited to 8 registers, so we can only go up to LMUL=2.
HWY_RVV_FOREACH(HWY_RVV_LOAD3, LoadInterleaved3, lseg3, _LE2_VIRT)
#undef HWY_RVV_LOAD3

// ------------------------------ LoadInterleaved4

#define HWY_RVV_LOAD4(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                      MLEN, NAME, OP)                                         \
  template <size_t N>                                                         \
  HWY_API void NAME(                                                          \
      HWY_RVV_D(BASE, SEW, N, SHIFT) d,                                       \
      const HWY_RVV_T(BASE, SEW) * HWY_RESTRICT unaligned,                    \
      HWY_RVV_V(BASE, SEW, LMUL) & v0, HWY_RVV_V(BASE, SEW, LMUL) & v1,       \
      HWY_RVV_V(BASE, SEW, LMUL) & v2, HWY_RVV_V(BASE, SEW, LMUL) & v3) {     \
    const HWY_RVV_TUP(BASE, SEW, LMUL, 4) tup =                               \
        __riscv_v##OP##e##SEW##_v_##CHAR##SEW##LMUL##x4(unaligned, Lanes(d)); \
    v0 = Get4<0>(tup);                                                        \
    v1 = Get4<1>(tup);                                                        \
    v2 = Get4<2>(tup);                                                        \
    v3 = Get4<3>(tup);                                                        \
  }
// Segments are limited to 8 registers, so we can only go up to LMUL=2.
HWY_RVV_FOREACH(HWY_RVV_LOAD4, LoadInterleaved4, lseg4, _LE2_VIRT)
#undef HWY_RVV_LOAD4

// ------------------------------ StoreInterleaved2

#define HWY_RVV_STORE2(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                       MLEN, NAME, OP)                                         \
  template <size_t N>                                                          \
  HWY_API void NAME(HWY_RVV_V(BASE, SEW, LMUL) v0,                             \
                    HWY_RVV_V(BASE, SEW, LMUL) v1,                             \
                    HWY_RVV_D(BASE, SEW, N, SHIFT) d,                          \
                    HWY_RVV_T(BASE, SEW) * HWY_RESTRICT unaligned) {           \
    const HWY_RVV_TUP(BASE, SEW, LMUL, 2) tup = Create2(d, v0, v1);            \
    __riscv_v##OP##e##SEW##_v_##CHAR##SEW##LMUL##x2(unaligned, tup, Lanes(d)); \
  }
// Segments are limited to 8 registers, so we can only go up to LMUL=2.
HWY_RVV_FOREACH(HWY_RVV_STORE2, StoreInterleaved2, sseg2, _LE2_VIRT)
#undef HWY_RVV_STORE2

// ------------------------------ StoreInterleaved3

#define HWY_RVV_STORE3(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                       MLEN, NAME, OP)                                         \
  template <size_t N>                                                          \
  HWY_API void NAME(                                                           \
      HWY_RVV_V(BASE, SEW, LMUL) v0, HWY_RVV_V(BASE, SEW, LMUL) v1,            \
      HWY_RVV_V(BASE, SEW, LMUL) v2, HWY_RVV_D(BASE, SEW, N, SHIFT) d,         \
      HWY_RVV_T(BASE, SEW) * HWY_RESTRICT unaligned) {                         \
    const HWY_RVV_TUP(BASE, SEW, LMUL, 3) tup = Create3(d, v0, v1, v2);        \
    __riscv_v##OP##e##SEW##_v_##CHAR##SEW##LMUL##x3(unaligned, tup, Lanes(d)); \
  }
// Segments are limited to 8 registers, so we can only go up to LMUL=2.
HWY_RVV_FOREACH(HWY_RVV_STORE3, StoreInterleaved3, sseg3, _LE2_VIRT)
#undef HWY_RVV_STORE3

// ------------------------------ StoreInterleaved4

#define HWY_RVV_STORE4(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, SHIFT, \
                       MLEN, NAME, OP)                                         \
  template <size_t N>                                                          \
  HWY_API void NAME(                                                           \
      HWY_RVV_V(BASE, SEW, LMUL) v0, HWY_RVV_V(BASE, SEW, LMUL) v1,            \
      HWY_RVV_V(BASE, SEW, LMUL) v2, HWY_RVV_V(BASE, SEW, LMUL) v3,            \
      HWY_RVV_D(BASE, SEW, N, SHIFT) d,                                        \
      HWY_RVV_T(BASE, SEW) * HWY_RESTRICT unaligned) {                         \
    const HWY_RVV_TUP(BASE, SEW, LMUL, 4) tup = Create4(d, v0, v1, v2, v3);    \
    __riscv_v##OP##e##SEW##_v_##CHAR##SEW##LMUL##x4(unaligned, tup, Lanes(d)); \
  }
// Segments are limited to 8 registers, so we can only go up to LMUL=2.
HWY_RVV_FOREACH(HWY_RVV_STORE4, StoreInterleaved4, sseg4, _LE2_VIRT)
#undef HWY_RVV_STORE4

#else  // !HWY_HAVE_TUPLE

template <class D, typename T = TFromD<D>, HWY_RVV_IF_NOT_EMULATED_D(D)>
HWY_API void LoadInterleaved2(D d, const T* HWY_RESTRICT unaligned,
                              VFromD<D>& v0, VFromD<D>& v1) {
  const VFromD<D> A = LoadU(d, unaligned);  // v1[1] v0[1] v1[0] v0[0]
  const VFromD<D> B = LoadU(d, unaligned + Lanes(d));
  v0 = ConcatEven(d, B, A);
  v1 = ConcatOdd(d, B, A);
}

namespace detail {
#define HWY_RVV_LOAD_STRIDED(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                             SHIFT, MLEN, NAME, OP)                           \
  template <size_t N>                                                         \
  HWY_API HWY_RVV_V(BASE, SEW, LMUL)                                          \
      NAME(HWY_RVV_D(BASE, SEW, N, SHIFT) d,                                  \
           const HWY_RVV_T(BASE, SEW) * HWY_RESTRICT p, size_t stride) {      \
    return __riscv_v##OP##SEW##_v_##CHAR##SEW##LMUL(                          \
        p, static_cast<ptrdiff_t>(stride), Lanes(d));                         \
  }
HWY_RVV_FOREACH(HWY_RVV_LOAD_STRIDED, LoadStrided, lse, _ALL_VIRT)
#undef HWY_RVV_LOAD_STRIDED
}  // namespace detail

template <class D, typename T = TFromD<D>, HWY_RVV_IF_NOT_EMULATED_D(D)>
HWY_API void LoadInterleaved3(D d, const TFromD<D>* HWY_RESTRICT unaligned,
                              VFromD<D>& v0, VFromD<D>& v1, VFromD<D>& v2) {
  // Offsets are bytes, and this is not documented.
  v0 = detail::LoadStrided(d, unaligned + 0, 3 * sizeof(T));
  v1 = detail::LoadStrided(d, unaligned + 1, 3 * sizeof(T));
  v2 = detail::LoadStrided(d, unaligned + 2, 3 * sizeof(T));
}

template <class D, typename T = TFromD<D>, HWY_RVV_IF_NOT_EMULATED_D(D)>
HWY_API void LoadInterleaved4(D d, const TFromD<D>* HWY_RESTRICT unaligned,
                              VFromD<D>& v0, VFromD<D>& v1, VFromD<D>& v2,
                              VFromD<D>& v3) {
  // Offsets are bytes, and this is not documented.
  v0 = detail::LoadStrided(d, unaligned + 0, 4 * sizeof(T));
  v1 = detail::LoadStrided(d, unaligned + 1, 4 * sizeof(T));
  v2 = detail::LoadStrided(d, unaligned + 2, 4 * sizeof(T));
  v3 = detail::LoadStrided(d, unaligned + 3, 4 * sizeof(T));
}

// Not 64-bit / max LMUL: interleave via promote, slide, OddEven.
template <class D, typename T = TFromD<D>, HWY_IF_NOT_T_SIZE_D(D, 8),
          HWY_IF_POW2_LE_D(D, 2), HWY_RVV_IF_NOT_EMULATED_D(D)>
HWY_API void StoreInterleaved2(VFromD<D> v0, VFromD<D> v1, D d,
                               T* HWY_RESTRICT unaligned) {
  const RebindToUnsigned<D> du;
  const Twice<RepartitionToWide<decltype(du)>> duw;
  const Twice<decltype(d)> dt;
  // Interleave with zero by promoting to wider (unsigned) type.
  const VFromD<decltype(dt)> w0 = BitCast(dt, PromoteTo(duw, BitCast(du, v0)));
  const VFromD<decltype(dt)> w1 = BitCast(dt, PromoteTo(duw, BitCast(du, v1)));
  // OR second vector into the zero-valued lanes (faster than OddEven).
  StoreU(Or(w0, detail::Slide1Up(w1)), dt, unaligned);
}

// Can promote, max LMUL: two half-length
template <class D, typename T = TFromD<D>, HWY_IF_NOT_T_SIZE_D(D, 8),
          HWY_IF_POW2_GT_D(D, 2), HWY_RVV_IF_NOT_EMULATED_D(D)>
HWY_API void StoreInterleaved2(VFromD<D> v0, VFromD<D> v1, D d,
                               T* HWY_RESTRICT unaligned) {
  const Half<decltype(d)> dh;
  StoreInterleaved2(LowerHalf(dh, v0), LowerHalf(dh, v1), d, unaligned);
  StoreInterleaved2(UpperHalf(dh, v0), UpperHalf(dh, v1), d,
                    unaligned + Lanes(d));
}

namespace detail {
#define HWY_RVV_STORE_STRIDED(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH, \
                              SHIFT, MLEN, NAME, OP)                           \
  template <size_t N>                                                          \
  HWY_API void NAME(HWY_RVV_V(BASE, SEW, LMUL) v,                              \
                    HWY_RVV_D(BASE, SEW, N, SHIFT) d,                          \
                    HWY_RVV_T(BASE, SEW) * HWY_RESTRICT p, size_t stride) {    \
    return __riscv_v##OP##SEW##_v_##CHAR##SEW##LMUL(                           \
        p, static_cast<ptrdiff_t>(stride), v, Lanes(d));                       \
  }
HWY_RVV_FOREACH(HWY_RVV_STORE_STRIDED, StoreStrided, sse, _ALL_VIRT)
#undef HWY_RVV_STORE_STRIDED
}  // namespace detail

// 64-bit: strided
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE_D(D, 8),
          HWY_RVV_IF_NOT_EMULATED_D(D)>
HWY_API void StoreInterleaved2(VFromD<D> v0, VFromD<D> v1, D d,
                               T* HWY_RESTRICT unaligned) {
  // Offsets are bytes, and this is not documented.
  detail::StoreStrided(v0, d, unaligned + 0, 2 * sizeof(T));
  detail::StoreStrided(v1, d, unaligned + 1, 2 * sizeof(T));
}

template <class D, typename T = TFromD<D>, HWY_RVV_IF_NOT_EMULATED_D(D)>
HWY_API void StoreInterleaved3(VFromD<D> v0, VFromD<D> v1, VFromD<D> v2, D d,
                               T* HWY_RESTRICT unaligned) {
  // Offsets are bytes, and this is not documented.
  detail::StoreStrided(v0, d, unaligned + 0, 3 * sizeof(T));
  detail::StoreStrided(v1, d, unaligned + 1, 3 * sizeof(T));
  detail::StoreStrided(v2, d, unaligned + 2, 3 * sizeof(T));
}

template <class D, typename T = TFromD<D>, HWY_RVV_IF_NOT_EMULATED_D(D)>
HWY_API void StoreInterleaved4(VFromD<D> v0, VFromD<D> v1, VFromD<D> v2,
                               VFromD<D> v3, D d, T* HWY_RESTRICT unaligned) {
  // Offsets are bytes, and this is not documented.
  detail::StoreStrided(v0, d, unaligned + 0, 4 * sizeof(T));
  detail::StoreStrided(v1, d, unaligned + 1, 4 * sizeof(T));
  detail::StoreStrided(v2, d, unaligned + 2, 4 * sizeof(T));
  detail::StoreStrided(v3, d, unaligned + 3, 4 * sizeof(T));
}

#endif  // HWY_HAVE_TUPLE

// Rely on generic Load/StoreInterleaved[234] for any emulated types.
// Requires HWY_GENERIC_IF_EMULATED_D mirrors HWY_RVV_IF_EMULATED_D.

// ------------------------------ Dup128VecFromValues (ResizeBitCast)

template <class D, HWY_IF_T_SIZE_D(D, 8), HWY_IF_LANES_D(D, 1)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> /*t1*/) {
  return Set(d, t0);
}

template <class D, HWY_IF_T_SIZE_D(D, 8), HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1) {
  const auto even_lanes = Set(d, t0);
#if HWY_COMPILER_GCC && !HWY_IS_DEBUG_BUILD
  if (__builtin_constant_p(BitCastScalar<uint64_t>(t0) ==
                           BitCastScalar<uint64_t>(t1)) &&
      (BitCastScalar<uint64_t>(t0) == BitCastScalar<uint64_t>(t1))) {
    return even_lanes;
  }
#endif

  const auto odd_lanes = Set(d, t1);
  return OddEven(odd_lanes, even_lanes);
}

namespace detail {

#pragma pack(push, 1)

template <class T>
struct alignas(8) Vec64ValsWrapper {
  static_assert(sizeof(T) >= 1, "sizeof(T) >= 1 must be true");
  static_assert(sizeof(T) <= 8, "sizeof(T) <= 8 must be true");
  T vals[8 / sizeof(T)];
};

#pragma pack(pop)

}  // namespace detail

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6, TFromD<D> t7,
                                      TFromD<D> t8, TFromD<D> t9, TFromD<D> t10,
                                      TFromD<D> t11, TFromD<D> t12,
                                      TFromD<D> t13, TFromD<D> t14,
                                      TFromD<D> t15) {
  const detail::AdjustSimdTagToMinVecPow2<Repartition<uint64_t, D>> du64;
  return ResizeBitCast(
      d, Dup128VecFromValues(
             du64,
             BitCastScalar<uint64_t>(detail::Vec64ValsWrapper<TFromD<D>>{
                 {t0, t1, t2, t3, t4, t5, t6, t7}}),
             BitCastScalar<uint64_t>(detail::Vec64ValsWrapper<TFromD<D>>{
                 {t8, t9, t10, t11, t12, t13, t14, t15}})));
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6,
                                      TFromD<D> t7) {
  const detail::AdjustSimdTagToMinVecPow2<Repartition<uint64_t, D>> du64;
  return ResizeBitCast(
      d, Dup128VecFromValues(
             du64,
             BitCastScalar<uint64_t>(
                 detail::Vec64ValsWrapper<TFromD<D>>{{t0, t1, t2, t3}}),
             BitCastScalar<uint64_t>(
                 detail::Vec64ValsWrapper<TFromD<D>>{{t4, t5, t6, t7}})));
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3) {
  const detail::AdjustSimdTagToMinVecPow2<Repartition<uint64_t, D>> du64;
  return ResizeBitCast(
      d,
      Dup128VecFromValues(du64,
                          BitCastScalar<uint64_t>(
                              detail::Vec64ValsWrapper<TFromD<D>>{{t0, t1}}),
                          BitCastScalar<uint64_t>(
                              detail::Vec64ValsWrapper<TFromD<D>>{{t2, t3}})));
}

// ------------------------------ PopulationCount (ShiftRight)

// Handles LMUL < 2 or capped vectors, which generic_ops-inl cannot.
template <typename V, class D = DFromV<V>, HWY_IF_U8_D(D),
          hwy::EnableIf<D().Pow2() < 1 || D().MaxLanes() < 16>* = nullptr>
HWY_API V PopulationCount(V v) {
  // See https://arxiv.org/pdf/1611.07612.pdf, Figure 3
  v = Sub(v, detail::AndS(ShiftRight<1>(v), 0x55));
  v = Add(detail::AndS(ShiftRight<2>(v), 0x33), detail::AndS(v, 0x33));
  return detail::AndS(Add(v, ShiftRight<4>(v)), 0x0F);
}

// ------------------------------ LoadDup128

template <class D>
HWY_API VFromD<D> LoadDup128(D d, const TFromD<D>* const HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;

  // Make sure that no more than 16 bytes are loaded from p
  constexpr int kLoadPow2 = d.Pow2();
  constexpr size_t kMaxLanesToLoad =
      HWY_MIN(HWY_MAX_LANES_D(D), 16 / sizeof(TFromD<D>));
  constexpr size_t kLoadN = D::template NewN<kLoadPow2, kMaxLanesToLoad>();
  const Simd<TFromD<D>, kLoadN, kLoadPow2> d_load;
  static_assert(d_load.MaxBytes() <= 16,
                "d_load.MaxBytes() <= 16 must be true");
  static_assert((d.MaxBytes() < 16) || (d_load.MaxBytes() == 16),
                "d_load.MaxBytes() == 16 must be true if d.MaxBytes() >= 16 is "
                "true");
  static_assert((d.MaxBytes() >= 16) || (d_load.MaxBytes() == d.MaxBytes()),
                "d_load.MaxBytes() == d.MaxBytes() must be true if "
                "d.MaxBytes() < 16 is true");

  const VFromD<D> loaded = Load(d_load, p);
  if (d.MaxBytes() <= 16) return loaded;

  // idx must be unsigned for TableLookupLanes.
  using TU = TFromD<decltype(du)>;
  const TU mask = static_cast<TU>(detail::LanesPerBlock(d) - 1);
  // Broadcast the first block.
  const VFromD<RebindToUnsigned<D>> idx = detail::AndS(detail::Iota0(du), mask);
  // Safe even for 8-bit lanes because indices never exceed 15.
  return TableLookupLanes(loaded, idx);
}

// ------------------------------ LoadMaskBits

// Support all combinations of T and SHIFT(LMUL) without explicit overloads for
// each. First overload for MLEN=1..64.
namespace detail {

// Maps D to MLEN (wrapped in SizeTag), such that #mask_bits = VLEN/MLEN. MLEN
// increases with lane size and decreases for increasing LMUL. Cap at 64, the
// largest supported by HWY_RVV_FOREACH_B (and intrinsics), for virtual LMUL
// e.g. vuint16mf8_t: (8*2 << 3) == 128.
template <class D>
using MaskTag = hwy::SizeTag<HWY_MIN(
    64, detail::ScaleByPower(8 * sizeof(TFromD<D>), -D().Pow2()))>;

#define HWY_RVV_LOAD_MASK_BITS(SEW, SHIFT, MLEN, NAME, OP)                \
  HWY_INLINE HWY_RVV_M(MLEN)                                              \
      NAME(hwy::SizeTag<MLEN> /* tag */, const uint8_t* bits, size_t N) { \
    return __riscv_v##OP##_v_b##MLEN(bits, N);                            \
  }
HWY_RVV_FOREACH_B(HWY_RVV_LOAD_MASK_BITS, LoadMaskBits, lm)
#undef HWY_RVV_LOAD_MASK_BITS
}  // namespace detail

template <class D, class MT = detail::MaskTag<D>>
HWY_API auto LoadMaskBits(D d, const uint8_t* bits)
    -> decltype(detail::LoadMaskBits(MT(), bits, Lanes(d))) {
  return detail::LoadMaskBits(MT(), bits, Lanes(d));
}

// ------------------------------ StoreMaskBits
#define HWY_RVV_STORE_MASK_BITS(SEW, SHIFT, MLEN, NAME, OP)               \
  template <class D>                                                      \
  HWY_API size_t NAME(D d, HWY_RVV_M(MLEN) m, uint8_t* bits) {            \
    const size_t N = Lanes(d);                                            \
    __riscv_v##OP##_v_b##MLEN(bits, m, N);                                \
    /* Non-full byte, need to clear the undefined upper bits. */          \
    /* Use MaxLanes and sizeof(T) to move some checks to compile-time. */ \
    constexpr bool kLessThan8 =                                           \
        detail::ScaleByPower(16 / sizeof(TFromD<D>), d.Pow2()) < 8;       \
    if (MaxLanes(d) < 8 || (kLessThan8 && N < 8)) {                       \
      const int mask = (1 << N) - 1;                                      \
      bits[0] = static_cast<uint8_t>(bits[0] & mask);                     \
    }                                                                     \
    return (N + 7) / 8;                                                   \
  }
HWY_RVV_FOREACH_B(HWY_RVV_STORE_MASK_BITS, StoreMaskBits, sm)
#undef HWY_RVV_STORE_MASK_BITS

// ------------------------------ CompressBits, CompressBitsStore (LoadMaskBits)

template <class V>
HWY_INLINE V CompressBits(V v, const uint8_t* HWY_RESTRICT bits) {
  return Compress(v, LoadMaskBits(DFromV<V>(), bits));
}

template <class D>
HWY_API size_t CompressBitsStore(VFromD<D> v, const uint8_t* HWY_RESTRICT bits,
                                 D d, TFromD<D>* HWY_RESTRICT unaligned) {
  return CompressStore(v, LoadMaskBits(d, bits), d, unaligned);
}

// ------------------------------ FirstN (Iota0, Lt, RebindMask, SlideUp)

// NOTE: do not use this as a building block within rvv-inl - it is likely more
// efficient to use avl or detail::SlideUp.

// Disallow for 8-bit because Iota is likely to overflow.
template <class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API MFromD<D> FirstN(const D d, const size_t n) {
  const RebindToUnsigned<D> du;
  using TU = TFromD<decltype(du)>;
  return RebindMask(d, detail::LtS(detail::Iota0(du), static_cast<TU>(n)));
}

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_API MFromD<D> FirstN(const D d, const size_t n) {
  const auto zero = Zero(d);
  const auto one = Set(d, 1);
  return Eq(detail::SlideUp(one, zero, n), one);
}

// ------------------------------ LowerHalfOfMask/UpperHalfOfMask

#if HWY_COMPILER_CLANG >= 1700 || HWY_COMPILER_GCC_ACTUAL >= 1400

// Target-specific implementations of LowerHalfOfMask, UpperHalfOfMask,
// CombineMasks, OrderedDemote2MasksTo, and Dup128MaskFromMaskBits are possible
// on RVV if the __riscv_vreinterpret_v_b*_u8m1 and
// __riscv_vreinterpret_v_u8m1_b* intrinsics are available.

// The __riscv_vreinterpret_v_b*_u8m1 and __riscv_vreinterpret_v_u8m1_b*
// intrinsics available with Clang 17 and later and GCC 14 and later.

namespace detail {

HWY_INLINE vuint8m1_t MaskToU8MaskBitsVec(vbool1_t m) {
  return __riscv_vreinterpret_v_b1_u8m1(m);
}

HWY_INLINE vuint8m1_t MaskToU8MaskBitsVec(vbool2_t m) {
  return __riscv_vreinterpret_v_b2_u8m1(m);
}

HWY_INLINE vuint8m1_t MaskToU8MaskBitsVec(vbool4_t m) {
  return __riscv_vreinterpret_v_b4_u8m1(m);
}

HWY_INLINE vuint8m1_t MaskToU8MaskBitsVec(vbool8_t m) {
  return __riscv_vreinterpret_v_b8_u8m1(m);
}

HWY_INLINE vuint8m1_t MaskToU8MaskBitsVec(vbool16_t m) {
  return __riscv_vreinterpret_v_b16_u8m1(m);
}

HWY_INLINE vuint8m1_t MaskToU8MaskBitsVec(vbool32_t m) {
  return __riscv_vreinterpret_v_b32_u8m1(m);
}

HWY_INLINE vuint8m1_t MaskToU8MaskBitsVec(vbool64_t m) {
  return __riscv_vreinterpret_v_b64_u8m1(m);
}

template <class D, hwy::EnableIf<IsSame<MFromD<D>, vbool1_t>()>* = nullptr>
HWY_INLINE MFromD<D> U8MaskBitsVecToMask(D /*d*/, vuint8m1_t v) {
  return __riscv_vreinterpret_v_u8m1_b1(v);
}

template <class D, hwy::EnableIf<IsSame<MFromD<D>, vbool2_t>()>* = nullptr>
HWY_INLINE MFromD<D> U8MaskBitsVecToMask(D /*d*/, vuint8m1_t v) {
  return __riscv_vreinterpret_v_u8m1_b2(v);
}

template <class D, hwy::EnableIf<IsSame<MFromD<D>, vbool4_t>()>* = nullptr>
HWY_INLINE MFromD<D> U8MaskBitsVecToMask(D /*d*/, vuint8m1_t v) {
  return __riscv_vreinterpret_v_u8m1_b4(v);
}

template <class D, hwy::EnableIf<IsSame<MFromD<D>, vbool8_t>()>* = nullptr>
HWY_INLINE MFromD<D> U8MaskBitsVecToMask(D /*d*/, vuint8m1_t v) {
  return __riscv_vreinterpret_v_u8m1_b8(v);
}

template <class D, hwy::EnableIf<IsSame<MFromD<D>, vbool16_t>()>* = nullptr>
HWY_INLINE MFromD<D> U8MaskBitsVecToMask(D /*d*/, vuint8m1_t v) {
  return __riscv_vreinterpret_v_u8m1_b16(v);
}

template <class D, hwy::EnableIf<IsSame<MFromD<D>, vbool32_t>()>* = nullptr>
HWY_INLINE MFromD<D> U8MaskBitsVecToMask(D /*d*/, vuint8m1_t v) {
  return __riscv_vreinterpret_v_u8m1_b32(v);
}

template <class D, hwy::EnableIf<IsSame<MFromD<D>, vbool64_t>()>* = nullptr>
HWY_INLINE MFromD<D> U8MaskBitsVecToMask(D /*d*/, vuint8m1_t v) {
  return __riscv_vreinterpret_v_u8m1_b64(v);
}

}  // namespace detail

#ifdef HWY_NATIVE_LOWER_HALF_OF_MASK
#undef HWY_NATIVE_LOWER_HALF_OF_MASK
#else
#define HWY_NATIVE_LOWER_HALF_OF_MASK
#endif

template <class D>
HWY_API MFromD<D> LowerHalfOfMask(D d, MFromD<Twice<D>> m) {
  return detail::U8MaskBitsVecToMask(d, detail::MaskToU8MaskBitsVec(m));
}

#ifdef HWY_NATIVE_UPPER_HALF_OF_MASK
#undef HWY_NATIVE_UPPER_HALF_OF_MASK
#else
#define HWY_NATIVE_UPPER_HALF_OF_MASK
#endif

template <class D>
HWY_API MFromD<D> UpperHalfOfMask(D d, MFromD<Twice<D>> m) {
  const size_t N = Lanes(d);

  vuint8m1_t mask_bits = detail::MaskToU8MaskBitsVec(m);
  mask_bits = ShiftRightSame(mask_bits, static_cast<int>(N & 7));
  if (HWY_MAX_LANES_D(D) >= 8) {
    mask_bits = SlideDownLanes(ScalableTag<uint8_t>(), mask_bits, N / 8);
  }

  return detail::U8MaskBitsVecToMask(d, mask_bits);
}

// ------------------------------ CombineMasks

#ifdef HWY_NATIVE_COMBINE_MASKS
#undef HWY_NATIVE_COMBINE_MASKS
#else
#define HWY_NATIVE_COMBINE_MASKS
#endif

template <class D>
HWY_API MFromD<D> CombineMasks(D d, MFromD<Half<D>> hi, MFromD<Half<D>> lo) {
  const Half<decltype(d)> dh;
  const size_t half_N = Lanes(dh);

  const auto ext_lo_mask =
      And(detail::U8MaskBitsVecToMask(d, detail::MaskToU8MaskBitsVec(lo)),
          FirstN(d, half_N));
  vuint8m1_t hi_mask_bits = detail::MaskToU8MaskBitsVec(hi);
  hi_mask_bits = ShiftLeftSame(hi_mask_bits, static_cast<int>(half_N & 7));
  if (HWY_MAX_LANES_D(D) >= 8) {
    hi_mask_bits =
        SlideUpLanes(ScalableTag<uint8_t>(), hi_mask_bits, half_N / 8);
  }

  return Or(ext_lo_mask, detail::U8MaskBitsVecToMask(d, hi_mask_bits));
}

// ------------------------------ OrderedDemote2MasksTo

#ifdef HWY_NATIVE_ORDERED_DEMOTE_2_MASKS_TO
#undef HWY_NATIVE_ORDERED_DEMOTE_2_MASKS_TO
#else
#define HWY_NATIVE_ORDERED_DEMOTE_2_MASKS_TO
#endif

template <class DTo, class DFrom,
          HWY_IF_T_SIZE_D(DTo, sizeof(TFromD<DFrom>) / 2),
          class DTo_2 = Repartition<TFromD<DTo>, DFrom>,
          hwy::EnableIf<IsSame<MFromD<DTo>, MFromD<DTo_2>>()>* = nullptr>
HWY_API MFromD<DTo> OrderedDemote2MasksTo(DTo d_to, DFrom /*d_from*/,
                                          MFromD<DFrom> a, MFromD<DFrom> b) {
  return CombineMasks(d_to, b, a);
}

#endif  // HWY_COMPILER_CLANG >= 1700 || HWY_COMPILER_GCC_ACTUAL >= 1400

// ------------------------------ Dup128MaskFromMaskBits

namespace detail {
// Even though this is only used after checking if (kN < X), this helper
// function prevents "shift count exceeded" errors.
template <size_t kN, HWY_IF_LANES_LE(kN, 31)>
constexpr unsigned MaxMaskBits() {
  return (1u << kN) - 1;
}
template <size_t kN, HWY_IF_LANES_GT(kN, 31)>
constexpr unsigned MaxMaskBits() {
  return ~0u;
}

template <class D>
constexpr int SufficientPow2ForMask() {
  return HWY_MAX(
      D().Pow2() - 3 - static_cast<int>(FloorLog2(sizeof(TFromD<D>))), -3);
}
}  // namespace detail

template <class D, HWY_IF_T_SIZE_D(D, 1), HWY_IF_LANES_LE_D(D, 8)>
HWY_API MFromD<D> Dup128MaskFromMaskBits(D d, unsigned mask_bits) {
  constexpr size_t kN = MaxLanes(d);
  if (kN < 8) mask_bits &= detail::MaxMaskBits<kN>();

#if HWY_COMPILER_CLANG >= 1700 || HWY_COMPILER_GCC_ACTUAL >= 1400
  return detail::U8MaskBitsVecToMask(
      d, Set(ScalableTag<uint8_t>(), static_cast<uint8_t>(mask_bits)));
#else
  const RebindToUnsigned<decltype(d)> du8;
  const detail::AdjustSimdTagToMinVecPow2<Repartition<uint64_t, decltype(du8)>>
      du64;

  const auto bytes = ResizeBitCast(
      du8, detail::AndS(
               ResizeBitCast(du64, Set(du8, static_cast<uint8_t>(mask_bits))),
               uint64_t{0x8040201008040201u}));
  return detail::NeS(bytes, uint8_t{0});
#endif
}

template <class D, HWY_IF_T_SIZE_D(D, 1), HWY_IF_LANES_GT_D(D, 8)>
HWY_API MFromD<D> Dup128MaskFromMaskBits(D d, unsigned mask_bits) {
#if HWY_COMPILER_CLANG >= 1700 || HWY_COMPILER_GCC_ACTUAL >= 1400
  const ScalableTag<uint8_t, detail::SufficientPow2ForMask<D>()> du8;
  const ScalableTag<uint16_t, detail::SufficientPow2ForMask<D>()> du16;
  // There are exactly 16 mask bits for 128 vector bits of 8-bit lanes.
  return detail::U8MaskBitsVecToMask(
      d, detail::ChangeLMUL(
             ScalableTag<uint8_t>(),
             BitCast(du8, Set(du16, static_cast<uint16_t>(mask_bits)))));
#else
  // Slow fallback for completeness; the above bits to mask cast is preferred.
  const RebindToUnsigned<decltype(d)> du8;
  const Repartition<uint16_t, decltype(du8)> du16;
  const detail::AdjustSimdTagToMinVecPow2<Repartition<uint64_t, decltype(du8)>>
      du64;

  // Replicate the lower 16 bits of mask_bits to each u16 lane of a u16 vector,
  // and then bitcast the replicated mask_bits to a u8 vector
  const auto bytes = BitCast(du8, Set(du16, static_cast<uint16_t>(mask_bits)));
  // Replicate bytes 8x such that each byte contains the bit that governs it.
  const auto rep8 = TableLookupLanes(bytes, ShiftRight<3>(detail::Iota0(du8)));

  const auto masked_out_rep8 = ResizeBitCast(
      du8,
      detail::AndS(ResizeBitCast(du64, rep8), uint64_t{0x8040201008040201u}));
  return detail::NeS(masked_out_rep8, uint8_t{0});
#endif
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API MFromD<D> Dup128MaskFromMaskBits(D d, unsigned mask_bits) {
  constexpr size_t kN = MaxLanes(d);
  if (kN < 8) mask_bits &= detail::MaxMaskBits<kN>();

#if HWY_COMPILER_CLANG >= 1700 || HWY_COMPILER_GCC_ACTUAL >= 1400
  const ScalableTag<uint8_t, detail::SufficientPow2ForMask<D>()> du8;
  // There are exactly 8 mask bits for 128 vector bits of 16-bit lanes.
  return detail::U8MaskBitsVecToMask(
      d, detail::ChangeLMUL(ScalableTag<uint8_t>(),
                            Set(du8, static_cast<uint8_t>(mask_bits))));
#else
  // Slow fallback for completeness; the above bits to mask cast is preferred.
  const RebindToUnsigned<D> du;
  const VFromD<decltype(du)> bits =
      Shl(Set(du, uint16_t{1}), detail::AndS(detail::Iota0(du), 7));
  return TestBit(Set(du, static_cast<uint16_t>(mask_bits)), bits);
#endif
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_API MFromD<D> Dup128MaskFromMaskBits(D d, unsigned mask_bits) {
  constexpr size_t kN = MaxLanes(d);
  if (kN < 4) mask_bits &= detail::MaxMaskBits<kN>();

#if HWY_COMPILER_CLANG >= 1700 || HWY_COMPILER_GCC_ACTUAL >= 1400
  const ScalableTag<uint8_t, detail::SufficientPow2ForMask<D>()> du8;
  return detail::U8MaskBitsVecToMask(
      d, detail::ChangeLMUL(ScalableTag<uint8_t>(),
                            Set(du8, static_cast<uint8_t>(mask_bits * 0x11))));
#else
  // Slow fallback for completeness; the above bits to mask cast is preferred.
  const RebindToUnsigned<D> du;
  const VFromD<decltype(du)> bits = Dup128VecFromValues(du, 1, 2, 4, 8);
  return TestBit(Set(du, static_cast<uint32_t>(mask_bits)), bits);
#endif
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API MFromD<D> Dup128MaskFromMaskBits(D d, unsigned mask_bits) {
  constexpr size_t kN = MaxLanes(d);
  if (kN < 2) mask_bits &= detail::MaxMaskBits<kN>();

#if HWY_COMPILER_CLANG >= 1700 || HWY_COMPILER_GCC_ACTUAL >= 1400
  const ScalableTag<uint8_t, detail::SufficientPow2ForMask<D>()> du8;
  return detail::U8MaskBitsVecToMask(
      d, detail::ChangeLMUL(ScalableTag<uint8_t>(),
                            Set(du8, static_cast<uint8_t>(mask_bits * 0x55))));
#else
  // Slow fallback for completeness; the above bits to mask cast is preferred.
  const RebindToUnsigned<D> du;
  const VFromD<decltype(du)> bits = Dup128VecFromValues(du, 1, 2);
  return TestBit(Set(du, static_cast<uint64_t>(mask_bits)), bits);
#endif
}

// ------------------------------ Abs (Max, Neg)

template <class V, HWY_IF_SIGNED_V(V)>
HWY_API V Abs(const V v) {
  return Max(v, Neg(v));
}

HWY_RVV_FOREACH_F(HWY_RVV_RETV_ARGV2, Abs, fsgnjx, _ALL)

#undef HWY_RVV_RETV_ARGV2

// ------------------------------ AbsDiff (Abs, Sub)
template <class V, HWY_IF_FLOAT_V(V)>
HWY_API V AbsDiff(const V a, const V b) {
  return Abs(Sub(a, b));
}

// ------------------------------ Round  (NearestInt, ConvertTo, CopySign)

// IEEE-754 roundToIntegralTiesToEven returns floating-point, but we do not have
// a dedicated instruction for that. Rounding to integer and converting back to
// float is correct except when the input magnitude is large, in which case the
// input was already an integer (because mantissa >> exponent is zero).

namespace detail {
enum RoundingModes { kNear, kTrunc, kDown, kUp };

template <class V>
HWY_INLINE auto UseInt(const V v) -> decltype(MaskFromVec(v)) {
  return detail::LtS(Abs(v), MantissaEnd<TFromV<V>>());
}

}  // namespace detail

template <class V>
HWY_API V Round(const V v) {
  const DFromV<V> df;

  const auto integer = NearestInt(v);  // round using current mode
  const auto int_f = ConvertTo(df, integer);

  return IfThenElse(detail::UseInt(v), CopySign(int_f, v), v);
}

// ------------------------------ Trunc (ConvertTo)
template <class V>
HWY_API V Trunc(const V v) {
  const DFromV<V> df;
  const RebindToSigned<decltype(df)> di;

  const auto integer = ConvertTo(di, v);  // round toward 0
  const auto int_f = ConvertTo(df, integer);

  return IfThenElse(detail::UseInt(v), CopySign(int_f, v), v);
}

// ------------------------------ Ceil
#if (HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL >= 1400) || \
    (HWY_COMPILER_CLANG && HWY_COMPILER_CLANG >= 1700)
namespace detail {
#define HWY_RVV_CEIL_INT(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,   \
                         SHIFT, MLEN, NAME, OP)                             \
  HWY_API HWY_RVV_V(int, SEW, LMUL) CeilInt(HWY_RVV_V(BASE, SEW, LMUL) v) { \
    return __riscv_vfcvt_x_f_v_i##SEW##LMUL##_rm(v, __RISCV_FRM_RUP,        \
                                                 HWY_RVV_AVL(SEW, SHIFT));  \
  }
HWY_RVV_FOREACH_F(HWY_RVV_CEIL_INT, _, _, _ALL)
#undef HWY_RVV_CEIL_INT

}  // namespace detail

template <class V>
HWY_API V Ceil(const V v) {
  const DFromV<V> df;

  const auto integer = detail::CeilInt(v);
  const auto int_f = ConvertTo(df, integer);

  return IfThenElse(detail::UseInt(v), CopySign(int_f, v), v);
}

#else  // GCC 13 or earlier or Clang 16 or earlier

template <class V>
HWY_API V Ceil(const V v) {
  const DFromV<decltype(v)> df;
  const RebindToSigned<decltype(df)> di;

  using T = TFromD<decltype(df)>;

  const auto integer = ConvertTo(di, v);  // round toward 0
  const auto int_f = ConvertTo(df, integer);

  // Truncating a positive non-integer ends up smaller; if so, add 1.
  const auto pos1 =
      IfThenElseZero(Lt(int_f, v), Set(df, ConvertScalarTo<T>(1.0)));

  return IfThenElse(detail::UseInt(v), Add(int_f, pos1), v);
}

#endif  // (HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL >= 1400) ||
        // (HWY_COMPILER_CLANG && HWY_COMPILER_CLANG >= 1700)

// ------------------------------ Floor
#if (HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL >= 1400) || \
    (HWY_COMPILER_CLANG && HWY_COMPILER_CLANG >= 1700)
namespace detail {
#define HWY_RVV_FLOOR_INT(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,   \
                          SHIFT, MLEN, NAME, OP)                             \
  HWY_API HWY_RVV_V(int, SEW, LMUL) FloorInt(HWY_RVV_V(BASE, SEW, LMUL) v) { \
    return __riscv_vfcvt_x_f_v_i##SEW##LMUL##_rm(v, __RISCV_FRM_RDN,         \
                                                 HWY_RVV_AVL(SEW, SHIFT));   \
  }
HWY_RVV_FOREACH_F(HWY_RVV_FLOOR_INT, _, _, _ALL)
#undef HWY_RVV_FLOOR_INT

}  // namespace detail

template <class V>
HWY_API V Floor(const V v) {
  const DFromV<V> df;

  const auto integer = detail::FloorInt(v);
  const auto int_f = ConvertTo(df, integer);

  return IfThenElse(detail::UseInt(v), CopySign(int_f, v), v);
}

#else  // GCC 13 or earlier or Clang 16 or earlier

template <class V>
HWY_API V Floor(const V v) {
  const DFromV<decltype(v)> df;
  const RebindToSigned<decltype(df)> di;

  using T = TFromD<decltype(df)>;

  const auto integer = ConvertTo(di, v);  // round toward 0
  const auto int_f = ConvertTo(df, integer);

  // Truncating a negative non-integer ends up larger; if so, subtract 1.
  const auto neg1 =
      IfThenElseZero(Gt(int_f, v), Set(df, ConvertScalarTo<T>(-1.0)));

  return IfThenElse(detail::UseInt(v), Add(int_f, neg1), v);
}

#endif  // (HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL >= 1400) ||
        // (HWY_COMPILER_CLANG && HWY_COMPILER_CLANG >= 1700)

// ------------------------------ Floating-point classification (Ne)

// vfclass does not help because it would require 3 instructions (to AND and
// then compare the bits), whereas these are just 1-3 integer instructions.

template <class V>
HWY_API MFromD<DFromV<V>> IsNaN(const V v) {
  return Ne(v, v);
}

// Per-target flag to prevent generic_ops-inl.h from defining IsInf / IsFinite.
// We use a fused Set/comparison for IsFinite.
#ifdef HWY_NATIVE_ISINF
#undef HWY_NATIVE_ISINF
#else
#define HWY_NATIVE_ISINF
#endif

template <class V, class D = DFromV<V>>
HWY_API MFromD<D> IsInf(const V v) {
  const D d;
  const RebindToSigned<decltype(d)> di;
  using T = TFromD<D>;
  const VFromD<decltype(di)> vi = BitCast(di, v);
  // 'Shift left' to clear the sign bit, check for exponent=max and mantissa=0.
  return RebindMask(d, detail::EqS(Add(vi, vi), hwy::MaxExponentTimes2<T>()));
}

// Returns whether normal/subnormal/zero.
template <class V, class D = DFromV<V>>
HWY_API MFromD<D> IsFinite(const V v) {
  const D d;
  const RebindToUnsigned<decltype(d)> du;
  const RebindToSigned<decltype(d)> di;  // cheaper than unsigned comparison
  using T = TFromD<D>;
  const VFromD<decltype(du)> vu = BitCast(du, v);
  // 'Shift left' to clear the sign bit, then right so we can compare with the
  // max exponent (cannot compare with MaxExponentTimes2 directly because it is
  // negative and non-negative floats would be greater).
  const VFromD<decltype(di)> exp =
      BitCast(di, ShiftRight<hwy::MantissaBits<T>() + 1>(Add(vu, vu)));
  return RebindMask(d, detail::LtS(exp, hwy::MaxExponentField<T>()));
}

// ------------------------------ Iota (ConvertTo)

template <class D, typename T2, HWY_IF_UNSIGNED_D(D)>
HWY_API VFromD<D> Iota(const D d, T2 first) {
  return detail::AddS(detail::Iota0(d), static_cast<TFromD<D>>(first));
}

template <class D, typename T2, HWY_IF_SIGNED_D(D)>
HWY_API VFromD<D> Iota(const D d, T2 first) {
  const RebindToUnsigned<D> du;
  return detail::AddS(BitCast(d, detail::Iota0(du)),
                      static_cast<TFromD<D>>(first));
}

template <class D, typename T2, HWY_IF_FLOAT_D(D)>
HWY_API VFromD<D> Iota(const D d, T2 first) {
  const RebindToUnsigned<D> du;
  const RebindToSigned<D> di;
  return detail::AddS(ConvertTo(d, BitCast(di, detail::Iota0(du))),
                      ConvertScalarTo<TFromD<D>>(first));
}

// ------------------------------ BitShuffle (PromoteTo, Rol, SumsOf8)

// Native implementation required to avoid 8-bit wraparound on long vectors.
#ifdef HWY_NATIVE_BITSHUFFLE
#undef HWY_NATIVE_BITSHUFFLE
#else
#define HWY_NATIVE_BITSHUFFLE
#endif

// Cannot handle LMUL=8 because we promote indices.
template <class V64, class VI, HWY_IF_UI8(TFromV<VI>), class D64 = DFromV<V64>,
          HWY_IF_UI64_D(D64), HWY_IF_POW2_LE_D(D64, 2)>
HWY_API V64 BitShuffle(V64 values, VI idx) {
  const RebindToUnsigned<D64> du64;
  const Repartition<uint8_t, D64> du8;
  const Rebind<uint16_t, decltype(du8)> du16;
  using VU8 = VFromD<decltype(du8)>;
  using VU16 = VFromD<decltype(du16)>;
  // For each 16-bit (to avoid wraparound for long vectors) index of an output
  // byte: offset of the u64 lane to which it belongs.
  const VU16 byte_offsets =
      detail::AndS(detail::Iota0(du16), static_cast<uint16_t>(~7u));
  // idx is for a bit; shifting makes that bytes. Promote so we can add
  // byte_offsets, then we have the u8 lane index within the whole vector.
  const VU16 idx16 =
      Add(byte_offsets, PromoteTo(du16, ShiftRight<3>(BitCast(du8, idx))));
  const VU8 bytes = detail::TableLookupLanes16(BitCast(du8, values), idx16);

  // We want to shift right by idx & 7 to extract the desired bit in `bytes`,
  // and left by iota & 7 to put it in the correct output bit. To correctly
  // handle shift counts from -7 to 7, we rotate (unfortunately not natively
  // supported on RVV).
  const VU8 rotate_left_bits = Sub(detail::Iota0(du8), BitCast(du8, idx));
  const VU8 extracted_bits_mask =
      BitCast(du8, Set(du64, static_cast<uint64_t>(0x8040201008040201u)));
  const VU8 extracted_bits =
      And(Rol(bytes, rotate_left_bits), extracted_bits_mask);
  // Combine bit-sliced (one bit per byte) into one 64-bit sum.
  return BitCast(D64(), SumsOf8(extracted_bits));
}

template <class V64, class VI, HWY_IF_UI8(TFromV<VI>), class D64 = DFromV<V64>,
          HWY_IF_UI64_D(D64), HWY_IF_POW2_GT_D(D64, 2)>
HWY_API V64 BitShuffle(V64 values, VI idx) {
  const Half<D64> dh;
  const Half<DFromV<VI>> dih;
  using V64H = VFromD<decltype(dh)>;
  const V64H r0 = BitShuffle(LowerHalf(dh, values), LowerHalf(dih, idx));
  const V64H r1 = BitShuffle(UpperHalf(dh, values), UpperHalf(dih, idx));
  return Combine(D64(), r1, r0);
}

// ------------------------------ MulEven/Odd (Mul, OddEven)

template <class V, HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 1) | (1 << 2) | (1 << 4)),
          class D = DFromV<V>, class DW = RepartitionToWide<D>>
HWY_API VFromD<DW> MulEven(const V a, const V b) {
  const auto lo = Mul(a, b);
  const auto hi = MulHigh(a, b);
  return BitCast(DW(), OddEven(detail::Slide1Up(hi), lo));
}

template <class V, HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 1) | (1 << 2) | (1 << 4)),
          class D = DFromV<V>, class DW = RepartitionToWide<D>>
HWY_API VFromD<DW> MulOdd(const V a, const V b) {
  const auto lo = Mul(a, b);
  const auto hi = MulHigh(a, b);
  return BitCast(DW(), OddEven(hi, detail::Slide1Down(lo)));
}

// There is no 64x64 vwmul.
template <class V, HWY_IF_T_SIZE_V(V, 8)>
HWY_INLINE V MulEven(const V a, const V b) {
  const auto lo = Mul(a, b);
  const auto hi = MulHigh(a, b);
  return OddEven(detail::Slide1Up(hi), lo);
}

template <class V, HWY_IF_T_SIZE_V(V, 8)>
HWY_INLINE V MulOdd(const V a, const V b) {
  const auto lo = Mul(a, b);
  const auto hi = MulHigh(a, b);
  return OddEven(hi, detail::Slide1Down(lo));
}

// ------------------------------ ReorderDemote2To (OddEven, Combine)

template <class D, HWY_IF_BF16_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dbf16, VFromD<RepartitionToWide<D>> a,
                                   VFromD<RepartitionToWide<D>> b) {
  const RebindToUnsigned<decltype(dbf16)> du16;
  const Half<decltype(du16)> du16_half;
  const RebindToUnsigned<DFromV<decltype(a)>> du32;
  const VFromD<decltype(du32)> a_in_even = PromoteTo(
      du32, detail::DemoteTo16NearestEven(du16_half, BitCast(du32, a)));
  const VFromD<decltype(du32)> b_in_even = PromoteTo(
      du32, detail::DemoteTo16NearestEven(du16_half, BitCast(du32, b)));
  // Equivalent to InterleaveEven, but because the upper 16 bits are zero, we
  // can OR instead of OddEven.
  const VFromD<decltype(du16)> a_in_odd =
      detail::Slide1Up(BitCast(du16, a_in_even));
  return BitCast(dbf16, Or(a_in_odd, BitCast(du16, b_in_even)));
}

// If LMUL is not the max, Combine first to avoid another DemoteTo.
template <class DN, HWY_IF_NOT_FLOAT_NOR_SPECIAL(TFromD<DN>),
          HWY_IF_POW2_LE_D(DN, 2), class V, HWY_IF_SIGNED_V(V),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2),
          class V2 = VFromD<Repartition<TFromV<V>, DN>>,
          hwy::EnableIf<DFromV<V>().Pow2() == DFromV<V2>().Pow2()>* = nullptr>
HWY_API VFromD<DN> ReorderDemote2To(DN dn, V a, V b) {
  const Rebind<TFromV<V>, DN> dt;
  const VFromD<decltype(dt)> ab = Combine(dt, b, a);
  return DemoteTo(dn, ab);
}

template <class DN, HWY_IF_UNSIGNED_D(DN), HWY_IF_POW2_LE_D(DN, 2), class V,
          HWY_IF_UNSIGNED_V(V), HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2),
          class V2 = VFromD<Repartition<TFromV<V>, DN>>,
          hwy::EnableIf<DFromV<V>().Pow2() == DFromV<V2>().Pow2()>* = nullptr>
HWY_API VFromD<DN> ReorderDemote2To(DN dn, V a, V b) {
  const Rebind<TFromV<V>, DN> dt;
  const VFromD<decltype(dt)> ab = Combine(dt, b, a);
  return DemoteTo(dn, ab);
}

// Max LMUL: must DemoteTo first, then Combine.
template <class DN, HWY_IF_NOT_FLOAT_NOR_SPECIAL(TFromD<DN>),
          HWY_IF_POW2_GT_D(DN, 2), class V, HWY_IF_SIGNED_V(V),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2),
          class V2 = VFromD<Repartition<TFromV<V>, DN>>,
          hwy::EnableIf<DFromV<V>().Pow2() == DFromV<V2>().Pow2()>* = nullptr>
HWY_API VFromD<DN> ReorderDemote2To(DN dn, V a, V b) {
  const Half<decltype(dn)> dnh;
  const VFromD<decltype(dnh)> demoted_a = DemoteTo(dnh, a);
  const VFromD<decltype(dnh)> demoted_b = DemoteTo(dnh, b);
  return Combine(dn, demoted_b, demoted_a);
}

template <class DN, HWY_IF_UNSIGNED_D(DN), HWY_IF_POW2_GT_D(DN, 2), class V,
          HWY_IF_UNSIGNED_V(V), HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2),
          class V2 = VFromD<Repartition<TFromV<V>, DN>>,
          hwy::EnableIf<DFromV<V>().Pow2() == DFromV<V2>().Pow2()>* = nullptr>
HWY_API VFromD<DN> ReorderDemote2To(DN dn, V a, V b) {
  const Half<decltype(dn)> dnh;
  const VFromD<decltype(dnh)> demoted_a = DemoteTo(dnh, a);
  const VFromD<decltype(dnh)> demoted_b = DemoteTo(dnh, b);
  return Combine(dn, demoted_b, demoted_a);
}

// If LMUL is not the max, Combine first to avoid another DemoteTo.
template <class DN, HWY_IF_SPECIAL_FLOAT_D(DN), HWY_IF_POW2_LE_D(DN, 2),
          class V, HWY_IF_F32_D(DFromV<V>),
          class V2 = VFromD<Repartition<TFromV<V>, DN>>,
          hwy::EnableIf<DFromV<V>().Pow2() == DFromV<V2>().Pow2()>* = nullptr>
HWY_API VFromD<DN> OrderedDemote2To(DN dn, V a, V b) {
  const Rebind<TFromV<V>, DN> dt;
  const VFromD<decltype(dt)> ab = Combine(dt, b, a);
  return DemoteTo(dn, ab);
}

// Max LMUL: must DemoteTo first, then Combine.
template <class DN, HWY_IF_SPECIAL_FLOAT_D(DN), HWY_IF_POW2_GT_D(DN, 2),
          class V, HWY_IF_F32_D(DFromV<V>),
          class V2 = VFromD<Repartition<TFromV<V>, DN>>,
          hwy::EnableIf<DFromV<V>().Pow2() == DFromV<V2>().Pow2()>* = nullptr>
HWY_API VFromD<DN> OrderedDemote2To(DN dn, V a, V b) {
  const Half<decltype(dn)> dnh;
  const RebindToUnsigned<decltype(dn)> dn_u;
  const RebindToUnsigned<decltype(dnh)> dnh_u;
  const auto demoted_a = BitCast(dnh_u, DemoteTo(dnh, a));
  const auto demoted_b = BitCast(dnh_u, DemoteTo(dnh, b));
  return BitCast(dn, Combine(dn_u, demoted_b, demoted_a));
}

template <class DN, HWY_IF_NOT_FLOAT_NOR_SPECIAL(TFromD<DN>), class V,
          HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2),
          class V2 = VFromD<Repartition<TFromV<V>, DN>>,
          hwy::EnableIf<DFromV<V>().Pow2() == DFromV<V2>().Pow2()>* = nullptr>
HWY_API VFromD<DN> OrderedDemote2To(DN dn, V a, V b) {
  return ReorderDemote2To(dn, a, b);
}

// ------------------------------ WidenMulPairwiseAdd

template <class DF, HWY_IF_F32_D(DF),
          class VBF = VFromD<Repartition<hwy::bfloat16_t, DF>>>
HWY_API VFromD<DF> WidenMulPairwiseAdd(DF df, VBF a, VBF b) {
  const VFromD<DF> ae = PromoteEvenTo(df, a);
  const VFromD<DF> be = PromoteEvenTo(df, b);
  const VFromD<DF> ao = PromoteOddTo(df, a);
  const VFromD<DF> bo = PromoteOddTo(df, b);
  return MulAdd(ae, be, Mul(ao, bo));
}

template <class D, HWY_IF_UI32_D(D), class V16 = VFromD<RepartitionToNarrow<D>>>
HWY_API VFromD<D> WidenMulPairwiseAdd(D d32, V16 a, V16 b) {
  return MulAdd(PromoteEvenTo(d32, a), PromoteEvenTo(d32, b),
                Mul(PromoteOddTo(d32, a), PromoteOddTo(d32, b)));
}

// ------------------------------ ReorderWidenMulAccumulate (MulAdd, ZipLower)

namespace detail {

#define HWY_RVV_WIDEN_MACC(BASE, CHAR, SEW, SEWD, SEWH, LMUL, LMULD, LMULH,    \
                           SHIFT, MLEN, NAME, OP)                              \
  template <size_t N>                                                          \
  HWY_API HWY_RVV_V(BASE, SEWD, LMULD) NAME(                                   \
      HWY_RVV_D(BASE, SEWD, N, SHIFT + 1) d, HWY_RVV_V(BASE, SEWD, LMULD) sum, \
      HWY_RVV_V(BASE, SEW, LMUL) a, HWY_RVV_V(BASE, SEW, LMUL) b) {            \
    return __riscv_v##OP##CHAR##SEWD##LMULD(sum, a, b, Lanes(d));              \
  }

HWY_RVV_FOREACH_I16(HWY_RVV_WIDEN_MACC, WidenMulAcc, wmacc_vv_, _EXT_VIRT)
HWY_RVV_FOREACH_U16(HWY_RVV_WIDEN_MACC, WidenMulAcc, wmaccu_vv_, _EXT_VIRT)
#undef HWY_RVV_WIDEN_MACC

// If LMUL is not the max, we can WidenMul first (3 instructions).
template <class D32, HWY_IF_POW2_LE_D(D32, 2), class V32 = VFromD<D32>,
          class D16 = RepartitionToNarrow<D32>>
HWY_API VFromD<D32> ReorderWidenMulAccumulateI16(D32 d32, VFromD<D16> a,
                                                 VFromD<D16> b, const V32 sum0,
                                                 V32& sum1) {
  const Twice<decltype(d32)> d32t;
  using V32T = VFromD<decltype(d32t)>;
  V32T sum = Combine(d32t, sum1, sum0);
  sum = detail::WidenMulAcc(d32t, sum, a, b);
  sum1 = UpperHalf(d32, sum);
  return LowerHalf(d32, sum);
}

// Max LMUL: must LowerHalf first (4 instructions).
template <class D32, HWY_IF_POW2_GT_D(D32, 2), class V32 = VFromD<D32>,
          class D16 = RepartitionToNarrow<D32>>
HWY_API VFromD<D32> ReorderWidenMulAccumulateI16(D32 d32, VFromD<D16> a,
                                                 VFromD<D16> b, const V32 sum0,
                                                 V32& sum1) {
  const Half<D16> d16h;
  using V16H = VFromD<decltype(d16h)>;
  const V16H a0 = LowerHalf(d16h, a);
  const V16H a1 = UpperHalf(d16h, a);
  const V16H b0 = LowerHalf(d16h, b);
  const V16H b1 = UpperHalf(d16h, b);
  sum1 = detail::WidenMulAcc(d32, sum1, a1, b1);
  return detail::WidenMulAcc(d32, sum0, a0, b0);
}

// If LMUL is not the max, we can WidenMul first (3 instructions).
template <class D32, HWY_IF_POW2_LE_D(D32, 2), class V32 = VFromD<D32>,
          class D16 = RepartitionToNarrow<D32>>
HWY_API VFromD<D32> ReorderWidenMulAccumulateU16(D32 d32, VFromD<D16> a,
                                                 VFromD<D16> b, const V32 sum0,
                                                 V32& sum1) {
  const Twice<decltype(d32)> d32t;
  using V32T = VFromD<decltype(d32t)>;
  V32T sum = Combine(d32t, sum1, sum0);
  sum = detail::WidenMulAcc(d32t, sum, a, b);
  sum1 = UpperHalf(d32, sum);
  return LowerHalf(d32, sum);
}

// Max LMUL: must LowerHalf first (4 instructions).
template <class D32, HWY_IF_POW2_GT_D(D32, 2), class V32 = VFromD<D32>,
          class D16 = RepartitionToNarrow<D32>>
HWY_API VFromD<D32> ReorderWidenMulAccumulateU16(D32 d32, VFromD<D16> a,
                                                 VFromD<D16> b, const V32 sum0,
                                                 V32& sum1) {
  const Half<D16> d16h;
  using V16H = VFromD<decltype(d16h)>;
  const V16H a0 = LowerHalf(d16h, a);
  const V16H a1 = UpperHalf(d16h, a);
  const V16H b0 = LowerHalf(d16h, b);
  const V16H b1 = UpperHalf(d16h, b);
  sum1 = detail::WidenMulAcc(d32, sum1, a1, b1);
  return detail::WidenMulAcc(d32, sum0, a0, b0);
}

}  // namespace detail

template <class D, HWY_IF_I32_D(D), class VN, class VW>
HWY_API VW ReorderWidenMulAccumulate(D d32, VN a, VN b, const VW sum0,
                                     VW& sum1) {
  return detail::ReorderWidenMulAccumulateI16(d32, a, b, sum0, sum1);
}

template <class D, HWY_IF_U32_D(D), class VN, class VW>
HWY_API VW ReorderWidenMulAccumulate(D d32, VN a, VN b, const VW sum0,
                                     VW& sum1) {
  return detail::ReorderWidenMulAccumulateU16(d32, a, b, sum0, sum1);
}

// ------------------------------ RearrangeToOddPlusEven

template <class VW, HWY_IF_SIGNED_V(VW)>  // vint32_t*
HWY_API VW RearrangeToOddPlusEven(const VW sum0, const VW sum1) {
  // vwmacc doubles LMUL, so we require a pairwise sum here. This op is
  // expected to be less frequent than ReorderWidenMulAccumulate, hence it's
  // preferable to do the extra work here rather than do manual odd/even
  // extraction there.
  const DFromV<VW> di32;
  const RebindToUnsigned<decltype(di32)> du32;
  const Twice<decltype(di32)> di32x2;
  const RepartitionToWide<decltype(di32x2)> di64x2;
  const RebindToUnsigned<decltype(di64x2)> du64x2;
  const auto combined = BitCast(di64x2, Combine(di32x2, sum1, sum0));
  // Isolate odd/even int32 in int64 lanes.
  const auto even = ShiftRight<32>(ShiftLeft<32>(combined));  // sign extend
  const auto odd = ShiftRight<32>(combined);
  return BitCast(di32, TruncateTo(du32, BitCast(du64x2, Add(even, odd))));
}

// For max LMUL, we cannot Combine again and instead manually unroll.
HWY_API vint32m8_t RearrangeToOddPlusEven(vint32m8_t sum0, vint32m8_t sum1) {
  const DFromV<vint32m8_t> d;
  const Half<decltype(d)> dh;
  const vint32m4_t lo =
      RearrangeToOddPlusEven(LowerHalf(sum0), UpperHalf(dh, sum0));
  const vint32m4_t hi =
      RearrangeToOddPlusEven(LowerHalf(sum1), UpperHalf(dh, sum1));
  return Combine(d, hi, lo);
}

template <class VW, HWY_IF_UNSIGNED_V(VW)>  // vuint32_t*
HWY_API VW RearrangeToOddPlusEven(const VW sum0, const VW sum1) {
  // vwmacc doubles LMUL, so we require a pairwise sum here. This op is
  // expected to be less frequent than ReorderWidenMulAccumulate, hence it's
  // preferable to do the extra work here rather than do manual odd/even
  // extraction there.
  const DFromV<VW> du32;
  const Twice<decltype(du32)> du32x2;
  const RepartitionToWide<decltype(du32x2)> du64x2;
  const auto combined = BitCast(du64x2, Combine(du32x2, sum1, sum0));
  // Isolate odd/even int32 in int64 lanes.
  const auto even = detail::AndS(combined, uint64_t{0xFFFFFFFFu});
  const auto odd = ShiftRight<32>(combined);
  return TruncateTo(du32, Add(even, odd));
}

// For max LMUL, we cannot Combine again and instead manually unroll.
HWY_API vuint32m8_t RearrangeToOddPlusEven(vuint32m8_t sum0, vuint32m8_t sum1) {
  const DFromV<vuint32m8_t> d;
  const Half<decltype(d)> dh;
  const vuint32m4_t lo =
      RearrangeToOddPlusEven(LowerHalf(sum0), UpperHalf(dh, sum0));
  const vuint32m4_t hi =
      RearrangeToOddPlusEven(LowerHalf(sum1), UpperHalf(dh, sum1));
  return Combine(d, hi, lo);
}

template <class VW, HWY_IF_FLOAT_V(VW)>  // vfloat*
HWY_API VW RearrangeToOddPlusEven(const VW sum0, const VW sum1) {
  return Add(sum0, sum1);  // invariant already holds
}

// ------------------------------ Lt128
#if HWY_COMPILER_CLANG >= 1700 || HWY_COMPILER_GCC_ACTUAL >= 1400

template <class D>
HWY_INLINE MFromD<D> Lt128(D d, const VFromD<D> a, const VFromD<D> b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  // The subsequent computations are performed using e8mf8 (8-bit elements with
  // a fractional LMUL of 1/8) for the following reasons:
  // 1. It is correct for the possible input vector types e64m<1,2,4,8>. This is
  //    because the resulting mask can occupy at most 1/8 of a full vector when
  //    using e64m8.
  // 2. It can be more efficient than using a full vector or a vector group.
  //
  // The algorithm computes the result as follows:
  // 1. Compute cH | (=H & cL) in the high bits, where cH and cL represent the
  //    comparison results for the high and low 64-bit elements, respectively.
  // 2. Shift the result right by 1 to duplicate the comparison results for the
  //    low bits.
  // 3. Obtain the final result by performing a bitwise OR on the high and low
  //    bits.
  auto du8mf8 = ScalableTag<uint8_t, -3>{};
  const vuint8mf8_t ltHL0 =
      detail::ChangeLMUL(du8mf8, detail::MaskToU8MaskBitsVec(Lt(a, b)));
  const vuint8mf8_t eqHL0 =
      detail::ChangeLMUL(du8mf8, detail::MaskToU8MaskBitsVec(Eq(a, b)));
  const vuint8mf8_t ltLx0 = Add(ltHL0, ltHL0);
  const vuint8mf8_t resultHx = detail::AndS(OrAnd(ltHL0, ltLx0, eqHL0), 0xaa);
  const vuint8mf8_t resultxL = ShiftRight<1>(resultHx);
  const vuint8mf8_t result = Or(resultHx, resultxL);
  auto du8m1 = ScalableTag<uint8_t>{};
  return detail::U8MaskBitsVecToMask(d, detail::ChangeLMUL(du8m1, result));
}

#else

template <class D>
HWY_INLINE MFromD<D> Lt128(D d, const VFromD<D> a, const VFromD<D> b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  // Truth table of Eq and Compare for Hi and Lo u64.
  // (removed lines with (=H && cH) or (=L && cL) - cannot both be true)
  // =H =L cH cL  | out = cH | (=H & cL)
  //  0  0  0  0  |  0
  //  0  0  0  1  |  0
  //  0  0  1  0  |  1
  //  0  0  1  1  |  1
  //  0  1  0  0  |  0
  //  0  1  0  1  |  0
  //  0  1  1  0  |  1
  //  1  0  0  0  |  0
  //  1  0  0  1  |  1
  //  1  1  0  0  |  0
  const VFromD<D> eqHL = VecFromMask(d, Eq(a, b));
  const VFromD<D> ltHL = VecFromMask(d, Lt(a, b));
  // Shift leftward so L can influence H.
  const VFromD<D> ltLx = detail::Slide1Up(ltHL);
  const VFromD<D> vecHx = OrAnd(ltHL, eqHL, ltLx);
  // Replicate H to its neighbor.
  return MaskFromVec(OddEven(vecHx, detail::Slide1Down(vecHx)));
}

#endif  // HWY_COMPILER_CLANG >= 1700 || HWY_COMPILER_GCC_ACTUAL >= 1400

// ------------------------------ Lt128Upper
#if HWY_COMPILER_CLANG >= 1700 || HWY_COMPILER_GCC_ACTUAL >= 1400

template <class D>
HWY_INLINE MFromD<D> Lt128Upper(D d, const VFromD<D> a, const VFromD<D> b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  auto du8mf8 = ScalableTag<uint8_t, -3>{};
  const vuint8mf8_t ltHL =
      detail::ChangeLMUL(du8mf8, detail::MaskToU8MaskBitsVec(Lt(a, b)));
  const vuint8mf8_t ltHx = detail::AndS(ltHL, 0xaa);
  const vuint8mf8_t ltxL = ShiftRight<1>(ltHx);
  auto du8m1 = ScalableTag<uint8_t>{};
  return detail::U8MaskBitsVecToMask(d,
                                     detail::ChangeLMUL(du8m1, Or(ltHx, ltxL)));
}

#else

template <class D>
HWY_INLINE MFromD<D> Lt128Upper(D d, const VFromD<D> a, const VFromD<D> b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  const VFromD<D> ltHL = VecFromMask(d, Lt(a, b));
  const VFromD<D> down = detail::Slide1Down(ltHL);
  // b(267743505): Clang compiler bug, workaround is DoNotOptimize
  asm volatile("" : : "r,m"(GetLane(down)) : "memory");
  // Replicate H to its neighbor.
  return MaskFromVec(OddEven(ltHL, down));
}

#endif  // HWY_COMPILER_CLANG >= 1700 || HWY_COMPILER_GCC_ACTUAL >= 1400

// ------------------------------ Eq128
#if HWY_COMPILER_CLANG >= 1700 || HWY_COMPILER_GCC_ACTUAL >= 1400

template <class D>
HWY_INLINE MFromD<D> Eq128(D d, const VFromD<D> a, const VFromD<D> b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  auto du8mf8 = ScalableTag<uint8_t, -3>{};
  const vuint8mf8_t eqHL =
      detail::ChangeLMUL(du8mf8, detail::MaskToU8MaskBitsVec(Eq(a, b)));
  const vuint8mf8_t eqxH = ShiftRight<1>(eqHL);
  const vuint8mf8_t result0L = detail::AndS(And(eqHL, eqxH), 0x55);
  const vuint8mf8_t resultH0 = Add(result0L, result0L);
  auto du8m1 = ScalableTag<uint8_t>{};
  return detail::U8MaskBitsVecToMask(
      d, detail::ChangeLMUL(du8m1, Or(result0L, resultH0)));
}

#else

template <class D>
HWY_INLINE MFromD<D> Eq128(D d, const VFromD<D> a, const VFromD<D> b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  const VFromD<D> eqHL = VecFromMask(d, Eq(a, b));
  const VFromD<D> eqLH = Reverse2(d, eqHL);
  const VFromD<D> eq = And(eqHL, eqLH);
  // b(267743505): Clang compiler bug, workaround is DoNotOptimize
  asm volatile("" : : "r,m"(GetLane(eq)) : "memory");
  return MaskFromVec(eq);
}

#endif

// ------------------------------ Eq128Upper
#if HWY_COMPILER_CLANG >= 1700 || HWY_COMPILER_GCC_ACTUAL >= 1400

template <class D>
HWY_INLINE MFromD<D> Eq128Upper(D d, const VFromD<D> a, const VFromD<D> b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  auto du8mf8 = ScalableTag<uint8_t, -3>{};
  const vuint8mf8_t eqHL =
      detail::ChangeLMUL(du8mf8, detail::MaskToU8MaskBitsVec(Eq(a, b)));
  const vuint8mf8_t eqHx = detail::AndS(eqHL, 0xaa);
  const vuint8mf8_t eqxL = ShiftRight<1>(eqHx);
  auto du8m1 = ScalableTag<uint8_t>{};
  return detail::U8MaskBitsVecToMask(d,
                                     detail::ChangeLMUL(du8m1, Or(eqHx, eqxL)));
}

#else

template <class D>
HWY_INLINE MFromD<D> Eq128Upper(D d, const VFromD<D> a, const VFromD<D> b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  const VFromD<D> eqHL = VecFromMask(d, Eq(a, b));
  // Replicate H to its neighbor.
  return MaskFromVec(OddEven(eqHL, detail::Slide1Down(eqHL)));
}

#endif

// ------------------------------ Ne128
#if HWY_COMPILER_CLANG >= 1700 || HWY_COMPILER_GCC_ACTUAL >= 1400

template <class D>
HWY_INLINE MFromD<D> Ne128(D d, const VFromD<D> a, const VFromD<D> b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  auto du8mf8 = ScalableTag<uint8_t, -3>{};
  const vuint8mf8_t neHL =
      detail::ChangeLMUL(du8mf8, detail::MaskToU8MaskBitsVec(Ne(a, b)));
  const vuint8mf8_t nexH = ShiftRight<1>(neHL);
  const vuint8mf8_t result0L = detail::AndS(Or(neHL, nexH), 0x55);
  const vuint8mf8_t resultH0 = Add(result0L, result0L);
  auto du8m1 = ScalableTag<uint8_t>{};
  return detail::U8MaskBitsVecToMask(
      d, detail::ChangeLMUL(du8m1, Or(result0L, resultH0)));
}

#else

template <class D>
HWY_INLINE MFromD<D> Ne128(D d, const VFromD<D> a, const VFromD<D> b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  const VFromD<D> neHL = VecFromMask(d, Ne(a, b));
  const VFromD<D> neLH = Reverse2(d, neHL);
  // b(267743505): Clang compiler bug, workaround is DoNotOptimize
  asm volatile("" : : "r,m"(GetLane(neLH)) : "memory");
  return MaskFromVec(Or(neHL, neLH));
}

#endif

// ------------------------------ Ne128Upper
#if HWY_COMPILER_CLANG >= 1700 || HWY_COMPILER_GCC_ACTUAL >= 1400

template <class D>
HWY_INLINE MFromD<D> Ne128Upper(D d, const VFromD<D> a, const VFromD<D> b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  auto du8mf8 = ScalableTag<uint8_t, -3>{};
  const vuint8mf8_t neHL =
      detail::ChangeLMUL(du8mf8, detail::MaskToU8MaskBitsVec(Ne(a, b)));
  const vuint8mf8_t neHx = detail::AndS(neHL, 0xaa);
  const vuint8mf8_t nexL = ShiftRight<1>(neHx);
  auto du8m1 = ScalableTag<uint8_t>{};
  return detail::U8MaskBitsVecToMask(d,
                                     detail::ChangeLMUL(du8m1, Or(neHx, nexL)));
}

#else

template <class D>
HWY_INLINE MFromD<D> Ne128Upper(D d, const VFromD<D> a, const VFromD<D> b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  const VFromD<D> neHL = VecFromMask(d, Ne(a, b));
  const VFromD<D> down = detail::Slide1Down(neHL);
  // b(267743505): Clang compiler bug, workaround is DoNotOptimize
  asm volatile("" : : "r,m"(GetLane(down)) : "memory");
  // Replicate H to its neighbor.
  return MaskFromVec(OddEven(neHL, down));
}

#endif

// ------------------------------ Min128, Max128 (Lt128)

template <class D>
HWY_INLINE VFromD<D> Min128(D /* tag */, const VFromD<D> a, const VFromD<D> b) {
  const VFromD<D> aXH = detail::Slide1Down(a);
  const VFromD<D> bXH = detail::Slide1Down(b);
  const VFromD<D> minHL = Min(a, b);
  const MFromD<D> ltXH = Lt(aXH, bXH);
  const MFromD<D> eqXH = Eq(aXH, bXH);
  // If the upper lane is the decider, take lo from the same reg.
  const VFromD<D> lo = IfThenElse(ltXH, a, b);
  // The upper lane is just minHL; if they are equal, we also need to use the
  // actual min of the lower lanes.
  return OddEven(minHL, IfThenElse(eqXH, minHL, lo));
}

template <class D>
HWY_INLINE VFromD<D> Max128(D /* tag */, const VFromD<D> a, const VFromD<D> b) {
  const VFromD<D> aXH = detail::Slide1Down(a);
  const VFromD<D> bXH = detail::Slide1Down(b);
  const VFromD<D> maxHL = Max(a, b);
  const MFromD<D> ltXH = Lt(aXH, bXH);
  const MFromD<D> eqXH = Eq(aXH, bXH);
  // If the upper lane is the decider, take lo from the same reg.
  const VFromD<D> lo = IfThenElse(ltXH, b, a);
  // The upper lane is just maxHL; if they are equal, we also need to use the
  // actual min of the lower lanes.
  return OddEven(maxHL, IfThenElse(eqXH, maxHL, lo));
}

template <class D>
HWY_INLINE VFromD<D> Min128Upper(D d, VFromD<D> a, VFromD<D> b) {
  return IfThenElse(Lt128Upper(d, a, b), a, b);
}

template <class D>
HWY_INLINE VFromD<D> Max128Upper(D d, VFromD<D> a, VFromD<D> b) {
  return IfThenElse(Lt128Upper(d, b, a), a, b);
}

// ================================================== END MACROS
#undef HWY_RVV_AVL
#undef HWY_RVV_D
#undef HWY_RVV_FOREACH
#undef HWY_RVV_FOREACH_08_ALL
#undef HWY_RVV_FOREACH_08_ALL_VIRT
#undef HWY_RVV_FOREACH_08_DEMOTE
#undef HWY_RVV_FOREACH_08_DEMOTE_VIRT
#undef HWY_RVV_FOREACH_08_EXT
#undef HWY_RVV_FOREACH_08_EXT_VIRT
#undef HWY_RVV_FOREACH_08_TRUNC
#undef HWY_RVV_FOREACH_08_VIRT
#undef HWY_RVV_FOREACH_16_ALL
#undef HWY_RVV_FOREACH_16_ALL_VIRT
#undef HWY_RVV_FOREACH_16_DEMOTE
#undef HWY_RVV_FOREACH_16_DEMOTE_VIRT
#undef HWY_RVV_FOREACH_16_EXT
#undef HWY_RVV_FOREACH_16_EXT_VIRT
#undef HWY_RVV_FOREACH_16_TRUNC
#undef HWY_RVV_FOREACH_16_VIRT
#undef HWY_RVV_FOREACH_32_ALL
#undef HWY_RVV_FOREACH_32_ALL_VIRT
#undef HWY_RVV_FOREACH_32_DEMOTE
#undef HWY_RVV_FOREACH_32_DEMOTE_VIRT
#undef HWY_RVV_FOREACH_32_EXT
#undef HWY_RVV_FOREACH_32_EXT_VIRT
#undef HWY_RVV_FOREACH_32_TRUNC
#undef HWY_RVV_FOREACH_32_VIRT
#undef HWY_RVV_FOREACH_64_ALL
#undef HWY_RVV_FOREACH_64_ALL_VIRT
#undef HWY_RVV_FOREACH_64_DEMOTE
#undef HWY_RVV_FOREACH_64_DEMOTE_VIRT
#undef HWY_RVV_FOREACH_64_EXT
#undef HWY_RVV_FOREACH_64_EXT_VIRT
#undef HWY_RVV_FOREACH_64_TRUNC
#undef HWY_RVV_FOREACH_64_VIRT
#undef HWY_RVV_FOREACH_B
#undef HWY_RVV_FOREACH_F
#undef HWY_RVV_FOREACH_F16
#undef HWY_RVV_FOREACH_F32
#undef HWY_RVV_FOREACH_F3264
#undef HWY_RVV_FOREACH_F64
#undef HWY_RVV_FOREACH_I
#undef HWY_RVV_FOREACH_I08
#undef HWY_RVV_FOREACH_I16
#undef HWY_RVV_FOREACH_I163264
#undef HWY_RVV_FOREACH_I32
#undef HWY_RVV_FOREACH_I64
#undef HWY_RVV_FOREACH_U
#undef HWY_RVV_FOREACH_U08
#undef HWY_RVV_FOREACH_U16
#undef HWY_RVV_FOREACH_U163264
#undef HWY_RVV_FOREACH_U32
#undef HWY_RVV_FOREACH_U64
#undef HWY_RVV_FOREACH_UI
#undef HWY_RVV_FOREACH_UI08
#undef HWY_RVV_FOREACH_UI16
#undef HWY_RVV_FOREACH_UI163264
#undef HWY_RVV_FOREACH_UI32
#undef HWY_RVV_FOREACH_UI3264
#undef HWY_RVV_FOREACH_UI64
#undef HWY_RVV_IF_EMULATED_D
#undef HWY_RVV_IF_CAN128_D
#undef HWY_RVV_IF_GE128_D
#undef HWY_RVV_IF_LT128_D
#undef HWY_RVV_INSERT_VXRM
#undef HWY_RVV_M
#undef HWY_RVV_RETM_ARGM
#undef HWY_RVV_RETV_ARGMVV
#undef HWY_RVV_RETV_ARGV
#undef HWY_RVV_RETV_ARGVS
#undef HWY_RVV_RETV_ARGVV
#undef HWY_RVV_T
#undef HWY_RVV_V
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();
