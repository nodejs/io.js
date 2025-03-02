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

#include <stdint.h>
#include <string.h>  // memcpy

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/crypto_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

#define HWY_PRINT_CLMUL_GOLDEN 0

#if HWY_TARGET != HWY_SCALAR

class TestAES {
  template <typename T, class D>
  HWY_NOINLINE void TestSBox(T /*unused*/, D d) {
    // The generic implementation of the S-box is difficult to verify by
    // inspection, so we add a white-box test that verifies it using enumeration
    // (outputs for 0..255 vs. https://en.wikipedia.org/wiki/Rijndael_S-box).
    const uint8_t sbox[256] = {
        0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b,
        0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
        0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26,
        0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
        0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2,
        0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
        0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed,
        0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
        0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f,
        0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
        0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec,
        0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
        0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14,
        0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
        0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
        0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
        0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f,
        0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
        0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11,
        0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
        0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
        0xb0, 0x54, 0xbb, 0x16};

    // Ensure it's safe to load an entire vector by padding.
    const size_t N = Lanes(d);
    const size_t padded = RoundUpTo(256, N);
    auto expected = AllocateAligned<T>(padded);
    HWY_ASSERT(expected);
    // Must wrap around to match the input (Iota).
    for (size_t pos = 0; pos < padded;) {
      const size_t remaining = HWY_MIN(padded - pos, size_t(256));
      memcpy(expected.get() + pos, sbox, remaining);
      pos += remaining;
    }

    for (size_t i = 0; i < 256; i += N) {
      const auto in = Iota(d, i);
      HWY_ASSERT_VEC_EQ(d, expected.get() + i, detail::SubBytes(in));
    }
  }

 public:
  template <typename T, class D>
  HWY_NOINLINE void operator()(T t, D d) {
    // Test vector (after first KeyAddition) from
    // https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/AES_Core128.pdf
    alignas(16) static constexpr uint8_t test_lanes[16] = {
        0x40, 0xBF, 0xAB, 0xF4, 0x06, 0xEE, 0x4D, 0x30,
        0x42, 0xCA, 0x6B, 0x99, 0x7A, 0x5C, 0x58, 0x16};
    const auto test = LoadDup128(d, test_lanes);

    // = ShiftRow result
    alignas(16) static constexpr uint8_t expected_sr_lanes[16] = {
        0x09, 0x28, 0x7F, 0x47, 0x6F, 0x74, 0x6A, 0xBF,
        0x2C, 0x4A, 0x62, 0x04, 0xDA, 0x08, 0xE3, 0xEE};
    const auto expected_sr = LoadDup128(d, expected_sr_lanes);

    // = MixColumn result
    alignas(16) static constexpr uint8_t expected_mc_lanes[16] = {
        0x52, 0x9F, 0x16, 0xC2, 0x97, 0x86, 0x15, 0xCA,
        0xE0, 0x1A, 0xAE, 0x54, 0xBA, 0x1A, 0x26, 0x59};
    const auto expected_mc = LoadDup128(d, expected_mc_lanes);

    // = KeyAddition result
    alignas(16) static constexpr uint8_t expected_lanes[16] = {
        0xF2, 0x65, 0xE8, 0xD5, 0x1F, 0xD2, 0x39, 0x7B,
        0xC3, 0xB9, 0x97, 0x6D, 0x90, 0x76, 0x50, 0x5C};
    const auto expected = LoadDup128(d, expected_lanes);

    alignas(16) uint8_t key_lanes[16];
    for (size_t i = 0; i < 16; ++i) {
      key_lanes[i] = expected_mc_lanes[i] ^ expected_lanes[i];
    }
    const auto round_key = LoadDup128(d, key_lanes);

    HWY_ASSERT_VEC_EQ(d, expected_mc, AESRound(test, Zero(d)));
    HWY_ASSERT_VEC_EQ(d, expected, AESRound(test, round_key));
    HWY_ASSERT_VEC_EQ(d, expected_sr, AESLastRound(test, Zero(d)));
    HWY_ASSERT_VEC_EQ(d, Xor(expected_sr, round_key),
                      AESLastRound(test, round_key));

    TestSBox(t, d);
  }
};
HWY_NOINLINE void TestAllAES() { ForGEVectors<128, TestAES>()(uint8_t()); }

class TestAESInverse {
  template <typename T, class D>
  HWY_NOINLINE void TestInverseSBox(T /*unused*/, D d) {
    // The generic implementation of the inverse S-box is difficult to verify by
    // inspection, so we add a white-box test that verifies it using enumeration
    // (outputs for 0..255 vs. https://en.wikipedia.org/wiki/Rijndael_S-box).
    const uint8_t inv_sbox[256] = {
        0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e,
        0x81, 0xf3, 0xd7, 0xfb, 0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87,
        0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb, 0x54, 0x7b, 0x94, 0x32,
        0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
        0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49,
        0x6d, 0x8b, 0xd1, 0x25, 0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16,
        0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92, 0x6c, 0x70, 0x48, 0x50,
        0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
        0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05,
        0xb8, 0xb3, 0x45, 0x06, 0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02,
        0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b, 0x3a, 0x91, 0x11, 0x41,
        0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
        0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8,
        0x1c, 0x75, 0xdf, 0x6e, 0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
        0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b, 0xfc, 0x56, 0x3e, 0x4b,
        0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
        0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59,
        0x27, 0x80, 0xec, 0x5f, 0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d,
        0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef, 0xa0, 0xe0, 0x3b, 0x4d,
        0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
        0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63,
        0x55, 0x21, 0x0c, 0x7d};

    // Ensure it's safe to load an entire vector by padding.
    const size_t N = Lanes(d);
    const size_t padded = RoundUpTo(256, N);
    auto expected = AllocateAligned<T>(padded);
    HWY_ASSERT(expected);
    // Must wrap around to match the input (Iota).
    for (size_t pos = 0; pos < padded;) {
      const size_t remaining = HWY_MIN(padded - pos, size_t(256));
      memcpy(expected.get() + pos, inv_sbox, remaining);
      pos += remaining;
    }

    for (size_t i = 0; i < 256; i += N) {
      const auto in = Iota(d, i);
      HWY_ASSERT_VEC_EQ(d, expected.get() + i, detail::InvSubBytes(in));
    }
  }

  template <typename T, class D>
  HWY_INLINE void TestAESRoundInv(T /*unused*/, D d) {
    // Test vector (after first KeyAddition) from page 37 of
    // https://nvlpubs.nist.gov/nistpubs/fips/nist.fips.197.pdf
    alignas(16) static constexpr uint8_t test_lanes[16] = {
        0x7A, 0xD5, 0xFD, 0xA7, 0x89, 0xEF, 0x4E, 0x27,
        0x2B, 0xCA, 0x10, 0x0B, 0x3D, 0x9F, 0xF5, 0x9F};
    const auto test = LoadDup128(d, test_lanes);

    // = InvShiftRow result
    alignas(16) static constexpr uint8_t expected_isr_lanes[16] = {
        0xBD, 0x6E, 0x7C, 0x3D, 0xF2, 0xB5, 0x77, 0x9E,
        0x0B, 0x61, 0x21, 0x6E, 0x8B, 0x10, 0xB6, 0x89};
    const auto expected_isr = LoadDup128(d, expected_isr_lanes);

    // = InvMixColumn result
    alignas(16) static constexpr uint8_t expected_imc_lanes[16] = {
        0x47, 0x73, 0xB9, 0x1F, 0xF7, 0x2F, 0x35, 0x43,
        0x61, 0xCB, 0x01, 0x8E, 0xA1, 0xE6, 0xCF, 0x2C};
    const auto expected_imc = LoadDup128(d, expected_imc_lanes);

    // = KeyAddition result
    alignas(16) static constexpr uint8_t expected_lanes[16] = {
        0x13, 0xAA, 0x29, 0xBE, 0x9C, 0x8F, 0xAF, 0xF6,
        0xF7, 0x70, 0xF5, 0x80, 0x00, 0xF7, 0xBF, 0x03};
    const auto expected = LoadDup128(d, expected_lanes);

    alignas(16) uint8_t key_lanes[16];
    for (size_t i = 0; i < 16; ++i) {
      key_lanes[i] = expected_imc_lanes[i] ^ expected_lanes[i];
    }
    const auto round_key = LoadDup128(d, key_lanes);

    HWY_ASSERT_VEC_EQ(d, expected_isr, AESLastRoundInv(test, Zero(d)));
    HWY_ASSERT_VEC_EQ(d, expected_imc, AESRoundInv(test, Zero(d)));
    HWY_ASSERT_VEC_EQ(d, expected_imc,
                      AESInvMixColumns(AESLastRoundInv(test, Zero(d))));
    HWY_ASSERT_VEC_EQ(d, expected, AESRoundInv(test, round_key));
  }

