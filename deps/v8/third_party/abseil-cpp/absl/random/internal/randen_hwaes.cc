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

// HERMETIC NOTE: The randen_hwaes target must not introduce duplicate
// symbols from arbitrary system and other headers, since it may be built
// with different flags from other targets, using different levels of
// optimization, potentially introducing ODR violations.

#include "absl/random/internal/randen_hwaes.h"

#include <cstdint>
#include <cstring>

#include "absl/base/attributes.h"
#include "absl/numeric/int128.h"
#include "absl/random/internal/platform.h"
#include "absl/random/internal/randen_traits.h"

// ABSL_RANDEN_HWAES_IMPL indicates whether this file will contain
// a hardware accelerated implementation of randen, or whether it
// will contain stubs that exit the process.
#if ABSL_HAVE_ACCELERATED_AES
// The following platforms have implemented RandenHwAes.
#if defined(ABSL_ARCH_X86_64) || defined(ABSL_ARCH_X86_32) || \
    defined(ABSL_ARCH_PPC) || defined(ABSL_ARCH_ARM) ||       \
    defined(ABSL_ARCH_AARCH64)
#define ABSL_RANDEN_HWAES_IMPL 1
#endif
#endif

#if !defined(ABSL_RANDEN_HWAES_IMPL)
// No accelerated implementation is supported.
// The RandenHwAes functions are stubs that print an error and exit.

#include <cstdio>
#include <cstdlib>

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// No accelerated implementation.
bool HasRandenHwAesImplementation() { return false; }

// NOLINTNEXTLINE
const void* RandenHwAes::GetKeys() {
  // Attempted to dispatch to an unsupported dispatch target.
  const int d = ABSL_RANDOM_INTERNAL_AES_DISPATCH;
  fprintf(stderr, "AES Hardware detection failed (%d).\n", d);
  exit(1);
  return nullptr;
}

// NOLINTNEXTLINE
void RandenHwAes::Absorb(const void*, void*) {
  // Attempted to dispatch to an unsupported dispatch target.
  const int d = ABSL_RANDOM_INTERNAL_AES_DISPATCH;
  fprintf(stderr, "AES Hardware detection failed (%d).\n", d);
  exit(1);
}