  template <typename T, class D>
  HWY_INLINE void TestAESLastRoundInv(T /*unused*/, D d) {
    // Test vector (after the KeyAddition operation of round 9) from page 38 of
    // https://nvlpubs.nist.gov/nistpubs/fips/nist.fips.197.pdf
    alignas(16) static constexpr uint8_t test_lanes[16] = {
        0x63, 0x53, 0xE0, 0x8C, 0x09, 0x60, 0xE1, 0x04,
        0xCD, 0x70, 0xB7, 0x51, 0xBA, 0xCA, 0xD0, 0xE7};
    const auto test = LoadDup128(d, test_lanes);

    // = InvShiftRow result
    alignas(16) static constexpr uint8_t expected_isr_lanes[16] = {
        0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70,
        0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0};
    const auto expected_isr = LoadDup128(d, expected_isr_lanes);

    // = KeyAddition result
    alignas(16) static constexpr uint8_t expected_lanes[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const auto expected = LoadDup128(d, expected_lanes);

    alignas(16) uint8_t key_lanes[16];
    for (size_t i = 0; i < 16; ++i) {
      key_lanes[i] = expected_isr_lanes[i] ^ expected_lanes[i];
    }
    const auto round_key = LoadDup128(d, key_lanes);

    HWY_ASSERT_VEC_EQ(d, expected_isr, AESLastRoundInv(test, Zero(d)));
    HWY_ASSERT_VEC_EQ(d, expected, AESLastRoundInv(test, round_key));
  }

 public:
  template <typename T, class D>
  HWY_NOINLINE void operator()(T t, D d) {
    TestAESRoundInv(t, d);
    TestAESLastRoundInv(t, d);
    TestInverseSBox(t, d);
  }
};
HWY_NOINLINE void TestAllAESInverse() {
  ForGEVectors<128, TestAESInverse>()(uint8_t());
}

struct TestAESKeyGenAssist {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*t*/, D d) {
    alignas(16) static constexpr uint8_t kTestVect1[16] = {
        0x27, 0xCF, 0x73, 0xC3, 0x27, 0xCF, 0x73, 0xC3,
        0x74, 0x01, 0x90, 0x5A, 0x74, 0x01, 0x90, 0x5A};
    alignas(16) static constexpr uint8_t kExpectedResult1[16] = {
        0xCC, 0x8A, 0x8F, 0x2E, 0xAA, 0x8F, 0x2E, 0xCC,
        0x92, 0x7C, 0x60, 0xBE, 0x5C, 0x60, 0xBE, 0x92};

    const auto expected_1 = LoadDup128(d, kExpectedResult1);
    const auto actual_1 = AESKeyGenAssist<0x20>(LoadDup128(d, kTestVect1));
    HWY_ASSERT_VEC_EQ(d, expected_1, actual_1);

    alignas(16) static constexpr uint8_t kTestVect2[16] = {
        0xD0, 0x14, 0xF9, 0xA8, 0x57, 0x5C, 0x00, 0x6E,
        0xE1, 0x3F, 0x0C, 0xC8, 0xC9, 0xEE, 0x25, 0x89};
    alignas(16) static constexpr uint8_t kExpectedResult2[16] = {
        0x5B, 0x4A, 0x63, 0x9F, 0x7C, 0x63, 0x9F, 0x5B,
        0xDD, 0x28, 0x3F, 0xA7, 0x1E, 0x3F, 0xA7, 0xDD};

    const auto expected_2 = LoadDup128(d, kExpectedResult2);
    const auto actual_2 = AESKeyGenAssist<0x36>(LoadDup128(d, kTestVect2));
    HWY_ASSERT_VEC_EQ(d, expected_2, actual_2);
  }
};

HWY_NOINLINE void TestAllAESKeyGenAssist() {
  ForGEVectors<128, TestAESKeyGenAssist>()(uint8_t());
}

#else
HWY_NOINLINE void TestAllAES() {}
HWY_NOINLINE void TestAllAESInverse() {}
HWY_NOINLINE void TestAllAESKeyGenAssist() {}
#endif  // HWY_TARGET != HWY_SCALAR

struct TestCLMul {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    // needs 64 bit lanes and 128-bit result
#if HWY_TARGET != HWY_SCALAR && HWY_HAVE_INTEGER64
    const size_t N = Lanes(d);
    if (N == 1) return;

    auto in1 = AllocateAligned<T>(N);
    auto in2 = AllocateAligned<T>(N);
    HWY_ASSERT(in1 && in2);

    constexpr size_t kCLMulNum = 512;
    // Depends on rng!
    static constexpr uint64_t kCLMulLower[kCLMulNum] = {
        0x24511d4ce34d6350ULL, 0x4ca582edde1236bbULL, 0x537e58f72dac25a8ULL,
        0x4e942d5e130b9225ULL, 0x75a906c519257a68ULL, 0x1df9f85126d96c5eULL,
        0x464e7c13f4ad286aULL, 0x138535ee35dabc40ULL, 0xb2f7477b892664ecULL,
        0x01557b077167c25dULL, 0xf32682490ee49624ULL, 0x0025bac603b9e140ULL,
        0xcaa86aca3e3daf40ULL, 0x1fbcfe4af73eb6c4ULL, 0x8ee8064dd0aae5dcULL,
        0x1248cb547858c213ULL, 0x37a55ee5b10fb34cULL, 0x6eb5c97b958f86e2ULL,
        0x4b1ab3eb655ea7cdULL, 0x1d66645a85627520ULL, 0xf8728e96daa36748ULL,
        0x38621043e6ff5e3bULL, 0xd1d28b5da5ffefb4ULL, 0x0a5cd65931546df7ULL,
        0x2a0639be3d844150ULL, 0x0e2d0f18c8d6f045ULL, 0xfacc770b963326c1ULL,
        0x19611b31ca2ef141ULL, 0xabea29510dd87518ULL, 0x18a7dc4b205f2768ULL,
        0x9d3975ea5612dc86ULL, 0x06319c139e374773ULL, 0x6641710400b4c390ULL,
        0x356c29b6001c3670ULL, 0xe9e04d851e040a00ULL, 0x21febe561222d79aULL,
        0xc071eaae6e148090ULL, 0x0eed351a0af94f5bULL, 0x04324eedb3c03688ULL,
        0x39e89b136e0d6ccdULL, 0x07d0fd2777a31600ULL, 0x44b8573827209822ULL,
        0x6d690229ea177d78ULL, 0x1b9749d960ba9f18ULL, 0x190945271c0fbb94ULL,
        0x189aea0e07d2c88eULL, 0xf18eab6b65a6beb2ULL, 0x57744b21c13d0d84ULL,
        0xf63050a613e95c2eULL, 0x12cd20d25f97102fULL, 0x5a5df0678dbcba60ULL,
        0x0b08fb80948bfafcULL, 0x44cf1cbe7c6fc3c8ULL, 0x166a470ef25da288ULL,
        0x2c498a609204e48cULL, 0x261b0a22585697ecULL, 0x737750574af7dde4ULL,
        0x4079959c60b01e0cULL, 0x06ed8aac13f782d6ULL, 0x019d454ba9b5ef20ULL,
        0xea1edbf96d49e858ULL, 0x17c2f3ebde9ac469ULL, 0x5cf72706e3d6f5e4ULL,
        0x16e856aa3c841516ULL, 0x256f7e3cef83368eULL, 0x47e17c8eb2774e77ULL,
        0x9b48ac150a804821ULL, 0x584523f61ccfdf22ULL, 0xedcb6a2a75d9e7f2ULL,
        0x1fe3d1838e537aa7ULL, 0x778872e9f64549caULL, 0x2f1cea6f0d3faf92ULL,
        0x0e8c4b6a9343f326ULL, 0x01902d1ba3048954ULL, 0xc5c1fd5269e91dc0ULL,
        0x0ef8a4707817eb9cULL, 0x1f696f09a5354ca4ULL, 0x369cd9de808b818cULL,
        0xf6917d1dd43fd784ULL, 0x7f4b76bf40dc166fULL, 0x4ce67698724ace12ULL,
        0x02c3bf60e6e9cd92ULL, 0xb8229e45b21458e8ULL, 0x415efd41e91adf49ULL,
        0x5edfcd516bb921cdULL, 0x5ff2c29429fd187eULL, 0x0af666b17103b3e0ULL,
        0x1f5e4ff8f54c9a5bULL, 0x429253d8a5544ba6ULL, 0x19de2fdf9f4d9dcaULL,
        0x29bf3d37ddc19a40ULL, 0x04d4513a879552baULL, 0x5cc7476cf71ee155ULL,
        0x40011f8c238784a5ULL, 0x1a3ae50b0fd2ee2bULL, 0x7db22f432ba462baULL,
        0x417290b0bee2284aULL, 0x055a6bd5bb853db2ULL, 0xaa667daeed8c2a34ULL,
        0x0d6b316bda7f3577ULL, 0x72d35598468e3d5dULL, 0x375b594804bfd33aULL,
        0x16ed3a319b540ae8ULL, 0x093bace4b4695afdULL, 0xc7118754ec2737ceULL,
        0x0fff361f0505c81aULL, 0x996e9e7291321af0ULL, 0x496b1d9b0b89ba8cULL,
        0x65a98b2e9181da9cULL, 0x70759c8dd45575dfULL, 0x3446fe727f5e2cbbULL,
        0x1121ae609d195e74ULL, 0x5ff5d68ce8a21018ULL, 0x0e27eca3825b60d6ULL,
        0x82f628bceca3d1daULL, 0x2756a0914e344047ULL, 0xa460406c1c708d50ULL,
        0x63ce32a0c083e491ULL, 0xc883e5a685c480e0ULL, 0x602c951891e600f9ULL,
        0x02ecb2e3911ca5f8ULL, 0x0d8675f4bb70781aULL, 0x43545cc3c78ea496ULL,
        0x04164b01d6b011c2ULL, 0x3acbb323dcab2c9bULL, 0x31c5ba4e22793082ULL,
        0x5a6484af5f7c2d10ULL, 0x1a929b16194e8078ULL, 0x7a6a75d03b313924ULL,
        0x0553c73a35b1d525ULL, 0xf18628c51142be34ULL, 0x1b51cf80d7efd8f5ULL,
        0x52e0ca4df63ee258ULL, 0x0e977099160650c9ULL, 0x6be1524e92024f70ULL,
        0x0ee2152625438b9dULL, 0xfa32af436f6d8eb4ULL, 0x5ecf49c2154287e5ULL,
        0x6b72f4ae3590569dULL, 0x086c5ee6e87bfb68ULL, 0x737a4f0dc04b6187ULL,
        0x08c3439280edea41ULL, 0x9547944f01636c5cULL, 0x6acfbfc2571cd71fULL,
        0x85d7842972449637ULL, 0x252ea5e5a7fad86aULL, 0x4e41468f99ba1632ULL,
        0x095e0c3ae63b25a2ULL, 0xb005ce88fd1c9425ULL, 0x748e668abbe09f03ULL,
        0xb2cfdf466b187d18ULL, 0x60b11e633d8fe845ULL, 0x07144c4d246db604ULL,
        0x139bcaac55e96125ULL, 0x118679b5a6176327ULL, 0x1cebe90fa4d9f83fULL,
        0x22244f52f0d312acULL, 0x669d4e17c9bfb713ULL, 0x96390e0b834bb0d0ULL,
        0x01f7f0e82ba08071ULL, 0x2dffeee31ca6d284ULL, 0x1f4738745ef039feULL,
        0x4ce0dd2b603b6420ULL, 0x0035fc905910a4d5ULL, 0x07df2b533df6fb04ULL,
        0x1cee2735c9b910ddULL, 0x2bc4af565f7809eaULL, 0x2f876c1f5cb1076cULL,
        0x33e079524099d056ULL, 0x169e0405d2f9efbaULL, 0x018643ab548a358cULL,
        0x1bb6fc4331cffe92ULL, 0x05111d3a04e92faaULL, 0x23c27ecf0d638b73ULL,
        0x1b79071dc1685d68ULL, 0x0662d20aba8e1e0cULL, 0xe7f6440277144c6fULL,
        0x4ca38b64c22196c0ULL, 0x43c05f6d1936fbeeULL, 0x0654199d4d1faf0fULL,
        0xf2014054e71c2d04ULL, 0x0a103e47e96b4c84ULL, 0x7986e691dd35b040ULL,
        0x4e1ebb53c306a341ULL, 0x2775bb3d75d65ba6ULL, 0x0562ab0adeff0f15ULL,
        0x3c2746ad5eba3eacULL, 0x1facdb5765680c60ULL, 0xb802a60027d81d00ULL,
        0x1191d0f6366ae3a9ULL, 0x81a97b5ae0ea5d14ULL, 0x06bee05b6178a770ULL,
        0xc7baeb2fe1d6aeb3ULL, 0x594cb5b867d04fdfULL, 0xf515a80138a4e350ULL,
        0x646417ad8073cf38ULL, 0x4a229a43373fb8d4ULL, 0x10fa6eafff1ca453ULL,
        0x9f060700895cc731ULL, 0x00521133d11d11f4ULL, 0xb940a2bb912a7a5cULL,
        0x3fab180670ad2a3cULL, 0x45a5f0e5b6fdb95dULL, 0x27c1baad6f946b15ULL,
        0x336c6bdbe527cf58ULL, 0x3b83aa602a5baea3ULL, 0xdf749153f9bcc376ULL,
        0x1a05513a6c0b4a90ULL, 0xb81e0b570a075c47ULL, 0x471fabb40bdc27ceULL,
        0x9dec9472f6853f60ULL, 0x361f71b88114193bULL, 0x3b550a8c4feeff00ULL,
        0x0f6cde5a68bc9bc0ULL, 0x3f50121a925703e0ULL, 0x6967ff66d6d343a9ULL,
        0xff6b5bd2ce7bc3ccULL, 0x05474cea08bf6cd8ULL, 0xf76eabbfaf108eb0ULL,
        0x067529be4fc6d981ULL, 0x4d766b137cf8a988ULL, 0x2f09c7395c5cfbbdULL,
        0x388793712da06228ULL, 0x02c9ff342c8f339aULL, 0x152c734139a860a3ULL,
        0x35776eb2b270c04dULL, 0x0f8d8b41f11c4608ULL, 0x0c2071665be6b288ULL,
        0xc034e212b3f71d88ULL, 0x071d961ef3276f99ULL, 0xf98598ee75b60773ULL,
        0x062062c58c6724e4ULL, 0xd156438e2125572cULL, 0x38552d59a7f0f7c8ULL,
        0x1a402178206e413cULL, 0x1f1f996c68293b26ULL, 0x8bce3cafe1730f7eULL,
        0x2d0480a0828f6bf5ULL, 0x6c99cffa171f92f6ULL, 0x0087f842bb0ac681ULL,
        0x11d7ed06e1e7fd3eULL, 0x07cb1186f2385dc6ULL, 0x5d7763ebff1e170fULL,
        0x2dacc870231ac292ULL, 0x8486317a9ffb390cULL, 0x1c3a6dd20c959ac6ULL,
        0x90dc96e3992e06b8ULL, 0x70d60bfa33e72b67ULL, 0x70c9bddd0985ee63ULL,
        0x012c9767b3673093ULL, 0xfcd3bc5580f6a88aULL, 0x0ac80017ef6308c3ULL,
        0xdb67d709ef4bba09ULL, 0x4c63e324f0e247ccULL, 0xa15481d3fe219d60ULL,
        0x094c4279cdccb501ULL, 0x965a28c72575cb82ULL, 0x022869db25e391ebULL,
        0x37f528c146023910ULL, 0x0c1290636917deceULL, 0x9aee25e96251ca9cULL,
        0x728ac5ba853b69c2ULL, 0x9f272c93c4be20c8ULL, 0x06c1aa6319d28124ULL,
        0x4324496b1ca8a4f7ULL, 0x0096ecfe7dfc0189ULL, 0x9e06131b19ae0020ULL,
        0x15278b15902f4597ULL, 0x2a9fece8c13842d8ULL, 0x1d4e6781f0e1355eULL,
        0x6855b712d3dbf7c0ULL, 0x06a07fad99be6f46ULL, 0x3ed9d7957e4d1d7cULL,
        0x0c326f7cbc248bb2ULL, 0xe6363ad2c537cf51ULL, 0x0e12eb1c40723f13ULL,
        0xf5c6ac850afba803ULL, 0x0322a79d615fa9f0ULL, 0x6116696ed97bd5f8ULL,
        0x0d438080fbbdc9f1ULL, 0x2e4dc42c38f1e243ULL, 0x64948e9104f3a5bfULL,
        0x9fd622371bdb5f00ULL, 0x0f12bf082b2a1b6eULL, 0x4b1f8d867d78031cULL,
        0x134392ea9f5ef832ULL, 0xf3d70472321bc23eULL, 0x05fcbe5e9eea268eULL,
        0x136dede7175a22cfULL, 0x1308f8baac2cbcccULL, 0xd691026f0915eb64ULL,
        0x0e49a668345c3a38ULL, 0x24ddbbe8bc96f331ULL, 0x4d2ec9479b640578ULL,
        0x450f0697327b359cULL, 0x32b45360f4488ee0ULL, 0x4f6d9ecec46a105aULL,
        0x5500c63401ae8e80ULL, 0x47dea495cf6f98baULL, 0x13dc9a2dfca80babULL,
        0xe6f8a93f7b24ca92ULL, 0x073f57a6d900a87fULL, 0x9ddb935fd3aa695aULL,
        0x101e98d24b39e8aaULL, 0x6b8d0eb95a507ddcULL, 0x45a908b3903d209bULL,
        0x6c96a3e119e617d4ULL, 0x2442787543d3be48ULL, 0xd3bc055c7544b364ULL,
        0x7693bb042ca8653eULL, 0xb95e3a4ea5d0101eULL, 0x116f0d459bb94a73ULL,
        0x841244b72cdc5e90ULL, 0x1271acced6cb34d3ULL, 0x07d289106524d638ULL,
        0x537c9cf49c01b5bbULL, 0x8a8e16706bb7a5daULL, 0x12e50a9c499dc3a9ULL,
        0x1cade520db2ba830ULL, 0x1add52f000d7db70ULL, 0x12cf15db2ce78e30ULL,
        0x0657eaf606bfc866ULL, 0x4026816d3b05b1d0ULL, 0x1ba0ebdf90128e4aULL,
        0xdfd649375996dd6eULL, 0x0f416e906c23d9aeULL, 0x384273cad0582a24ULL,
        0x2ff27b0378a46189ULL, 0xc4ecd18a2d7a7616ULL, 0x35cef0b5cd51d640ULL,
        0x7d582363643f48b7ULL, 0x0984ad746ad0ab7cULL, 0x2990a999835f9688ULL,
        0x2d4df66a97b19e05ULL, 0x592c79720af99aa2ULL, 0x052863c230602cd3ULL,
        0x5f5e2b15edcf2840ULL, 0x01dff1b694b978b0ULL, 0x14345a48b622025eULL,
        0x028fab3b6407f715ULL, 0x3455d188e6feca50ULL, 0x1d0d40288fb1b5fdULL,
        0x4685c5c2b6a1e5aeULL, 0x3a2077b1e5fe5adeULL, 0x1bc55d611445a0d8ULL,
        0x05480ae95f3f83feULL, 0xbbb59cfcf7e17fb6ULL, 0x13f7f10970bbb990ULL,
        0x6d00ac169425a352ULL, 0x7da0db397ef2d5d3ULL, 0x5b512a247f8d2479ULL,
        0x637eaa6a977c3c32ULL, 0x3720f0ae37cba89cULL, 0x443df6e6aa7f525bULL,
        0x28664c287dcef321ULL, 0x03c267c00cf35e49ULL, 0x690185572d4021deULL,
        0x2707ff2596e321c2ULL, 0xd865f5af7722c380ULL, 0x1ea285658e33aafbULL,
        0xc257c5e88755bef4ULL, 0x066f67275cfcc31eULL, 0xb09931945cc0fed0ULL,
        0x58c1dc38d6e3a03fULL, 0xf99489678fc94ee8ULL, 0x75045bb99be5758aULL,
        0x6c163bc34b40feefULL, 0x0420063ce7bdd3b4ULL, 0xf86ef10582bf2e28ULL,
        0x162c3449ca14858cULL, 0x94106aa61dfe3280ULL, 0x4073ae7a4e7e4941ULL,
        0x32b13fd179c250b4ULL, 0x0178fbb216a7e744ULL, 0xf840ae2f1cf92669ULL,
        0x18fc709acc80243dULL, 0x20ac2ebd69f4d558ULL, 0x6e580ad9c73ad46aULL,
        0x76d2b535b541c19dULL, 0x6c7a3fb9dd0ce0afULL, 0xc3481689b9754f28ULL,
        0x156e813b6557abdbULL, 0x6ee372e31276eb10ULL, 0x19cf37c038c8d381ULL,
        0x00d4d906c9ae3072ULL, 0x09f03cbb6dfbfd40ULL, 0x461ba31c4125f3cfULL,
        0x25b29fc63ad9f05bULL, 0x6808c95c2dddede9ULL, 0x0564224337066d9bULL,
        0xc87eb5f4a4d966f2ULL, 0x66fc66e1701f5847ULL, 0xc553a3559f74da28ULL,
        0x1dfd841be574df43ULL, 0x3ee2f100c3ebc082ULL, 0x1a2c4f9517b56e89ULL,
        0x502f65c4b535c8ffULL, 0x1da5663ab6f96ec0ULL, 0xba1f80b73988152cULL,
        0x364ff12182ac8dc1ULL, 0xe3457a3c4871db31ULL, 0x6ae9cadf92fd7e84ULL,
        0x9621ba3d6ca15186ULL, 0x00ff5af878c144ceULL, 0x918464dc130101a4ULL,
        0x036511e6b187efa6ULL, 0x06667d66550ff260ULL, 0x7fd18913f9b51bc1ULL,
        0x3740e6b27af77aa8ULL, 0x1f546c2fd358ff8aULL, 0x42f1424e3115c891ULL,
        0x03767db4e3a1bb33ULL, 0xa171a1c564345060ULL, 0x0afcf632fd7b1324ULL,
        0xb59508d933ffb7d0ULL, 0x57d766c42071be83ULL, 0x659f0447546114a2ULL,
        0x4070364481c460aeULL, 0xa2b9752280644d52ULL, 0x04ab884bea5771bdULL,
        0x87cd135602a232b4ULL, 0x15e54cd9a8155313ULL, 0x1e8005efaa3e1047ULL,
        0x696b93f4ab15d39fULL, 0x0855a8e540de863aULL, 0x0bb11799e79f9426ULL,
        0xeffa61e5c1b579baULL, 0x1e060a1d11808219ULL, 0x10e219205667c599ULL,
        0x2f7b206091c49498ULL, 0xb48854c820064860ULL, 0x21c4aaa3bfbe4a38ULL,
        0x8f4a032a3fa67e9cULL, 0x3146b3823401e2acULL, 0x3afee26f19d88400ULL,
        0x167087c485791d38ULL, 0xb67a1ed945b0fb4bULL, 0x02436eb17e27f1c0ULL,
        0xe05afce2ce2d2790ULL, 0x49c536fc6224cfebULL, 0x178865b3b862b856ULL,
        0x1ce530de26acde5bULL, 0x87312c0b30a06f38ULL, 0x03e653b578558d76ULL,
        0x4d3663c21d8b3accULL, 0x038003c23626914aULL, 0xd9d5a2c052a09451ULL,
        0x39b5acfe08a49384ULL, 0x40f349956d5800e4ULL, 0x0968b6950b1bd8feULL,
        0xd60b2ca030f3779cULL, 0x7c8bc11a23ce18edULL, 0xcc23374e27630bc2ULL,
        0x2e38fc2a8bb33210ULL, 0xe421357814ee5c44ULL, 0x315fb65ea71ec671ULL,
        0xfb1b0223f70ed290ULL, 0x30556c9f983eaf07ULL, 0x8dd438c3d0cd625aULL,
        0x05a8fd0c7ffde71bULL, 0x764d1313b5aeec7aULL, 0x2036af5de9622f47ULL,
        0x508a5bfadda292feULL, 0x3f77f04ba2830e90ULL, 0x9047cd9c66ca66d2ULL,
        0x1168b5318a54eb21ULL, 0xc93462d221da2e15ULL, 0x4c2c7cc54abc066eULL,
        0x767a56fec478240eULL, 0x095de72546595bd3ULL, 0xc9da535865158558ULL,
        0x1baccf36f33e73fbULL, 0xf3d7dbe64df77f18ULL, 0x1f8ebbb7be4850b8ULL,
        0x043c5ed77bce25a1ULL, 0x07d401041b2a178aULL, 0x9181ebb8bd8d5618ULL,
        0x078b935dc3e4034aULL, 0x7b59c08954214300ULL, 0x03570dc2a4f84421ULL,
        0xdd8715b82f6b4078ULL, 0x2bb49c8bb544163bULL, 0xc9eb125564d59686ULL,
        0x5fdc7a38f80b810aULL, 0x3a4a6d8fff686544ULL, 0x28360e2418627d3aULL,
        0x60874244c95ed992ULL, 0x2115cc1dd9c34ed3ULL, 0xfaa3ef61f55e9efcULL,
        0x27ac9b1ef1adc7e6ULL, 0x95ea00478fec3f54ULL, 0x5aea808b2d99ab43ULL,
        0xc8f79e51fe43a580ULL, 0x5dbccd714236ce25ULL, 0x783fa76ed0753458ULL,
        0x48cb290f19d84655ULL, 0xc86a832f7696099aULL, 0x52f30c6fec0e71d3ULL,
        0x77d4e91e8cdeb886ULL, 0x7169a703c6a79ccdULL, 0x98208145b9596f74ULL,
        0x0945695c761c0796ULL, 0x0be897830d17bae0ULL, 0x033ad3924caeeeb4ULL,
        0xedecb6cfa2d303a8ULL, 0x3f86b074818642e7ULL, 0xeefa7c878a8b03f4ULL,
        0x093c101b80922551ULL, 0xfb3b4e6c26ac0034ULL, 0x162bf87999b94f5eULL,
        0xeaedae76e975b17cULL, 0x1852aa090effe18eULL};