// NOLINTNEXTLINE
void RandenHwAes::Generate(const void*, void*) {
  // Attempted to dispatch to an unsupported dispatch target.
  const int d = ABSL_RANDOM_INTERNAL_AES_DISPATCH;
  fprintf(stderr, "AES Hardware detection failed (%d).\n", d);
  exit(1);
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#else  // defined(ABSL_RANDEN_HWAES_IMPL)
//
// Accelerated implementations are supported.
// We need the per-architecture includes and defines.
//
namespace {

using absl::random_internal::RandenTraits;

}  // namespace

// TARGET_CRYPTO defines a crypto attribute for each architecture.
//
// NOTE: Evaluate whether we should eliminate ABSL_TARGET_CRYPTO.
#if (defined(__clang__) || defined(__GNUC__))
#if defined(ABSL_ARCH_X86_64) || defined(ABSL_ARCH_X86_32)
#define ABSL_TARGET_CRYPTO __attribute__((target("aes")))
#elif defined(ABSL_ARCH_PPC)
#define ABSL_TARGET_CRYPTO __attribute__((target("crypto")))
#else
#define ABSL_TARGET_CRYPTO
#endif
#else
#define ABSL_TARGET_CRYPTO
#endif

#if defined(ABSL_ARCH_PPC)
// NOTE: Keep in mind that PPC can operate in little-endian or big-endian mode,
// however the PPC altivec vector registers (and thus the AES instructions)
// always operate in big-endian mode.

#include <altivec.h>
// <altivec.h> #defines vector __vector; in C++, this is bad form.
#undef vector
#undef bool

// Rely on the PowerPC AltiVec vector operations for accelerated AES
// instructions. GCC support of the PPC vector types is described in:
// https://gcc.gnu.org/onlinedocs/gcc-4.9.0/gcc/PowerPC-AltiVec_002fVSX-Built-in-Functions.html
//
// Already provides operator^=.
using Vector128 = __vector unsigned long long;  // NOLINT(runtime/int)

namespace {
inline ABSL_TARGET_CRYPTO Vector128 ReverseBytes(const Vector128& v) {
  // Reverses the bytes of the vector.
  const __vector unsigned char perm = {15, 14, 13, 12, 11, 10, 9, 8,
                                       7,  6,  5,  4,  3,  2,  1, 0};
  return vec_perm(v, v, perm);
}

// WARNING: these load/store in native byte order. It is OK to load and then
// store an unchanged vector, but interpreting the bits as a number or input
// to AES will have undefined results.
inline ABSL_TARGET_CRYPTO Vector128 Vector128Load(const void* from) {
  return vec_vsx_ld(0, reinterpret_cast<const Vector128*>(from));
}

inline ABSL_TARGET_CRYPTO void Vector128Store(const Vector128& v, void* to) {
  vec_vsx_st(v, 0, reinterpret_cast<Vector128*>(to));
}

// One round of AES. "round_key" is a public constant for breaking the
// symmetry of AES (ensures previously equal columns differ afterwards).
inline ABSL_TARGET_CRYPTO Vector128 AesRound(const Vector128& state,
                                             const Vector128& round_key) {
  return Vector128(__builtin_crypto_vcipher(state, round_key));
}

// Enables native loads in the round loop by pre-swapping.
inline ABSL_TARGET_CRYPTO void SwapEndian(absl::uint128* state) {
  for (uint32_t block = 0; block < RandenTraits::kFeistelBlocks; ++block) {
    Vector128Store(ReverseBytes(Vector128Load(state + block)), state + block);
  }
}

}  // namespace

#elif defined(ABSL_ARCH_ARM) || defined(ABSL_ARCH_AARCH64)

// Rely on the ARM NEON+Crypto advanced simd types, defined in <arm_neon.h>.
// uint8x16_t is the user alias for underlying __simd128_uint8_t type.
// http://infocenter.arm.com/help/topic/com.arm.doc.ihi0073a/IHI0073A_arm_neon_intrinsics_ref.pdf
//
// <arm_neon> defines the following
//
// typedef __attribute__((neon_vector_type(16))) uint8_t uint8x16_t;
// typedef __attribute__((neon_vector_type(16))) int8_t int8x16_t;
// typedef __attribute__((neon_polyvector_type(16))) int8_t poly8x16_t;
//
// vld1q_v
// vst1q_v
// vaeseq_v
// vaesmcq_v
#include <arm_neon.h>

// Already provides operator^=.
using Vector128 = uint8x16_t;

namespace {

inline ABSL_TARGET_CRYPTO Vector128 Vector128Load(const void* from) {
  return vld1q_u8(reinterpret_cast<const uint8_t*>(from));
}

inline ABSL_TARGET_CRYPTO void Vector128Store(const Vector128& v, void* to) {
  vst1q_u8(reinterpret_cast<uint8_t*>(to), v);
}

// One round of AES. "round_key" is a public constant for breaking the
// symmetry of AES (ensures previously equal columns differ afterwards).
inline ABSL_TARGET_CRYPTO Vector128 AesRound(const Vector128& state,
                                             const Vector128& round_key) {
  // It is important to always use the full round function - omitting the
  // final MixColumns reduces security [https://eprint.iacr.org/2010/041.pdf]
  // and does not help because we never decrypt.
  //
  // Note that ARM divides AES instructions differently than x86 / PPC,
  // And we need to skip the first AddRoundKey step and add an extra
  // AddRoundKey step to the end. Lucky for us this is just XOR.
  return vaesmcq_u8(vaeseq_u8(state, uint8x16_t{})) ^ round_key;
}

inline ABSL_TARGET_CRYPTO void SwapEndian(void*) {}

}  // namespace

#elif defined(ABSL_ARCH_X86_64) || defined(ABSL_ARCH_X86_32)
// On x86 we rely on the aesni instructions
#include <immintrin.h>

namespace {

// Vector128 class is only wrapper for __m128i, benchmark indicates that it's
// faster than using __m128i directly.
class Vector128 {
 public:
  // Convert from/to intrinsics.
  inline explicit Vector128(const __m128i& v) : data_(v) {}

  inline __m128i data() const { return data_; }

  inline Vector128& operator^=(const Vector128& other) {
    data_ = _mm_xor_si128(data_, other.data());
    return *this;
  }

 private:
  __m128i data_;
};

inline ABSL_TARGET_CRYPTO Vector128 Vector128Load(const void* from) {
  return Vector128(_mm_load_si128(reinterpret_cast<const __m128i*>(from)));
}

inline ABSL_TARGET_CRYPTO void Vector128Store(const Vector128& v, void* to) {
  _mm_store_si128(reinterpret_cast<__m128i*>(to), v.data());
}

// One round of AES. "round_key" is a public constant for breaking the
// symmetry of AES (ensures previously equal columns differ afterwards).
inline ABSL_TARGET_CRYPTO Vector128 AesRound(const Vector128& state,
                                             const Vector128& round_key) {
  // It is important to always use the full round function - omitting the
  // final MixColumns reduces security [https://eprint.iacr.org/2010/041.pdf]
  // and does not help because we never decrypt.
  return Vector128(_mm_aesenc_si128(state.data(), round_key.data()));
}

inline ABSL_TARGET_CRYPTO void SwapEndian(void*) {}

}  // namespace

#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#endif

// At this point, all of the platform-specific features have been defined /
// implemented.
//
// REQUIRES: using Vector128 = ...
// REQUIRES: Vector128 Vector128Load(void*) {...}
// REQUIRES: void Vector128Store(Vector128, void*) {...}
// REQUIRES: Vector128 AesRound(Vector128, Vector128) {...}
// REQUIRES: void SwapEndian(uint64_t*) {...}
//
// PROVIDES: absl::random_internal::RandenHwAes::Absorb
// PROVIDES: absl::random_internal::RandenHwAes::Generate
namespace {

// Block shuffles applies a shuffle to the entire state between AES rounds.
// Improved odd-even shuffle from "New criterion for diffusion property".
inline ABSL_TARGET_CRYPTO void BlockShuffle(absl::uint128* state) {
  static_assert(RandenTraits::kFeistelBlocks == 16,
                "Expecting 16 FeistelBlocks.");

  constexpr size_t shuffle[RandenTraits::kFeistelBlocks] = {
      7, 2, 13, 4, 11, 8, 3, 6, 15, 0, 9, 10, 1, 14, 5, 12};

  const Vector128 v0 = Vector128Load(state + shuffle[0]);
  const Vector128 v1 = Vector128Load(state + shuffle[1]);
  const Vector128 v2 = Vector128Load(state + shuffle[2]);
  const Vector128 v3 = Vector128Load(state + shuffle[3]);
  const Vector128 v4 = Vector128Load(state + shuffle[4]);
  const Vector128 v5 = Vector128Load(state + shuffle[5]);
  const Vector128 v6 = Vector128Load(state + shuffle[6]);
  const Vector128 v7 = Vector128Load(state + shuffle[7]);
  const Vector128 w0 = Vector128Load(state + shuffle[8]);
  const Vector128 w1 = Vector128Load(state + shuffle[9]);
  const Vector128 w2 = Vector128Load(state + shuffle[10]);
  const Vector128 w3 = Vector128Load(state + shuffle[11]);
  const Vector128 w4 = Vector128Load(state + shuffle[12]);
  const Vector128 w5 = Vector128Load(state + shuffle[13]);
  const Vector128 w6 = Vector128Load(state + shuffle[14]);
  const Vector128 w7 = Vector128Load(state + shuffle[15]);

  Vector128Store(v0, state + 0);
  Vector128Store(v1, state + 1);
  Vector128Store(v2, state + 2);
  Vector128Store(v3, state + 3);
  Vector128Store(v4, state + 4);
  Vector128Store(v5, state + 5);
  Vector128Store(v6, state + 6);
  Vector128Store(v7, state + 7);
  Vector128Store(w0, state + 8);
  Vector128Store(w1, state + 9);
  Vector128Store(w2, state + 10);
  Vector128Store(w3, state + 11);
  Vector128Store(w4, state + 12);
  Vector128Store(w5, state + 13);
  Vector128Store(w6, state + 14);
  Vector128Store(w7, state + 15);
}

// Feistel round function using two AES subrounds. Very similar to F()
// from Simpira v2, but with independent subround keys. Uses 17 AES rounds
// per 16 bytes (vs. 10 for AES-CTR). Computing eight round functions in
// parallel hides the 7-cycle AESNI latency on HSW. Note that the Feistel
// XORs are 'free' (included in the second AES instruction).
inline ABSL_TARGET_CRYPTO const absl::uint128* FeistelRound(
    absl::uint128* state,
    const absl::uint128* ABSL_RANDOM_INTERNAL_RESTRICT keys) {
  static_assert(RandenTraits::kFeistelBlocks == 16,
                "Expecting 16 FeistelBlocks.");

  // MSVC does a horrible job at unrolling loops.
  // So we unroll the loop by hand to improve the performance.
  const Vector128 s0 = Vector128Load(state + 0);
  const Vector128 s1 = Vector128Load(state + 1);
  const Vector128 s2 = Vector128Load(state + 2);
  const Vector128 s3 = Vector128Load(state + 3);
  const Vector128 s4 = Vector128Load(state + 4);
  const Vector128 s5 = Vector128Load(state + 5);
  const Vector128 s6 = Vector128Load(state + 6);
  const Vector128 s7 = Vector128Load(state + 7);
  const Vector128 s8 = Vector128Load(state + 8);
  const Vector128 s9 = Vector128Load(state + 9);
  const Vector128 s10 = Vector128Load(state + 10);
  const Vector128 s11 = Vector128Load(state + 11);
  const Vector128 s12 = Vector128Load(state + 12);
  const Vector128 s13 = Vector128Load(state + 13);
  const Vector128 s14 = Vector128Load(state + 14);
  const Vector128 s15 = Vector128Load(state + 15);

  // Encode even blocks with keys.
  const Vector128 e0 = AesRound(s0, Vector128Load(keys + 0));
  const Vector128 e2 = AesRound(s2, Vector128Load(keys + 1));
  const Vector128 e4 = AesRound(s4, Vector128Load(keys + 2));
  const Vector128 e6 = AesRound(s6, Vector128Load(keys + 3));
  const Vector128 e8 = AesRound(s8, Vector128Load(keys + 4));
  const Vector128 e10 = AesRound(s10, Vector128Load(keys + 5));
  const Vector128 e12 = AesRound(s12, Vector128Load(keys + 6));
  const Vector128 e14 = AesRound(s14, Vector128Load(keys + 7));

  // Encode odd blocks with even output from above.
  const Vector128 o1 = AesRound(e0, s1);
  const Vector128 o3 = AesRound(e2, s3);
  const Vector128 o5 = AesRound(e4, s5);
  const Vector128 o7 = AesRound(e6, s7);
  const Vector128 o9 = AesRound(e8, s9);
  const Vector128 o11 = AesRound(e10, s11);
  const Vector128 o13 = AesRound(e12, s13);
  const Vector128 o15 = AesRound(e14, s15);

  // Store odd blocks. (These will be shuffled later).
  Vector128Store(o1, state + 1);
  Vector128Store(o3, state + 3);
  Vector128Store(o5, state + 5);
  Vector128Store(o7, state + 7);
  Vector128Store(o9, state + 9);
  Vector128Store(o11, state + 11);
  Vector128Store(o13, state + 13);
  Vector128Store(o15, state + 15);

  return keys + 8;
}

// Cryptographic permutation based via type-2 Generalized Feistel Network.
// Indistinguishable from ideal by chosen-ciphertext adversaries using less than
// 2^64 queries if the round function is a PRF. This is similar to the b=8 case
// of Simpira v2, but more efficient than its generic construction for b=16.
inline ABSL_TARGET_CRYPTO void Permute(
    absl::uint128* state,
    const absl::uint128* ABSL_RANDOM_INTERNAL_RESTRICT keys) {
  // (Successfully unrolled; the first iteration jumps into the second half)
#ifdef __clang__
#pragma clang loop unroll_count(2)
#endif
  for (size_t round = 0; round < RandenTraits::kFeistelRounds; ++round) {
    keys = FeistelRound(state, keys);
    BlockShuffle(state);
  }
}

}  // namespace

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

bool HasRandenHwAesImplementation() { return true; }

const void* ABSL_TARGET_CRYPTO RandenHwAes::GetKeys() {
  // Round keys for one AES per Feistel round and branch.
  // The canonical implementation uses first digits of Pi.
#if defined(ABSL_ARCH_PPC)
  return kRandenRoundKeysBE;
#else
  return kRandenRoundKeys;
#endif
}

// NOLINTNEXTLINE
void ABSL_TARGET_CRYPTO RandenHwAes::Absorb(const void* seed_void,
                                            void* state_void) {
  static_assert(RandenTraits::kCapacityBytes / sizeof(Vector128) == 1,
                "Unexpected Randen kCapacityBlocks");
  static_assert(RandenTraits::kStateBytes / sizeof(Vector128) == 16,
                "Unexpected Randen kStateBlocks");

  auto* state = reinterpret_cast<absl::uint128 * ABSL_RANDOM_INTERNAL_RESTRICT>(
      state_void);
  const auto* seed =
      reinterpret_cast<const absl::uint128 * ABSL_RANDOM_INTERNAL_RESTRICT>(
          seed_void);

  Vector128 b1 = Vector128Load(state + 1);
  b1 ^= Vector128Load(seed + 0);
  Vector128Store(b1, state + 1);

  Vector128 b2 = Vector128Load(state + 2);
  b2 ^= Vector128Load(seed + 1);
  Vector128Store(b2, state + 2);

  Vector128 b3 = Vector128Load(state + 3);
  b3 ^= Vector128Load(seed + 2);
  Vector128Store(b3, state + 3);

  Vector128 b4 = Vector128Load(state + 4);
  b4 ^= Vector128Load(seed + 3);
  Vector128Store(b4, state + 4);

  Vector128 b5 = Vector128Load(state + 5);
  b5 ^= Vector128Load(seed + 4);
  Vector128Store(b5, state + 5);

  Vector128 b6 = Vector128Load(state + 6);
  b6 ^= Vector128Load(seed + 5);
  Vector128Store(b6, state + 6);

  Vector128 b7 = Vector128Load(state + 7);
  b7 ^= Vector128Load(seed + 6);
  Vector128Store(b7, state + 7);

  Vector128 b8 = Vector128Load(state + 8);
  b8 ^= Vector128Load(seed + 7);
  Vector128Store(b8, state + 8);

  Vector128 b9 = Vector128Load(state + 9);
  b9 ^= Vector128Load(seed + 8);
  Vector128Store(b9, state + 9);

  Vector128 b10 = Vector128Load(state + 10);
  b10 ^= Vector128Load(seed + 9);
  Vector128Store(b10, state + 10);

  Vector128 b11 = Vector128Load(state + 11);
  b11 ^= Vector128Load(seed + 10);
  Vector128Store(b11, state + 11);

  Vector128 b12 = Vector128Load(state + 12);
  b12 ^= Vector128Load(seed + 11);
  Vector128Store(b12, state + 12);

  Vector128 b13 = Vector128Load(state + 13);
  b13 ^= Vector128Load(seed + 12);
  Vector128Store(b13, state + 13);

  Vector128 b14 = Vector128Load(state + 14);
  b14 ^= Vector128Load(seed + 13);
  Vector128Store(b14, state + 14);

  Vector128 b15 = Vector128Load(state + 15);
  b15 ^= Vector128Load(seed + 14);
  Vector128Store(b15, state + 15);
}

// NOLINTNEXTLINE
void ABSL_TARGET_CRYPTO RandenHwAes::Generate(const void* keys_void,
                                              void* state_void) {
  static_assert(RandenTraits::kCapacityBytes == sizeof(Vector128),
                "Capacity mismatch");

  auto* state = reinterpret_cast<absl::uint128*>(state_void);
  const auto* keys = reinterpret_cast<const absl::uint128*>(keys_void);

  const Vector128 prev_inner = Vector128Load(state);

  SwapEndian(state);

  Permute(state, keys);

  SwapEndian(state);

  // Ensure backtracking resistance.
  Vector128 inner = Vector128Load(state);
  inner ^= prev_inner;
  Vector128Store(inner, state);
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // (ABSL_RANDEN_HWAES_IMPL)