    static constexpr uint64_t kCLMulUpper[kCLMulNum] = {
        0xbb41199b1d587c69ULL, 0x514d94d55894ee29ULL, 0xebc6cd4d2efd5d16ULL,
        0x042044ad2de477fdULL, 0xb865c8b0fcdf4b15ULL, 0x0724d7e551cc40f3ULL,
        0xb15a16f39edb0bccULL, 0x37d64419ede7a171ULL, 0x2aa01bb80c753401ULL,
        0x06ff3f8a95fdaf4dULL, 0x79898cc0838546deULL, 0x776acbd1b237c60aULL,
        0x4c1753be4f4e0064ULL, 0x0ba9243601206ed3ULL, 0xd567c3b1bf3ec557ULL,
        0x043fac7bcff61fb3ULL, 0x49356232b159fb2fULL, 0x3910c82038102d4dULL,
        0x30592fef753eb300ULL, 0x7b2660e0c92a9e9aULL, 0x8246c9248d671ef0ULL,
        0x5a0dcd95147af5faULL, 0x43fde953909cc0eaULL, 0x06147b972cb96e1bULL,
        0xd84193a6b2411d80ULL, 0x00cd7711b950196fULL, 0x1088f9f4ade7fa64ULL,
        0x05a13096ec113cfbULL, 0x958d816d53b00edcULL, 0x3846154a7cdba9cbULL,
        0x8af516db6b27d1e6ULL, 0x1a1d462ab8a33b13ULL, 0x4040b0ac1b2c754cULL,
        0x05127fe9af2fe1d6ULL, 0x9f96e79374321fa6ULL, 0x06ff64a4d9c326f3ULL,
        0x28709566e158ac15ULL, 0x301701d7111ca51cULL, 0x31e0445d1b9d9544ULL,
        0x0a95aff69bf1d03eULL, 0x7c298c8414ecb879ULL, 0x00801499b4143195ULL,
        0x91521a00dd676a5cULL, 0x2777526a14c2f723ULL, 0xfa26aac6a6357dddULL,
        0x1d265889b0187a4bULL, 0xcd6e70fa8ed283e4ULL, 0x18a815aa50ea92caULL,
        0xc01e082694a263c6ULL, 0x4b40163ba53daf25ULL, 0xbc658caff6501673ULL,
        0x3ba35359586b9652ULL, 0x74f96acc97a4936cULL, 0x3989dfdb0cf1d2cfULL,
        0x358a01eaa50dda32ULL, 0x01109a5ed8f0802bULL, 0x55b84922e63c2958ULL,
        0x55b14843d87551d5ULL, 0x1db8ec61b1b578d8ULL, 0x79a2d49ef8c3658fULL,
        0xa304516816b3fbe0ULL, 0x163ecc09cc7b82f9ULL, 0xab91e8d22aabef00ULL,
        0x0ed6b09262de8354ULL, 0xcfd47d34cf73f6f2ULL, 0x7dbd1db2390bc6c3ULL,
        0x5ae789d3875e7b00ULL, 0x1d60fd0e70fe8fa4ULL, 0x690bc15d5ae4f6f5ULL,
        0x121ef5565104fb44ULL, 0x6e98e89297353b54ULL, 0x42554949249d62edULL,
        0xd6d6d16b12df78d2ULL, 0x320b33549b74975dULL, 0xd2a0618763d22e00ULL,
        0x0808deb93cba2017ULL, 0x01bd3b2302a2cc70ULL, 0x0b7b8dd4d71c8dd6ULL,
        0x34d60a3382a0756cULL, 0x40984584c8219629ULL, 0xf1152cba10093a66ULL,
        0x068001c6b2159ccbULL, 0x3d70f13c6cda0800ULL, 0x0e6b6746a322b956ULL,
        0x83a494319d8c770bULL, 0x0faecf64a8553e9aULL, 0xa34919222c39b1bcULL,
        0x0c63850d89e71c6fULL, 0x585f0bee92e53dc8ULL, 0x10f222b13b4fa5deULL,
        0x61573114f94252f2ULL, 0x09d59c311fba6c27ULL, 0x014effa7da49ed4eULL,
        0x4a400a1bc1c31d26ULL, 0xc9091c047b484972ULL, 0x3989f341ec2230ccULL,
        0xdcb03a98b3aee41eULL, 0x4a54a676a33a95e1ULL, 0xe499b7753951ef7cULL,
        0x2f43b1d1061d8b48ULL, 0xc3313bdc68ceb146ULL, 0x5159f6bc0e99227fULL,
        0x98128e6d9c05efcaULL, 0x15ea32b27f77815bULL, 0xe882c054e2654eecULL,
        0x003d2cdb8faee8c6ULL, 0xb416dd333a9fe1dfULL, 0x73f6746aefcfc98bULL,
        0x93dc114c10a38d70ULL, 0x05055941657845eaULL, 0x2ed7351347349334ULL,
        0x26fb1ee2c69ae690ULL, 0xa4575d10dc5b28e0ULL, 0x3395b11295e485ebULL,
        0xe840f198a224551cULL, 0x78e6e5a431d941d4ULL, 0xa1fee3ceab27f391ULL,
        0x07d35b3c5698d0dcULL, 0x983c67fca9174a29ULL, 0x2bb6bbae72b5144aULL,
        0xa7730b8d13ce58efULL, 0x51b5272883de1998ULL, 0xb334e128bb55e260ULL,
        0x1cacf5fbbe1b9974ULL, 0x71a9df4bb743de60ULL, 0x5176fe545c2d0d7aULL,
        0xbe592ecf1a16d672ULL, 0x27aa8a30c3efe460ULL, 0x4c78a32f47991e06ULL,
        0x383459294312f26aULL, 0x97ba789127f1490cULL, 0x51c9aa8a3abd1ef1ULL,
        0xcc7355188121e50fULL, 0x0ecb3a178ae334c1ULL, 0x84879a5e574b7160ULL,
        0x0765298f6389e8f3ULL, 0x5c6750435539bb22ULL, 0x11a05cf056c937b5ULL,
        0xb5dc2172dbfb7662ULL, 0x3ffc17915d9f40e8ULL, 0xbc7904daf3b431b0ULL,
        0x71f2088490930a7cULL, 0xa89505fd9efb53c4ULL, 0x02e194afd61c5671ULL,
        0x99a97f4abf35fcecULL, 0x26830aad30fae96fULL, 0x4b2abc16b25cf0b0ULL,
        0x07ec6fffa1cafbdbULL, 0xf38188fde97a280cULL, 0x121335701afff64dULL,
        0xea5ef38b4e672a64ULL, 0x477edbcae3eabf03ULL, 0xa32813cc0e0d244dULL,
        0x13346d2af4972eefULL, 0xcbc18357af1cfa9aULL, 0x561b630316e73fa6ULL,
        0xe9dfb53249249305ULL, 0x5d2b9dd1479312eeULL, 0x3458008119b56d04ULL,
        0x50e6790b49801385ULL, 0x5bb9febe2349492bULL, 0x0c2813954299098fULL,
        0xf747b0c890a071d5ULL, 0x417e8f82cc028d77ULL, 0xa134fee611d804f8ULL,
        0x24c99ee9a0408761ULL, 0x3ebb224e727137f3ULL, 0x0686022073ceb846ULL,
        0xa05e901fb82ad7daULL, 0x0ece7dc43ab470fcULL, 0x2d334ecc58f7d6a3ULL,
        0x23166fadacc54e40ULL, 0x9c3a4472f839556eULL, 0x071717ab5267a4adULL,
        0xb6600ac351ba3ea0ULL, 0x30ec748313bb63d4ULL, 0xb5374e39287b23ccULL,
        0x074d75e784238aebULL, 0x77315879243914a4ULL, 0x3bbb1971490865f1ULL,
        0xa355c21f4fbe02d3ULL, 0x0027f4bb38c8f402ULL, 0xeef8708e652bc5f0ULL,
        0x7b9aa56cf9440050ULL, 0x113ac03c16cfc924ULL, 0x395db36d3e4bef9fULL,
        0x5d826fabcaa597aeULL, 0x2a77d3c58786d7e0ULL, 0x85996859a3ba19d4ULL,
        0x01e7e3c904c2d97fULL, 0x34f90b9b98d51fd0ULL, 0x243aa97fd2e99bb7ULL,
        0x40a0cebc4f65c1e8ULL, 0x46d3922ed4a5503eULL, 0x446e7ecaf1f9c0a4ULL,
        0x49dc11558bc2e6aeULL, 0xe7a9f20881793af8ULL, 0x5771cc4bc98103f1ULL,
        0x2446ea6e718fce90ULL, 0x25d14aca7f7da198ULL, 0x4347af186f9af964ULL,
        0x10cb44fc9146363aULL, 0x8a35587afce476b4ULL, 0x575144662fee3d3aULL,
        0x69f41177a6bc7a05ULL, 0x02ff8c38d6b3c898ULL, 0x57c73589a226ca40ULL,
        0x732f6b5baae66683ULL, 0x00c008bbedd4bb34ULL, 0x7412ff09524d6cadULL,
        0xb8fd0b5ad8c145a8ULL, 0x74bd9f94b6cdc7dfULL, 0x68233b317ca6c19cULL,
        0x314b9c2c08b15c54ULL, 0x5bd1ad72072ebd08ULL, 0x6610e6a6c07030e4ULL,
        0xa4fc38e885ead7ceULL, 0x36975d1ca439e034ULL, 0xa358f0fe358ffb1aULL,
        0x38e247ad663acf7dULL, 0x77daed3643b5deb8ULL, 0x5507c2aeae1ec3d0ULL,
        0xfdec226c73acf775ULL, 0x1b87ff5f5033492dULL, 0xa832dee545d9033fULL,
        0x1cee43a61e41783bULL, 0xdff82b2e2d822f69ULL, 0x2bbc9a376cb38cf2ULL,
        0x117b1cdaf765dc02ULL, 0x26a407f5682be270ULL, 0x8eb664cf5634af28ULL,
        0x17cb4513bec68551ULL, 0xb0df6527900cbfd0ULL, 0x335a2dc79c5afdfcULL,
        0xa2f0ca4cd38dca88ULL, 0x1c370713b81a2de1ULL, 0x849d5df654d1adfcULL,
        0x2fd1f7675ae14e44ULL, 0x4ff64dfc02247f7bULL, 0x3a2bcf40e395a48dULL,
        0x436248c821b187c1ULL, 0x29f4337b1c7104c0ULL, 0xfc317c46e6630ec4ULL,
        0x2774bccc4e3264c7ULL, 0x2d03218d9d5bee23ULL, 0x36a0ed04d659058aULL,
        0x452484461573cab6ULL, 0x0708edf87ed6272bULL, 0xf07960a1587446cbULL,
        0x3660167b067d84e0ULL, 0x65990a6993ddf8c4ULL, 0x0b197cd3d0b40b3fULL,
        0x1dcec4ab619f3a05ULL, 0x722ab223a84f9182ULL, 0x0822d61a81e7c38fULL,
        0x3d22ad75da563201ULL, 0x93cef6979fd35e0fULL, 0x05c3c25ae598b14cULL,
        0x1338df97dd496377ULL, 0x15bc324dc9c20acfULL, 0x96397c6127e6e8cfULL,
        0x004d01069ef2050fULL, 0x2fcf2e27893fdcbcULL, 0x072f77c3e44f4a5cULL,
        0x5eb1d80b3fe44918ULL, 0x1f59e7c28cc21f22ULL, 0x3390ce5df055c1f8ULL,
        0x4c0ef11df92cb6bfULL, 0x50f82f9e0848c900ULL, 0x08d0fde3ffc0ae38ULL,
        0xbd8d0089a3fbfb73ULL, 0x118ba5b0f311ef59ULL, 0x9be9a8407b926a61ULL,
        0x4ea04fbb21318f63ULL, 0xa1c8e7bb07b871ffULL, 0x1253a7262d5d3b02ULL,
        0x13e997a0512e5b29ULL, 0x54318460ce9055baULL, 0x4e1d8a4db0054798ULL,
        0x0b235226e2cade32ULL, 0x2588732c1476b315ULL, 0x16a378750ba8ac68ULL,
        0xba0b116c04448731ULL, 0x4dd02bd47694c2f1ULL, 0x16d6797b218b6b25ULL,
        0x769eb3709cfbf936ULL, 0x197746a0ce396f38ULL, 0x7d17ad8465961d6eULL,
        0xfe58f4998ae19bb4ULL, 0x36df24305233ce69ULL, 0xb88a4eb008f4ee72ULL,
        0x302b2eb923334787ULL, 0x15a4e3edbe13d448ULL, 0x39a4bf64dd7730ceULL,
        0xedf25421b31090c4ULL, 0x4d547fc131be3b69ULL, 0x2b316e120ca3b90eULL,
        0x0faf2357bf18a169ULL, 0x71f34b54ee2c1d62ULL, 0x18eaf6e5c93a3824ULL,
        0x7e168ba03c1b4c18ULL, 0x1a534dd586d9e871ULL, 0xa2cccd307f5f8c38ULL,
        0x2999a6fb4dce30f6ULL, 0x8f6d3b02c1d549a6ULL, 0x5cf7f90d817aac5aULL,
        0xd2a4ceefe66c8170ULL, 0x11560edc4ca959feULL, 0x89e517e6f0dc464dULL,
        0x75bb8972dddd2085ULL, 0x13859ed1e459d65aULL, 0x057114653326fa84ULL,
        0xe2e6f465173cc86cULL, 0x0ada4076497d7de4ULL, 0xa856fa10ec6dbf8aULL,
        0x41505d9a7c25d875ULL, 0x3091b6278382eccdULL, 0x055737185b2c3f13ULL,
        0x2f4df8ecd6f9c632ULL, 0x0633e89c33552d98ULL, 0xf7673724d16db440ULL,
        0x7331bd08e636c391ULL, 0x0252f29672fee426ULL, 0x1fc384946b6b9ddeULL,
        0x03460c12c901443aULL, 0x003a0792e10abcdaULL, 0x8dbec31f624e37d0ULL,
        0x667420d5bfe4dcbeULL, 0xfbfa30e874ed7641ULL, 0x46d1ae14db7ecef6ULL,
        0x216bd7e8f5448768ULL, 0x32bcd40d3d69cc88ULL, 0x2e991dbc39b65abeULL,
        0x0e8fb123a502f553ULL, 0x3d2d486b2c7560c0ULL, 0x09aba1db3079fe03ULL,
        0xcb540c59398c9bceULL, 0x363970e5339ed600ULL, 0x2caee457c28af00eULL,
        0x005e7d7ee47f41a0ULL, 0x69fad3eb10f44100ULL, 0x048109388c75beb3ULL,
        0x253dddf96c7a6fb8ULL, 0x4c47f705b9d47d09ULL, 0x6cec894228b5e978ULL,
        0x04044bb9f8ff45c2ULL, 0x079e75704d775caeULL, 0x073bd54d2a9e2c33ULL,
        0xcec7289270a364fbULL, 0x19e7486f19cd9e4eULL, 0xb50ac15b86b76608ULL,
        0x0620cf81f165c812ULL, 0x63eaaf13be7b11d4ULL, 0x0e0cf831948248c2ULL,
        0xf0412df8f46e7957ULL, 0x671c1fe752517e3fULL, 0x8841bfb04dd3f540ULL,
        0x122de4142249f353ULL, 0x40a4959fb0e76870ULL, 0x25cfd3d4b4bbc459ULL,
        0x78a07c82930c60d0ULL, 0x12c2de24d4cbc969ULL, 0x85d44866096ad7f4ULL,
        0x1fd917ca66b2007bULL, 0x01fbbb0751764764ULL, 0x3d2a4953c6fe0fdcULL,
        0xcc1489c5737afd94ULL, 0x1817c5b6a5346f41ULL, 0xe605a6a7e9985644ULL,
        0x3c50412328ff1946ULL, 0xd8c7fd65817f1291ULL, 0x0bd66975ab66339bULL,
        0x2baf8fa1c7d10fa9ULL, 0x24abdf06ddef848dULL, 0x14df0c9b2ea4f6c2ULL,
        0x2be950edfd2cb1f7ULL, 0x21911e21094178b6ULL, 0x0fa54d518a93b379ULL,
        0xb52508e0ac01ab42ULL, 0x0e035b5fd8cb79beULL, 0x1c1c6d1a3b3c8648ULL,
        0x286037b42ea9871cULL, 0xfe67bf311e48a340ULL, 0x02324131e932a472ULL,
        0x2486dc2dd919e2deULL, 0x008aec7f1da1d2ebULL, 0x63269ba0e8d3eb3aULL,
        0x23c0f11154adb62fULL, 0xc6052393ecd4c018ULL, 0x523585b7d2f5b9fcULL,
        0xf7e6f8c1e87564c9ULL, 0x09eb9fe5dd32c1a3ULL, 0x4d4f86886e055472ULL,
        0x67ea17b58a37966bULL, 0x3d3ce8c23b1ed1a8ULL, 0x0df97c5ac48857ceULL,
        0x9b6992623759eb12ULL, 0x275aa9551ae091f2ULL, 0x08855e19ac5e62e5ULL,
        0x1155fffe0ae083ccULL, 0xbc9c78db7c570240ULL, 0x074560c447dd2418ULL,
        0x3bf78d330bcf1e70ULL, 0x49867cd4b7ed134bULL, 0x8e6eee0cb4470accULL,
        0x1dabafdf59233dd6ULL, 0xea3a50d844fc3fb8ULL, 0x4f03f4454764cb87ULL,
        0x1f2f41cc36c9e6ecULL, 0x53cba4df42963441ULL, 0x10883b70a88d91fbULL,
        0x62b1fc77d4eb9481ULL, 0x893d8f2604b362e1ULL, 0x0933b7855368b440ULL,
        0x9351b545703b2fceULL, 0x59c1d489b9bdd3b4ULL, 0xe72a9c4311417b18ULL,
        0x5355df77e88eb226ULL, 0xe802c37aa963d7e1ULL, 0x381c3747bd6c3bc3ULL,
        0x378565573444258cULL, 0x37848b1e52b43c18ULL, 0x5da2cd32bdce12b6ULL,
        0x13166c5da615f6fdULL, 0xa51ef95efcc66ac8ULL, 0x640c95e473f1e541ULL,
        0x6ec68def1f217500ULL, 0x49ce3543c76a4079ULL, 0x5fc6fd3cddc706b5ULL,
        0x05c3c0f0f6a1fb0dULL, 0xe7820c0996ad1bddULL, 0x21f0d752a088f35cULL,
        0x755405b51d6fc4a0ULL, 0x7ec7649ca4b0e351ULL, 0x3d2b6a46a251f790ULL,
        0x23e1176b19f418adULL, 0x06056575efe8ac05ULL, 0x0f75981b6966e477ULL,
        0x06e87ec41ad437e4ULL, 0x43f6c255d5e1cb84ULL, 0xe4e67d1120ceb580ULL,
        0x2cd67b9e12c26d7bULL, 0xcd00b5ff7fd187f1ULL, 0x3f6cd40accdc4106ULL,
        0x3e895c835459b330ULL, 0x0814d53a217c0850ULL, 0xc9111fe78bc3a62dULL,
        0x719967e351473204ULL, 0xe757707d24282aa4ULL, 0x7226b7f5607f98e6ULL,
        0x7b268ffae3c08d96ULL, 0x16d3917c8b86020eULL, 0x5128bca51c49ea64ULL,
        0x345ffea02bb1698dULL, 0x9460f5111fe4fbc8ULL, 0x60dd1aa5762852cbULL,
        0xbb7440ed3c81667cULL, 0x0a4b12affa7f6f5cULL, 0x95cbcb0ae03861b6ULL,
        0x07ab3b0591db6070ULL, 0xc6476a4c3de78982ULL, 0x204e82e8623ad725ULL,
        0x569a5b4e8ac2a5ccULL, 0x425a1d77d72ebae2ULL, 0xcdaad5551ab33830ULL,
        0x0b7c68fd8422939eULL, 0x46d9a01f53ec3020ULL, 0x102871edbb29e852ULL,
        0x7a8e8084039075a5ULL, 0x40eaede8615e376aULL, 0x4dc67d757a1c751fULL,
        0x1176ef33063f9145ULL, 0x4ea230285b1c8156ULL, 0x6b2aa46ce0027392ULL,
        0x32b13230fba1b068ULL, 0x0e69796851bb984fULL, 0xb749f4542db698c0ULL,
        0x19ad0241ffffd49cULL, 0x2f41e92ef6caff52ULL, 0x4d0b068576747439ULL,
        0x14d607aef7463e00ULL, 0x1443d00d85fb440eULL, 0x529b43bf68688780ULL,
        0x21133a6bc3a3e378ULL, 0x865b6436dae0e7e5ULL, 0x6b4fe83dc1d6defcULL,
        0x03a5858a0ca0be46ULL, 0x1e841b187e67f312ULL, 0x61ee22ef40a66940ULL,
        0x0494bd2e9e741ef8ULL, 0x4eb59e323010e72cULL, 0x19f2abcfb749810eULL,
        0xb30f1e4f994ef9bcULL, 0x53cf6cdd51bd2d96ULL, 0x263943036497a514ULL,
        0x0d4b52170aa2edbaULL, 0x0c4758a1c7b4f758ULL, 0x178dadb1b502b51aULL,
        0x1ddbb20a602eb57aULL, 0x1fc2e2564a9f27fdULL, 0xd5f8c50a0e3d6f90ULL,
        0x0081da3bbe72ac09ULL, 0xcf140d002ccdb200ULL, 0x0ae8389f09b017feULL,
        0x17cc9ffdc03f4440ULL, 0x04eb921d704bcdddULL, 0x139a0ce4cdc521abULL,
        0x0bfce00c145cb0f0ULL, 0x99925ff132eff707ULL, 0x063f6e5da50c3d35ULL,
        0xa0c25dea3f0e6e29ULL, 0x0c7a9048cc8e040fULL,
    };

    const size_t padded = RoundUpTo(kCLMulNum, N);
    auto expected_lower = AllocateAligned<T>(padded);
    auto expected_upper = AllocateAligned<T>(padded);
    HWY_ASSERT(expected_lower && expected_upper);
    CopyBytes<kCLMulNum * sizeof(T)>(kCLMulLower, expected_lower.get());
    CopyBytes<kCLMulNum * sizeof(T)>(kCLMulUpper, expected_upper.get());
    const size_t padding_size = (padded - kCLMulNum) * sizeof(T);
    memset(expected_lower.get() + kCLMulNum, 0, padding_size);
    memset(expected_upper.get() + kCLMulNum, 0, padding_size);

    // Random inputs in each lane
    RandomState rng;
    for (size_t rep = 0; rep < kCLMulNum / N; ++rep) {
      for (size_t i = 0; i < N; ++i) {
        in1[i] = Random64(&rng);
        in2[i] = Random64(&rng);
      }

      const auto a = Load(d, in1.get());
      const auto b = Load(d, in2.get());
#if HWY_PRINT_CLMUL_GOLDEN
      Store(CLMulLower(a, b), d, expected_lower.get() + rep * N);
      Store(CLMulUpper(a, b), d, expected_upper.get() + rep * N);
#else
      HWY_ASSERT_VEC_EQ(d, expected_lower.get() + rep * N, CLMulLower(a, b));
      HWY_ASSERT_VEC_EQ(d, expected_upper.get() + rep * N, CLMulUpper(a, b));
#endif
    }

#if HWY_PRINT_CLMUL_GOLDEN
    // RVV lacks PRIu64, so print 32-bit halves.
    for (size_t i = 0; i < kCLMulNum; ++i) {
      printf("0x%08x%08xULL,", static_cast<uint32_t>(expected_lower[i] >> 32),
             static_cast<uint32_t>(expected_lower[i] & 0xFFFFFFFFU));
    }
    printf("\n");
    for (size_t i = 0; i < kCLMulNum; ++i) {
      printf("0x%08x%08xULL,", static_cast<uint32_t>(expected_upper[i] >> 32),
             static_cast<uint32_t>(expected_upper[i] & 0xFFFFFFFFU));
    }
#endif  // HWY_PRINT_CLMUL_GOLDEN
#else
    (void)d;
#endif
  }
};

HWY_NOINLINE void TestAllCLMul() { ForGEVectors<128, TestCLMul>()(uint64_t()); }

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyCryptoTest);
HWY_EXPORT_AND_TEST_P(HwyCryptoTest, TestAllAES);
HWY_EXPORT_AND_TEST_P(HwyCryptoTest, TestAllAESInverse);
HWY_EXPORT_AND_TEST_P(HwyCryptoTest, TestAllCLMul);
HWY_EXPORT_AND_TEST_P(HwyCryptoTest, TestAllAESKeyGenAssist);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
