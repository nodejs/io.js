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

#include <stddef.h>
#include <stdint.h>

#include "hwy/base.h"

// Per-target include guard
// clang-format off
#if defined(HIGHWAY_HWY_CONTRIB_BIT_PACK_INL_H_) == defined(HWY_TARGET_TOGGLE)  // NOLINT
// clang-format on
#ifdef HIGHWAY_HWY_CONTRIB_BIT_PACK_INL_H_
#undef HIGHWAY_HWY_CONTRIB_BIT_PACK_INL_H_
#else
#define HIGHWAY_HWY_CONTRIB_BIT_PACK_INL_H_
#endif

#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

// The entry points are class templates specialized below for each number of
// bits. Each provides Pack and Unpack member functions which load (Pack) or
// store (Unpack) B raw vectors, and store (Pack) or load (Unpack) a number of
// packed vectors equal to kBits. B denotes the bits per lane: 8 for Pack8, 16
// for Pack16, 32 for Pack32 which is also the upper bound for kBits.
template <size_t kBits>  // <= 8
struct Pack8 {};
template <size_t kBits>  // <= 16
struct Pack16 {};

template <>
struct Pack8<1> {
  template <class D8>
  HWY_INLINE void Pack(D8 d8, const uint8_t* HWY_RESTRICT raw,
                       uint8_t* HWY_RESTRICT packed_out) const {
    const RepartitionToWide<decltype(d8)> d16;
    using VU16 = Vec<decltype(d16)>;
    const size_t N8 = Lanes(d8);
    // 16-bit shifts avoid masking (bits will not cross 8-bit lanes).
    const VU16 raw0 = BitCast(d16, LoadU(d8, raw + 0 * N8));
    const VU16 raw1 = BitCast(d16, LoadU(d8, raw + 1 * N8));
    const VU16 raw2 = BitCast(d16, LoadU(d8, raw + 2 * N8));
    const VU16 raw3 = BitCast(d16, LoadU(d8, raw + 3 * N8));
    const VU16 raw4 = BitCast(d16, LoadU(d8, raw + 4 * N8));
    const VU16 raw5 = BitCast(d16, LoadU(d8, raw + 5 * N8));
    const VU16 raw6 = BitCast(d16, LoadU(d8, raw + 6 * N8));
    const VU16 raw7 = BitCast(d16, LoadU(d8, raw + 7 * N8));

    const VU16 packed =
        Xor3(Or(ShiftLeft<7>(raw7), ShiftLeft<6>(raw6)),
             Xor3(ShiftLeft<5>(raw5), ShiftLeft<4>(raw4), ShiftLeft<3>(raw3)),
             Xor3(ShiftLeft<2>(raw2), ShiftLeft<1>(raw1), raw0));
    StoreU(BitCast(d8, packed), d8, packed_out);
  }

  template <class D8>
  HWY_INLINE void Unpack(D8 d8, const uint8_t* HWY_RESTRICT packed_in,
                         uint8_t* HWY_RESTRICT raw) const {
    const RepartitionToWide<decltype(d8)> d16;
    using VU16 = Vec<decltype(d16)>;
    const size_t N8 = Lanes(d8);
    const VU16 mask = Set(d16, 0x0101u);  // LSB in each byte

    const VU16 packed = BitCast(d16, LoadU(d8, packed_in));

    const VU16 raw0 = And(packed, mask);
    StoreU(BitCast(d8, raw0), d8, raw + 0 * N8);

    const VU16 raw1 = And(ShiftRight<1>(packed), mask);
    StoreU(BitCast(d8, raw1), d8, raw + 1 * N8);

    const VU16 raw2 = And(ShiftRight<2>(packed), mask);
    StoreU(BitCast(d8, raw2), d8, raw + 2 * N8);

    const VU16 raw3 = And(ShiftRight<3>(packed), mask);
    StoreU(BitCast(d8, raw3), d8, raw + 3 * N8);

    const VU16 raw4 = And(ShiftRight<4>(packed), mask);
    StoreU(BitCast(d8, raw4), d8, raw + 4 * N8);

    const VU16 raw5 = And(ShiftRight<5>(packed), mask);
    StoreU(BitCast(d8, raw5), d8, raw + 5 * N8);

    const VU16 raw6 = And(ShiftRight<6>(packed), mask);
    StoreU(BitCast(d8, raw6), d8, raw + 6 * N8);

    const VU16 raw7 = And(ShiftRight<7>(packed), mask);
    StoreU(BitCast(d8, raw7), d8, raw + 7 * N8);
  }
};  // Pack8<1>

template <>
struct Pack8<2> {
  template <class D8>
  HWY_INLINE void Pack(D8 d8, const uint8_t* HWY_RESTRICT raw,
                       uint8_t* HWY_RESTRICT packed_out) const {
    const RepartitionToWide<decltype(d8)> d16;
    using VU16 = Vec<decltype(d16)>;
    const size_t N8 = Lanes(d8);
    // 16-bit shifts avoid masking (bits will not cross 8-bit lanes).
    const VU16 raw0 = BitCast(d16, LoadU(d8, raw + 0 * N8));
    const VU16 raw1 = BitCast(d16, LoadU(d8, raw + 1 * N8));
    const VU16 raw2 = BitCast(d16, LoadU(d8, raw + 2 * N8));
    const VU16 raw3 = BitCast(d16, LoadU(d8, raw + 3 * N8));
    const VU16 raw4 = BitCast(d16, LoadU(d8, raw + 4 * N8));
    const VU16 raw5 = BitCast(d16, LoadU(d8, raw + 5 * N8));
    const VU16 raw6 = BitCast(d16, LoadU(d8, raw + 6 * N8));
    const VU16 raw7 = BitCast(d16, LoadU(d8, raw + 7 * N8));

    const VU16 packed0 = Xor3(ShiftLeft<6>(raw6), ShiftLeft<4>(raw4),
                              Or(ShiftLeft<2>(raw2), raw0));
    const VU16 packed1 = Xor3(ShiftLeft<6>(raw7), ShiftLeft<4>(raw5),
                              Or(ShiftLeft<2>(raw3), raw1));
    StoreU(BitCast(d8, packed0), d8, packed_out + 0 * N8);
    StoreU(BitCast(d8, packed1), d8, packed_out + 1 * N8);
  }

  template <class D8>
  HWY_INLINE void Unpack(D8 d8, const uint8_t* HWY_RESTRICT packed_in,
                         uint8_t* HWY_RESTRICT raw) const {
    const RepartitionToWide<decltype(d8)> d16;
    using VU16 = Vec<decltype(d16)>;
    const size_t N8 = Lanes(d8);
    const VU16 mask = Set(d16, 0x0303u);  // Lowest 2 bits per byte

    const VU16 packed0 = BitCast(d16, LoadU(d8, packed_in + 0 * N8));
    const VU16 packed1 = BitCast(d16, LoadU(d8, packed_in + 1 * N8));

    const VU16 raw0 = And(packed0, mask);
    StoreU(BitCast(d8, raw0), d8, raw + 0 * N8);

    const VU16 raw1 = And(packed1, mask);
    StoreU(BitCast(d8, raw1), d8, raw + 1 * N8);

    const VU16 raw2 = And(ShiftRight<2>(packed0), mask);
    StoreU(BitCast(d8, raw2), d8, raw + 2 * N8);

    const VU16 raw3 = And(ShiftRight<2>(packed1), mask);
    StoreU(BitCast(d8, raw3), d8, raw + 3 * N8);

    const VU16 raw4 = And(ShiftRight<4>(packed0), mask);
    StoreU(BitCast(d8, raw4), d8, raw + 4 * N8);

    const VU16 raw5 = And(ShiftRight<4>(packed1), mask);
    StoreU(BitCast(d8, raw5), d8, raw + 5 * N8);

    const VU16 raw6 = And(ShiftRight<6>(packed0), mask);
    StoreU(BitCast(d8, raw6), d8, raw + 6 * N8);

    const VU16 raw7 = And(ShiftRight<6>(packed1), mask);
    StoreU(BitCast(d8, raw7), d8, raw + 7 * N8);
  }
};  // Pack8<2>

template <>
struct Pack8<3> {
  template <class D8>
  HWY_INLINE void Pack(D8 d8, const uint8_t* HWY_RESTRICT raw,
                       uint8_t* HWY_RESTRICT packed_out) const {
    const RepartitionToWide<decltype(d8)> d16;
    using VU16 = Vec<decltype(d16)>;
    const size_t N8 = Lanes(d8);
    const VU16 raw0 = BitCast(d16, LoadU(d8, raw + 0 * N8));
    const VU16 raw1 = BitCast(d16, LoadU(d8, raw + 1 * N8));
    const VU16 raw2 = BitCast(d16, LoadU(d8, raw + 2 * N8));
    const VU16 raw3 = BitCast(d16, LoadU(d8, raw + 3 * N8));
    const VU16 raw4 = BitCast(d16, LoadU(d8, raw + 4 * N8));
    const VU16 raw5 = BitCast(d16, LoadU(d8, raw + 5 * N8));
    const VU16 raw6 = BitCast(d16, LoadU(d8, raw + 6 * N8));
    const VU16 raw7 = BitCast(d16, LoadU(d8, raw + 7 * N8));

    // The upper two bits of these three will be filled with packed3 (6 bits).
    VU16 packed0 = Or(ShiftLeft<3>(raw4), raw0);
    VU16 packed1 = Or(ShiftLeft<3>(raw5), raw1);
    VU16 packed2 = Or(ShiftLeft<3>(raw6), raw2);
    const VU16 packed3 = Or(ShiftLeft<3>(raw7), raw3);

    const VU16 hi2 = Set(d16, 0xC0C0u);
    packed0 = OrAnd(packed0, ShiftLeft<2>(packed3), hi2);
    packed1 = OrAnd(packed1, ShiftLeft<4>(packed3), hi2);
    packed2 = OrAnd(packed2, ShiftLeft<6>(packed3), hi2);
    StoreU(BitCast(d8, packed0), d8, packed_out + 0 * N8);
    StoreU(BitCast(d8, packed1), d8, packed_out + 1 * N8);
    StoreU(BitCast(d8, packed2), d8, packed_out + 2 * N8);
  }

  template <class D8>
  HWY_INLINE void Unpack(D8 d8, const uint8_t* HWY_RESTRICT packed_in,
                         uint8_t* HWY_RESTRICT raw) const {
    const RepartitionToWide<decltype(d8)> d16;
    using VU16 = Vec<decltype(d16)>;
    const size_t N8 = Lanes(d8);
    const VU16 mask = Set(d16, 0x0707u);  // Lowest 3 bits per byte

    const VU16 packed0 = BitCast(d16, LoadU(d8, packed_in + 0 * N8));
    const VU16 packed1 = BitCast(d16, LoadU(d8, packed_in + 1 * N8));
    const VU16 packed2 = BitCast(d16, LoadU(d8, packed_in + 2 * N8));

    const VU16 raw0 = And(packed0, mask);
    StoreU(BitCast(d8, raw0), d8, raw + 0 * N8);

    const VU16 raw1 = And(packed1, mask);
    StoreU(BitCast(d8, raw1), d8, raw + 1 * N8);

    const VU16 raw2 = And(packed2, mask);
    StoreU(BitCast(d8, raw2), d8, raw + 2 * N8);

    const VU16 raw4 = And(ShiftRight<3>(packed0), mask);
    StoreU(BitCast(d8, raw4), d8, raw + 4 * N8);

    const VU16 raw5 = And(ShiftRight<3>(packed1), mask);
    StoreU(BitCast(d8, raw5), d8, raw + 5 * N8);

    const VU16 raw6 = And(ShiftRight<3>(packed2), mask);
    StoreU(BitCast(d8, raw6), d8, raw + 6 * N8);

    // raw73 is the concatenation of the upper two bits in packed0..2.
    const VU16 hi2 = Set(d16, 0xC0C0u);
    const VU16 raw73 = Xor3(ShiftRight<6>(And(packed2, hi2)),  //
                            ShiftRight<4>(And(packed1, hi2)),
                            ShiftRight<2>(And(packed0, hi2)));

    const VU16 raw3 = And(mask, raw73);
    StoreU(BitCast(d8, raw3), d8, raw + 3 * N8);

    const VU16 raw7 = And(mask, ShiftRight<3>(raw73));
    StoreU(BitCast(d8, raw7), d8, raw + 7 * N8);
  }
};  // Pack8<3>

template <>
struct Pack8<4> {
  template <class D8>
  HWY_INLINE void Pack(D8 d8, const uint8_t* HWY_RESTRICT raw,
                       uint8_t* HWY_RESTRICT packed_out) const {
    const RepartitionToWide<decltype(d8)> d16;
    using VU16 = Vec<decltype(d16)>;
    const size_t N8 = Lanes(d8);
    // 16-bit shifts avoid masking (bits will not cross 8-bit lanes).
    const VU16 raw0 = BitCast(d16, LoadU(d8, raw + 0 * N8));
    const VU16 raw1 = BitCast(d16, LoadU(d8, raw + 1 * N8));
    const VU16 raw2 = BitCast(d16, LoadU(d8, raw + 2 * N8));
    const VU16 raw3 = BitCast(d16, LoadU(d8, raw + 3 * N8));
    const VU16 raw4 = BitCast(d16, LoadU(d8, raw + 4 * N8));
    const VU16 raw5 = BitCast(d16, LoadU(d8, raw + 5 * N8));
    const VU16 raw6 = BitCast(d16, LoadU(d8, raw + 6 * N8));
    const VU16 raw7 = BitCast(d16, LoadU(d8, raw + 7 * N8));

    const VU16 packed0 = Or(ShiftLeft<4>(raw2), raw0);
    const VU16 packed1 = Or(ShiftLeft<4>(raw3), raw1);
    const VU16 packed2 = Or(ShiftLeft<4>(raw6), raw4);
    const VU16 packed3 = Or(ShiftLeft<4>(raw7), raw5);

    StoreU(BitCast(d8, packed0), d8, packed_out + 0 * N8);
    StoreU(BitCast(d8, packed1), d8, packed_out + 1 * N8);
    StoreU(BitCast(d8, packed2), d8, packed_out + 2 * N8);
    StoreU(BitCast(d8, packed3), d8, packed_out + 3 * N8);
  }

  template <class D8>
  HWY_INLINE void Unpack(D8 d8, const uint8_t* HWY_RESTRICT packed_in,
                         uint8_t* HWY_RESTRICT raw) const {
    const RepartitionToWide<decltype(d8)> d16;
    using VU16 = Vec<decltype(d16)>;
    const size_t N8 = Lanes(d8);
    const VU16 mask = Set(d16, 0x0F0Fu);  // Lowest 4 bits per byte

    const VU16 packed0 = BitCast(d16, LoadU(d8, packed_in + 0 * N8));
    const VU16 packed1 = BitCast(d16, LoadU(d8, packed_in + 1 * N8));
    const VU16 packed2 = BitCast(d16, LoadU(d8, packed_in + 2 * N8));
    const VU16 packed3 = BitCast(d16, LoadU(d8, packed_in + 3 * N8));

    const VU16 raw0 = And(packed0, mask);
    StoreU(BitCast(d8, raw0), d8, raw + 0 * N8);

    const VU16 raw1 = And(packed1, mask);
    StoreU(BitCast(d8, raw1), d8, raw + 1 * N8);

    const VU16 raw2 = And(ShiftRight<4>(packed0), mask);
    StoreU(BitCast(d8, raw2), d8, raw + 2 * N8);

    const VU16 raw3 = And(ShiftRight<4>(packed1), mask);
    StoreU(BitCast(d8, raw3), d8, raw + 3 * N8);

    const VU16 raw4 = And(packed2, mask);
    StoreU(BitCast(d8, raw4), d8, raw + 4 * N8);

    const VU16 raw5 = And(packed3, mask);
    StoreU(BitCast(d8, raw5), d8, raw + 5 * N8);

    const VU16 raw6 = And(ShiftRight<4>(packed2), mask);
    StoreU(BitCast(d8, raw6), d8, raw + 6 * N8);

    const VU16 raw7 = And(ShiftRight<4>(packed3), mask);
    StoreU(BitCast(d8, raw7), d8, raw + 7 * N8);
  }
};  // Pack8<4>

template <>
struct Pack8<5> {
  template <class D8>
  HWY_INLINE void Pack(D8 d8, const uint8_t* HWY_RESTRICT raw,
                       uint8_t* HWY_RESTRICT packed_out) const {
    const RepartitionToWide<decltype(d8)> d16;
    using VU16 = Vec<decltype(d16)>;
    const size_t N8 = Lanes(d8);
    const VU16 raw0 = BitCast(d16, LoadU(d8, raw + 0 * N8));
    const VU16 raw1 = BitCast(d16, LoadU(d8, raw + 1 * N8));
    const VU16 raw2 = BitCast(d16, LoadU(d8, raw + 2 * N8));
    const VU16 raw3 = BitCast(d16, LoadU(d8, raw + 3 * N8));
    const VU16 raw4 = BitCast(d16, LoadU(d8, raw + 4 * N8));
    const VU16 raw5 = BitCast(d16, LoadU(d8, raw + 5 * N8));
    const VU16 raw6 = BitCast(d16, LoadU(d8, raw + 6 * N8));
    const VU16 raw7 = BitCast(d16, LoadU(d8, raw + 7 * N8));

    // Fill upper three bits with upper bits from raw4..7.
    const VU16 hi3 = Set(d16, 0xE0E0u);
    const VU16 packed0 = OrAnd(raw0, ShiftLeft<3>(raw4), hi3);
    const VU16 packed1 = OrAnd(raw1, ShiftLeft<3>(raw5), hi3);
    const VU16 packed2 = OrAnd(raw2, ShiftLeft<3>(raw6), hi3);
    const VU16 packed3 = OrAnd(raw3, ShiftLeft<3>(raw7), hi3);

    StoreU(BitCast(d8, packed0), d8, packed_out + 0 * N8);
    StoreU(BitCast(d8, packed1), d8, packed_out + 1 * N8);
    StoreU(BitCast(d8, packed2), d8, packed_out + 2 * N8);
    StoreU(BitCast(d8, packed3), d8, packed_out + 3 * N8);

    // Combine lower two bits of raw4..7 into packed4.
    const VU16 lo2 = Set(d16, 0x0303u);
    const VU16 packed4 = Or(And(raw4, lo2), Xor3(ShiftLeft<2>(And(raw5, lo2)),
                                                 ShiftLeft<4>(And(raw6, lo2)),
                                                 ShiftLeft<6>(And(raw7, lo2))));
    StoreU(BitCast(d8, packed4), d8, packed_out + 4 * N8);
  }

  template <class D8>
  HWY_INLINE void Unpack(D8 d8, const uint8_t* HWY_RESTRICT packed_in,
                         uint8_t* HWY_RESTRICT raw) const {
    const RepartitionToWide<decltype(d8)> d16;
    using VU16 = Vec<decltype(d16)>;
    const size_t N8 = Lanes(d8);

    const VU16 packed0 = BitCast(d16, LoadU(d8, packed_in + 0 * N8));
    const VU16 packed1 = BitCast(d16, LoadU(d8, packed_in + 1 * N8));
    const VU16 packed2 = BitCast(d16, LoadU(d8, packed_in + 2 * N8));
    const VU16 packed3 = BitCast(d16, LoadU(d8, packed_in + 3 * N8));
    const VU16 packed4 = BitCast(d16, LoadU(d8, packed_in + 4 * N8));

    const VU16 mask = Set(d16, 0x1F1Fu);  // Lowest 5 bits per byte

    const VU16 raw0 = And(packed0, mask);
    StoreU(BitCast(d8, raw0), d8, raw + 0 * N8);

    const VU16 raw1 = And(packed1, mask);
    StoreU(BitCast(d8, raw1), d8, raw + 1 * N8);

    const VU16 raw2 = And(packed2, mask);
    StoreU(BitCast(d8, raw2), d8, raw + 2 * N8);

    const VU16 raw3 = And(packed3, mask);
    StoreU(BitCast(d8, raw3), d8, raw + 3 * N8);

    // The upper bits are the top 3 bits shifted right by three.
    const VU16 top4 = ShiftRight<3>(AndNot(mask, packed0));
    const VU16 top5 = ShiftRight<3>(AndNot(mask, packed1));
    const VU16 top6 = ShiftRight<3>(AndNot(mask, packed2));
    const VU16 top7 = ShiftRight<3>(AndNot(mask, packed3));

    // Insert the lower 2 bits, which were concatenated into a byte.
    const VU16 lo2 = Set(d16, 0x0303u);
    const VU16 raw4 = OrAnd(top4, lo2, packed4);
    const VU16 raw5 = OrAnd(top5, lo2, ShiftRight<2>(packed4));
    const VU16 raw6 = OrAnd(top6, lo2, ShiftRight<4>(packed4));
    const VU16 raw7 = OrAnd(top7, lo2, ShiftRight<6>(packed4));

    StoreU(BitCast(d8, raw4), d8, raw + 4 * N8);
    StoreU(BitCast(d8, raw5), d8, raw + 5 * N8);
    StoreU(BitCast(d8, raw6), d8, raw + 6 * N8);
    StoreU(BitCast(d8, raw7), d8, raw + 7 * N8);
  }
};  // Pack8<5>

template <>
struct Pack8<6> {
  template <class D8>
  HWY_INLINE void Pack(D8 d8, const uint8_t* HWY_RESTRICT raw,
                       uint8_t* HWY_RESTRICT packed_out) const {
    const RepartitionToWide<decltype(d8)> d16;
    using VU16 = Vec<decltype(d16)>;
    const size_t N8 = Lanes(d8);
    const VU16 raw0 = BitCast(d16, LoadU(d8, raw + 0 * N8));
    const VU16 raw1 = BitCast(d16, LoadU(d8, raw + 1 * N8));
    const VU16 raw2 = BitCast(d16, LoadU(d8, raw + 2 * N8));
    const VU16 raw3 = BitCast(d16, LoadU(d8, raw + 3 * N8));
    const VU16 raw4 = BitCast(d16, LoadU(d8, raw + 4 * N8));
    const VU16 raw5 = BitCast(d16, LoadU(d8, raw + 5 * N8));
    const VU16 raw6 = BitCast(d16, LoadU(d8, raw + 6 * N8));
    const VU16 raw7 = BitCast(d16, LoadU(d8, raw + 7 * N8));

    const VU16 hi2 = Set(d16, 0xC0C0u);
    // Each triplet of these stores raw3/raw7 (6 bits) in the upper 2 bits.
    const VU16 packed0 = OrAnd(raw0, ShiftLeft<2>(raw3), hi2);
    const VU16 packed1 = OrAnd(raw1, ShiftLeft<4>(raw3), hi2);
    const VU16 packed2 = OrAnd(raw2, ShiftLeft<6>(raw3), hi2);
    const VU16 packed3 = OrAnd(raw4, ShiftLeft<2>(raw7), hi2);
    const VU16 packed4 = OrAnd(raw5, ShiftLeft<4>(raw7), hi2);
    const VU16 packed5 = OrAnd(raw6, ShiftLeft<6>(raw7), hi2);

    StoreU(BitCast(d8, packed0), d8, packed_out + 0 * N8);
    StoreU(BitCast(d8, packed1), d8, packed_out + 1 * N8);
    StoreU(BitCast(d8, packed2), d8, packed_out + 2 * N8);
    StoreU(BitCast(d8, packed3), d8, packed_out + 3 * N8);
    StoreU(BitCast(d8, packed4), d8, packed_out + 4 * N8);
    StoreU(BitCast(d8, packed5), d8, packed_out + 5 * N8);
  }

  template <class D8>
  HWY_INLINE void Unpack(D8 d8, const uint8_t* HWY_RESTRICT packed_in,
                         uint8_t* HWY_RESTRICT raw) const {
    const RepartitionToWide<decltype(d8)> d16;
    using VU16 = Vec<decltype(d16)>;
    const size_t N8 = Lanes(d8);
    const VU16 mask = Set(d16, 0x3F3Fu);  // Lowest 6 bits per byte

    const VU16 packed0 = BitCast(d16, LoadU(d8, packed_in + 0 * N8));
    const VU16 packed1 = BitCast(d16, LoadU(d8, packed_in + 1 * N8));
    const VU16 packed2 = BitCast(d16, LoadU(d8, packed_in + 2 * N8));
    const VU16 packed3 = BitCast(d16, LoadU(d8, packed_in + 3 * N8));
    const VU16 packed4 = BitCast(d16, LoadU(d8, packed_in + 4 * N8));
    const VU16 packed5 = BitCast(d16, LoadU(d8, packed_in + 5 * N8));

    const VU16 raw0 = And(packed0, mask);
    StoreU(BitCast(d8, raw0), d8, raw + 0 * N8);

    const VU16 raw1 = And(packed1, mask);
    StoreU(BitCast(d8, raw1), d8, raw + 1 * N8);

    const VU16 raw2 = And(packed2, mask);
    StoreU(BitCast(d8, raw2), d8, raw + 2 * N8);

    const VU16 raw4 = And(packed3, mask);
    StoreU(BitCast(d8, raw4), d8, raw + 4 * N8);

    const VU16 raw5 = And(packed4, mask);
    StoreU(BitCast(d8, raw5), d8, raw + 5 * N8);

    const VU16 raw6 = And(packed5, mask);
    StoreU(BitCast(d8, raw6), d8, raw + 6 * N8);

    // raw3/7 are the concatenation of the upper two bits in packed0..2.
    const VU16 raw3 = Xor3(ShiftRight<6>(AndNot(mask, packed2)),
                           ShiftRight<4>(AndNot(mask, packed1)),
                           ShiftRight<2>(AndNot(mask, packed0)));
    const VU16 raw7 = Xor3(ShiftRight<6>(AndNot(mask, packed5)),
                           ShiftRight<4>(AndNot(mask, packed4)),
                           ShiftRight<2>(AndNot(mask, packed3)));
    StoreU(BitCast(d8, raw3), d8, raw + 3 * N8);
    StoreU(BitCast(d8, raw7), d8, raw + 7 * N8);
  }
};  // Pack8<6>

template <>
struct Pack8<7> {
  template <class D8>
  HWY_INLINE void Pack(D8 d8, const uint8_t* HWY_RESTRICT raw,
                       uint8_t* HWY_RESTRICT packed_out) const {
    const RepartitionToWide<decltype(d8)> d16;
    using VU16 = Vec<decltype(d16)>;
    const size_t N8 = Lanes(d8);
    const VU16 raw0 = BitCast(d16, LoadU(d8, raw + 0 * N8));
    const VU16 raw1 = BitCast(d16, LoadU(d8, raw + 1 * N8));
    const VU16 raw2 = BitCast(d16, LoadU(d8, raw + 2 * N8));
    const VU16 raw3 = BitCast(d16, LoadU(d8, raw + 3 * N8));
    const VU16 raw4 = BitCast(d16, LoadU(d8, raw + 4 * N8));
    const VU16 raw5 = BitCast(d16, LoadU(d8, raw + 5 * N8));
    const VU16 raw6 = BitCast(d16, LoadU(d8, raw + 6 * N8));
    // Inserted into top bit of packed0..6.
    const VU16 raw7 = BitCast(d16, LoadU(d8, raw + 7 * N8));

    const VU16 hi1 = Set(d16, 0x8080u);
    const VU16 packed0 = OrAnd(raw0, Add(raw7, raw7), hi1);
    const VU16 packed1 = OrAnd(raw1, ShiftLeft<2>(raw7), hi1);
    const VU16 packed2 = OrAnd(raw2, ShiftLeft<3>(raw7), hi1);
    const VU16 packed3 = OrAnd(raw3, ShiftLeft<4>(raw7), hi1);
    const VU16 packed4 = OrAnd(raw4, ShiftLeft<5>(raw7), hi1);
    const VU16 packed5 = OrAnd(raw5, ShiftLeft<6>(raw7), hi1);
    const VU16 packed6 = OrAnd(raw6, ShiftLeft<7>(raw7), hi1);

    StoreU(BitCast(d8, packed0), d8, packed_out + 0 * N8);
    StoreU(BitCast(d8, packed1), d8, packed_out + 1 * N8);
    StoreU(BitCast(d8, packed2), d8, packed_out + 2 * N8);
    StoreU(BitCast(d8, packed3), d8, packed_out + 3 * N8);
    StoreU(BitCast(d8, packed4), d8, packed_out + 4 * N8);
    StoreU(BitCast(d8, packed5), d8, packed_out + 5 * N8);
    StoreU(BitCast(d8, packed6), d8, packed_out + 6 * N8);
  }

  template <class D8>
  HWY_INLINE void Unpack(D8 d8, const uint8_t* HWY_RESTRICT packed_in,
                         uint8_t* HWY_RESTRICT raw) const {
    const RepartitionToWide<decltype(d8)> d16;
    using VU16 = Vec<decltype(d16)>;
    const size_t N8 = Lanes(d8);

    const VU16 packed0 = BitCast(d16, LoadU(d8, packed_in + 0 * N8));
    const VU16 packed1 = BitCast(d16, LoadU(d8, packed_in + 1 * N8));
    const VU16 packed2 = BitCast(d16, LoadU(d8, packed_in + 2 * N8));
    const VU16 packed3 = BitCast(d16, LoadU(d8, packed_in + 3 * N8));
    const VU16 packed4 = BitCast(d16, LoadU(d8, packed_in + 4 * N8));
    const VU16 packed5 = BitCast(d16, LoadU(d8, packed_in + 5 * N8));
    const VU16 packed6 = BitCast(d16, LoadU(d8, packed_in + 6 * N8));

    const VU16 mask = Set(d16, 0x7F7Fu);  // Lowest 7 bits per byte

    const VU16 raw0 = And(packed0, mask);
    StoreU(BitCast(d8, raw0), d8, raw + 0 * N8);

    const VU16 raw1 = And(packed1, mask);
    StoreU(BitCast(d8, raw1), d8, raw + 1 * N8);

    const VU16 raw2 = And(packed2, mask);
    StoreU(BitCast(d8, raw2), d8, raw + 2 * N8);

    const VU16 raw3 = And(packed3, mask);
    StoreU(BitCast(d8, raw3), d8, raw + 3 * N8);

    const VU16 raw4 = And(packed4, mask);
    StoreU(BitCast(d8, raw4), d8, raw + 4 * N8);

    const VU16 raw5 = And(packed5, mask);
    StoreU(BitCast(d8, raw5), d8, raw + 5 * N8);

    const VU16 raw6 = And(packed6, mask);
    StoreU(BitCast(d8, raw6), d8, raw + 6 * N8);

    const VU16 p0 = Xor3(ShiftRight<7>(AndNot(mask, packed6)),
                         ShiftRight<6>(AndNot(mask, packed5)),
                         ShiftRight<5>(AndNot(mask, packed4)));
    const VU16 p1 = Xor3(ShiftRight<4>(AndNot(mask, packed3)),
                         ShiftRight<3>(AndNot(mask, packed2)),
                         ShiftRight<2>(AndNot(mask, packed1)));
    const VU16 raw7 = Xor3(ShiftRight<1>(AndNot(mask, packed0)), p0, p1);
    StoreU(BitCast(d8, raw7), d8, raw + 7 * N8);
  }
};  // Pack8<7>

template <>
struct Pack8<8> {
  template <class D8>
  HWY_INLINE void Pack(D8 d8, const uint8_t* HWY_RESTRICT raw,
                       uint8_t* HWY_RESTRICT packed_out) const {
    using VU8 = Vec<decltype(d8)>;
    const size_t N8 = Lanes(d8);
    const VU8 raw0 = LoadU(d8, raw + 0 * N8);
    const VU8 raw1 = LoadU(d8, raw + 1 * N8);
    const VU8 raw2 = LoadU(d8, raw + 2 * N8);
    const VU8 raw3 = LoadU(d8, raw + 3 * N8);
    const VU8 raw4 = LoadU(d8, raw + 4 * N8);
    const VU8 raw5 = LoadU(d8, raw + 5 * N8);
    const VU8 raw6 = LoadU(d8, raw + 6 * N8);
    const VU8 raw7 = LoadU(d8, raw + 7 * N8);

    StoreU(raw0, d8, packed_out + 0 * N8);
    StoreU(raw1, d8, packed_out + 1 * N8);
    StoreU(raw2, d8, packed_out + 2 * N8);
    StoreU(raw3, d8, packed_out + 3 * N8);
    StoreU(raw4, d8, packed_out + 4 * N8);
    StoreU(raw5, d8, packed_out + 5 * N8);
    StoreU(raw6, d8, packed_out + 6 * N8);
    StoreU(raw7, d8, packed_out + 7 * N8);
  }

  template <class D8>
  HWY_INLINE void Unpack(D8 d8, const uint8_t* HWY_RESTRICT packed_in,
                         uint8_t* HWY_RESTRICT raw) const {
    using VU8 = Vec<decltype(d8)>;
    const size_t N8 = Lanes(d8);
    const VU8 raw0 = LoadU(d8, packed_in + 0 * N8);
    const VU8 raw1 = LoadU(d8, packed_in + 1 * N8);
    const VU8 raw2 = LoadU(d8, packed_in + 2 * N8);
    const VU8 raw3 = LoadU(d8, packed_in + 3 * N8);
    const VU8 raw4 = LoadU(d8, packed_in + 4 * N8);
    const VU8 raw5 = LoadU(d8, packed_in + 5 * N8);
    const VU8 raw6 = LoadU(d8, packed_in + 6 * N8);
    const VU8 raw7 = LoadU(d8, packed_in + 7 * N8);

    StoreU(raw0, d8, raw + 0 * N8);
    StoreU(raw1, d8, raw + 1 * N8);
    StoreU(raw2, d8, raw + 2 * N8);
    StoreU(raw3, d8, raw + 3 * N8);
    StoreU(raw4, d8, raw + 4 * N8);
    StoreU(raw5, d8, raw + 5 * N8);
    StoreU(raw6, d8, raw + 6 * N8);
    StoreU(raw7, d8, raw + 7 * N8);
  }
};  // Pack8<8>

template <>
struct Pack16<1> {
  template <class D>
  HWY_INLINE void Pack(D d, const uint16_t* HWY_RESTRICT raw,
                       uint16_t* HWY_RESTRICT packed_out) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 raw0 = LoadU(d, raw + 0 * N);
    const VU16 raw1 = LoadU(d, raw + 1 * N);
    const VU16 raw2 = LoadU(d, raw + 2 * N);
    const VU16 raw3 = LoadU(d, raw + 3 * N);
    const VU16 raw4 = LoadU(d, raw + 4 * N);
    const VU16 raw5 = LoadU(d, raw + 5 * N);
    const VU16 raw6 = LoadU(d, raw + 6 * N);
    const VU16 raw7 = LoadU(d, raw + 7 * N);
    const VU16 raw8 = LoadU(d, raw + 8 * N);
    const VU16 raw9 = LoadU(d, raw + 9 * N);
    const VU16 rawA = LoadU(d, raw + 0xA * N);
    const VU16 rawB = LoadU(d, raw + 0xB * N);
    const VU16 rawC = LoadU(d, raw + 0xC * N);
    const VU16 rawD = LoadU(d, raw + 0xD * N);
    const VU16 rawE = LoadU(d, raw + 0xE * N);
    const VU16 rawF = LoadU(d, raw + 0xF * N);

    const VU16 p0 = Xor3(ShiftLeft<2>(raw2), Add(raw1, raw1), raw0);
    const VU16 p1 =
        Xor3(ShiftLeft<5>(raw5), ShiftLeft<4>(raw4), ShiftLeft<3>(raw3));
    const VU16 p2 =
        Xor3(ShiftLeft<8>(raw8), ShiftLeft<7>(raw7), ShiftLeft<6>(raw6));
    const VU16 p3 =
        Xor3(ShiftLeft<0xB>(rawB), ShiftLeft<0xA>(rawA), ShiftLeft<9>(raw9));
    const VU16 p4 =
        Xor3(ShiftLeft<0xE>(rawE), ShiftLeft<0xD>(rawD), ShiftLeft<0xC>(rawC));
    const VU16 packed =
        Or(Xor3(ShiftLeft<0xF>(rawF), p0, p1), Xor3(p2, p3, p4));
    StoreU(packed, d, packed_out);
  }

  template <class D>
  HWY_INLINE void Unpack(D d, const uint16_t* HWY_RESTRICT packed_in,
                         uint16_t* HWY_RESTRICT raw) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 mask = Set(d, 1u);  // Lowest bit

    const VU16 packed = LoadU(d, packed_in);

    const VU16 raw0 = And(packed, mask);
    StoreU(raw0, d, raw + 0 * N);

    const VU16 raw1 = And(ShiftRight<1>(packed), mask);
    StoreU(raw1, d, raw + 1 * N);

    const VU16 raw2 = And(ShiftRight<2>(packed), mask);
    StoreU(raw2, d, raw + 2 * N);

    const VU16 raw3 = And(ShiftRight<3>(packed), mask);
    StoreU(raw3, d, raw + 3 * N);

    const VU16 raw4 = And(ShiftRight<4>(packed), mask);
    StoreU(raw4, d, raw + 4 * N);

    const VU16 raw5 = And(ShiftRight<5>(packed), mask);
    StoreU(raw5, d, raw + 5 * N);

    const VU16 raw6 = And(ShiftRight<6>(packed), mask);
    StoreU(raw6, d, raw + 6 * N);

    const VU16 raw7 = And(ShiftRight<7>(packed), mask);
    StoreU(raw7, d, raw + 7 * N);

    const VU16 raw8 = And(ShiftRight<8>(packed), mask);
    StoreU(raw8, d, raw + 8 * N);

    const VU16 raw9 = And(ShiftRight<9>(packed), mask);
    StoreU(raw9, d, raw + 9 * N);

    const VU16 rawA = And(ShiftRight<0xA>(packed), mask);
    StoreU(rawA, d, raw + 0xA * N);

    const VU16 rawB = And(ShiftRight<0xB>(packed), mask);
    StoreU(rawB, d, raw + 0xB * N);

    const VU16 rawC = And(ShiftRight<0xC>(packed), mask);
    StoreU(rawC, d, raw + 0xC * N);

    const VU16 rawD = And(ShiftRight<0xD>(packed), mask);
    StoreU(rawD, d, raw + 0xD * N);

    const VU16 rawE = And(ShiftRight<0xE>(packed), mask);
    StoreU(rawE, d, raw + 0xE * N);

    const VU16 rawF = ShiftRight<0xF>(packed);
    StoreU(rawF, d, raw + 0xF * N);
  }
};  // Pack16<1>

template <>
struct Pack16<2> {
  template <class D>
  HWY_INLINE void Pack(D d, const uint16_t* HWY_RESTRICT raw,
                       uint16_t* HWY_RESTRICT packed_out) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 raw0 = LoadU(d, raw + 0 * N);
    const VU16 raw1 = LoadU(d, raw + 1 * N);
    const VU16 raw2 = LoadU(d, raw + 2 * N);
    const VU16 raw3 = LoadU(d, raw + 3 * N);
    const VU16 raw4 = LoadU(d, raw + 4 * N);
    const VU16 raw5 = LoadU(d, raw + 5 * N);
    const VU16 raw6 = LoadU(d, raw + 6 * N);
    const VU16 raw7 = LoadU(d, raw + 7 * N);
    const VU16 raw8 = LoadU(d, raw + 8 * N);
    const VU16 raw9 = LoadU(d, raw + 9 * N);
    const VU16 rawA = LoadU(d, raw + 0xA * N);
    const VU16 rawB = LoadU(d, raw + 0xB * N);
    const VU16 rawC = LoadU(d, raw + 0xC * N);
    const VU16 rawD = LoadU(d, raw + 0xD * N);
    const VU16 rawE = LoadU(d, raw + 0xE * N);
    const VU16 rawF = LoadU(d, raw + 0xF * N);

    VU16 packed0 = Xor3(ShiftLeft<4>(raw4), ShiftLeft<2>(raw2), raw0);
    VU16 packed1 = Xor3(ShiftLeft<4>(raw5), ShiftLeft<2>(raw3), raw1);
    packed0 = Xor3(packed0, ShiftLeft<8>(raw8), ShiftLeft<6>(raw6));
    packed1 = Xor3(packed1, ShiftLeft<8>(raw9), ShiftLeft<6>(raw7));

    packed0 = Xor3(packed0, ShiftLeft<12>(rawC), ShiftLeft<10>(rawA));
    packed1 = Xor3(packed1, ShiftLeft<12>(rawD), ShiftLeft<10>(rawB));

    packed0 = Or(packed0, ShiftLeft<14>(rawE));
    packed1 = Or(packed1, ShiftLeft<14>(rawF));
    StoreU(packed0, d, packed_out + 0 * N);
    StoreU(packed1, d, packed_out + 1 * N);
  }

  template <class D>
  HWY_INLINE void Unpack(D d, const uint16_t* HWY_RESTRICT packed_in,
                         uint16_t* HWY_RESTRICT raw) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 mask = Set(d, 0x3u);  // Lowest 2 bits

    const VU16 packed0 = LoadU(d, packed_in + 0 * N);
    const VU16 packed1 = LoadU(d, packed_in + 1 * N);

    const VU16 raw0 = And(packed0, mask);
    StoreU(raw0, d, raw + 0 * N);

    const VU16 raw1 = And(packed1, mask);
    StoreU(raw1, d, raw + 1 * N);

    const VU16 raw2 = And(ShiftRight<2>(packed0), mask);
    StoreU(raw2, d, raw + 2 * N);

    const VU16 raw3 = And(ShiftRight<2>(packed1), mask);
    StoreU(raw3, d, raw + 3 * N);

    const VU16 raw4 = And(ShiftRight<4>(packed0), mask);
    StoreU(raw4, d, raw + 4 * N);

    const VU16 raw5 = And(ShiftRight<4>(packed1), mask);
    StoreU(raw5, d, raw + 5 * N);

    const VU16 raw6 = And(ShiftRight<6>(packed0), mask);
    StoreU(raw6, d, raw + 6 * N);

    const VU16 raw7 = And(ShiftRight<6>(packed1), mask);
    StoreU(raw7, d, raw + 7 * N);

    const VU16 raw8 = And(ShiftRight<8>(packed0), mask);
    StoreU(raw8, d, raw + 8 * N);

    const VU16 raw9 = And(ShiftRight<8>(packed1), mask);
    StoreU(raw9, d, raw + 9 * N);

    const VU16 rawA = And(ShiftRight<0xA>(packed0), mask);
    StoreU(rawA, d, raw + 0xA * N);

    const VU16 rawB = And(ShiftRight<0xA>(packed1), mask);
    StoreU(rawB, d, raw + 0xB * N);

    const VU16 rawC = And(ShiftRight<0xC>(packed0), mask);
    StoreU(rawC, d, raw + 0xC * N);

    const VU16 rawD = And(ShiftRight<0xC>(packed1), mask);
    StoreU(rawD, d, raw + 0xD * N);

    const VU16 rawE = ShiftRight<0xE>(packed0);
    StoreU(rawE, d, raw + 0xE * N);

    const VU16 rawF = ShiftRight<0xE>(packed1);
    StoreU(rawF, d, raw + 0xF * N);
  }
};  // Pack16<2>

template <>
struct Pack16<3> {
  template <class D>
  HWY_INLINE void Pack(D d, const uint16_t* HWY_RESTRICT raw,
                       uint16_t* HWY_RESTRICT packed_out) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 raw0 = LoadU(d, raw + 0 * N);
    const VU16 raw1 = LoadU(d, raw + 1 * N);
    const VU16 raw2 = LoadU(d, raw + 2 * N);
    const VU16 raw3 = LoadU(d, raw + 3 * N);
    const VU16 raw4 = LoadU(d, raw + 4 * N);
    const VU16 raw5 = LoadU(d, raw + 5 * N);
    const VU16 raw6 = LoadU(d, raw + 6 * N);
    const VU16 raw7 = LoadU(d, raw + 7 * N);
    const VU16 raw8 = LoadU(d, raw + 8 * N);
    const VU16 raw9 = LoadU(d, raw + 9 * N);
    const VU16 rawA = LoadU(d, raw + 0xA * N);
    const VU16 rawB = LoadU(d, raw + 0xB * N);
    const VU16 rawC = LoadU(d, raw + 0xC * N);
    const VU16 rawD = LoadU(d, raw + 0xD * N);
    const VU16 rawE = LoadU(d, raw + 0xE * N);
    const VU16 rawF = LoadU(d, raw + 0xF * N);

    // We can fit 15 raw vectors in three packed vectors (five each).
    VU16 packed0 = Xor3(ShiftLeft<6>(raw6), ShiftLeft<3>(raw3), raw0);
    VU16 packed1 = Xor3(ShiftLeft<6>(raw7), ShiftLeft<3>(raw4), raw1);
    VU16 packed2 = Xor3(ShiftLeft<6>(raw8), ShiftLeft<3>(raw5), raw2);

    // rawF will be scattered into the upper bit of these three.
    packed0 = Xor3(packed0, ShiftLeft<12>(rawC), ShiftLeft<9>(raw9));
    packed1 = Xor3(packed1, ShiftLeft<12>(rawD), ShiftLeft<9>(rawA));
    packed2 = Xor3(packed2, ShiftLeft<12>(rawE), ShiftLeft<9>(rawB));

    const VU16 hi1 = Set(d, 0x8000u);
    packed0 = Or(packed0, ShiftLeft<15>(rawF));  // MSB only, no mask
    packed1 = OrAnd(packed1, ShiftLeft<14>(rawF), hi1);
    packed2 = OrAnd(packed2, ShiftLeft<13>(rawF), hi1);
    StoreU(packed0, d, packed_out + 0 * N);
    StoreU(packed1, d, packed_out + 1 * N);
    StoreU(packed2, d, packed_out + 2 * N);
  }

  template <class D>
  HWY_INLINE void Unpack(D d, const uint16_t* HWY_RESTRICT packed_in,
                         uint16_t* HWY_RESTRICT raw) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 mask = Set(d, 0x7u);  // Lowest 3 bits

    const VU16 packed0 = LoadU(d, packed_in + 0 * N);
    const VU16 packed1 = LoadU(d, packed_in + 1 * N);
    const VU16 packed2 = LoadU(d, packed_in + 2 * N);

    const VU16 raw0 = And(mask, packed0);
    StoreU(raw0, d, raw + 0 * N);

    const VU16 raw1 = And(mask, packed1);
    StoreU(raw1, d, raw + 1 * N);

    const VU16 raw2 = And(mask, packed2);
    StoreU(raw2, d, raw + 2 * N);

    const VU16 raw3 = And(mask, ShiftRight<3>(packed0));
    StoreU(raw3, d, raw + 3 * N);

    const VU16 raw4 = And(mask, ShiftRight<3>(packed1));
    StoreU(raw4, d, raw + 4 * N);

    const VU16 raw5 = And(mask, ShiftRight<3>(packed2));
    StoreU(raw5, d, raw + 5 * N);

    const VU16 raw6 = And(mask, ShiftRight<6>(packed0));
    StoreU(raw6, d, raw + 6 * N);

    const VU16 raw7 = And(mask, ShiftRight<6>(packed1));
    StoreU(raw7, d, raw + 7 * N);

    const VU16 raw8 = And(mask, ShiftRight<6>(packed2));
    StoreU(raw8, d, raw + 8 * N);

    const VU16 raw9 = And(mask, ShiftRight<9>(packed0));
    StoreU(raw9, d, raw + 9 * N);

    const VU16 rawA = And(mask, ShiftRight<9>(packed1));
    StoreU(rawA, d, raw + 0xA * N);

    const VU16 rawB = And(mask, ShiftRight<9>(packed2));
    StoreU(rawB, d, raw + 0xB * N);

    const VU16 rawC = And(mask, ShiftRight<12>(packed0));
    StoreU(rawC, d, raw + 0xC * N);

    const VU16 rawD = And(mask, ShiftRight<12>(packed1));
    StoreU(rawD, d, raw + 0xD * N);

    const VU16 rawE = And(mask, ShiftRight<12>(packed2));
    StoreU(rawE, d, raw + 0xE * N);

    // rawF is the concatenation of the upper bit of packed0..2.
    const VU16 down0 = ShiftRight<15>(packed0);
    const VU16 down1 = ShiftRight<15>(packed1);
    const VU16 down2 = ShiftRight<15>(packed2);
    const VU16 rawF = Xor3(ShiftLeft<2>(down2), Add(down1, down1), down0);
    StoreU(rawF, d, raw + 0xF * N);
  }
};  // Pack16<3>

template <>
struct Pack16<4> {
  template <class D>
  HWY_INLINE void Pack(D d, const uint16_t* HWY_RESTRICT raw,
                       uint16_t* HWY_RESTRICT packed_out) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 raw0 = LoadU(d, raw + 0 * N);
    const VU16 raw1 = LoadU(d, raw + 1 * N);
    const VU16 raw2 = LoadU(d, raw + 2 * N);
    const VU16 raw3 = LoadU(d, raw + 3 * N);
    const VU16 raw4 = LoadU(d, raw + 4 * N);
    const VU16 raw5 = LoadU(d, raw + 5 * N);
    const VU16 raw6 = LoadU(d, raw + 6 * N);
    const VU16 raw7 = LoadU(d, raw + 7 * N);
    const VU16 raw8 = LoadU(d, raw + 8 * N);
    const VU16 raw9 = LoadU(d, raw + 9 * N);
    const VU16 rawA = LoadU(d, raw + 0xA * N);
    const VU16 rawB = LoadU(d, raw + 0xB * N);
    const VU16 rawC = LoadU(d, raw + 0xC * N);
    const VU16 rawD = LoadU(d, raw + 0xD * N);
    const VU16 rawE = LoadU(d, raw + 0xE * N);
    const VU16 rawF = LoadU(d, raw + 0xF * N);

    VU16 packed0 = Xor3(ShiftLeft<8>(raw4), ShiftLeft<4>(raw2), raw0);
    VU16 packed1 = Xor3(ShiftLeft<8>(raw5), ShiftLeft<4>(raw3), raw1);
    packed0 = Or(packed0, ShiftLeft<12>(raw6));
    packed1 = Or(packed1, ShiftLeft<12>(raw7));
    VU16 packed2 = Xor3(ShiftLeft<8>(rawC), ShiftLeft<4>(rawA), raw8);
    VU16 packed3 = Xor3(ShiftLeft<8>(rawD), ShiftLeft<4>(rawB), raw9);
    packed2 = Or(packed2, ShiftLeft<12>(rawE));
    packed3 = Or(packed3, ShiftLeft<12>(rawF));

    StoreU(packed0, d, packed_out + 0 * N);
    StoreU(packed1, d, packed_out + 1 * N);
    StoreU(packed2, d, packed_out + 2 * N);
    StoreU(packed3, d, packed_out + 3 * N);
  }

  template <class D>
  HWY_INLINE void Unpack(D d, const uint16_t* HWY_RESTRICT packed_in,
                         uint16_t* HWY_RESTRICT raw) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 mask = Set(d, 0xFu);  // Lowest 4 bits

    const VU16 packed0 = LoadU(d, packed_in + 0 * N);
    const VU16 packed1 = LoadU(d, packed_in + 1 * N);
    const VU16 packed2 = LoadU(d, packed_in + 2 * N);
    const VU16 packed3 = LoadU(d, packed_in + 3 * N);

    const VU16 raw0 = And(packed0, mask);
    StoreU(raw0, d, raw + 0 * N);

    const VU16 raw1 = And(packed1, mask);
    StoreU(raw1, d, raw + 1 * N);

    const VU16 raw2 = And(ShiftRight<4>(packed0), mask);
    StoreU(raw2, d, raw + 2 * N);

    const VU16 raw3 = And(ShiftRight<4>(packed1), mask);
    StoreU(raw3, d, raw + 3 * N);

    const VU16 raw4 = And(ShiftRight<8>(packed0), mask);
    StoreU(raw4, d, raw + 4 * N);

    const VU16 raw5 = And(ShiftRight<8>(packed1), mask);
    StoreU(raw5, d, raw + 5 * N);

    const VU16 raw6 = ShiftRight<12>(packed0);  // no mask required
    StoreU(raw6, d, raw + 6 * N);

    const VU16 raw7 = ShiftRight<12>(packed1);  // no mask required
    StoreU(raw7, d, raw + 7 * N);

    const VU16 raw8 = And(packed2, mask);
    StoreU(raw8, d, raw + 8 * N);

    const VU16 raw9 = And(packed3, mask);
    StoreU(raw9, d, raw + 9 * N);

    const VU16 rawA = And(ShiftRight<4>(packed2), mask);
    StoreU(rawA, d, raw + 0xA * N);

    const VU16 rawB = And(ShiftRight<4>(packed3), mask);
    StoreU(rawB, d, raw + 0xB * N);

    const VU16 rawC = And(ShiftRight<8>(packed2), mask);
    StoreU(rawC, d, raw + 0xC * N);

    const VU16 rawD = And(ShiftRight<8>(packed3), mask);
    StoreU(rawD, d, raw + 0xD * N);

    const VU16 rawE = ShiftRight<12>(packed2);  // no mask required
    StoreU(rawE, d, raw + 0xE * N);

    const VU16 rawF = ShiftRight<12>(packed3);  // no mask required
    StoreU(rawF, d, raw + 0xF * N);
  }
};  // Pack16<4>

template <>
struct Pack16<5> {
  template <class D>
  HWY_INLINE void Pack(D d, const uint16_t* HWY_RESTRICT raw,
                       uint16_t* HWY_RESTRICT packed_out) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 raw0 = LoadU(d, raw + 0 * N);
    const VU16 raw1 = LoadU(d, raw + 1 * N);
    const VU16 raw2 = LoadU(d, raw + 2 * N);
    const VU16 raw3 = LoadU(d, raw + 3 * N);
    const VU16 raw4 = LoadU(d, raw + 4 * N);
    const VU16 raw5 = LoadU(d, raw + 5 * N);
    const VU16 raw6 = LoadU(d, raw + 6 * N);
    const VU16 raw7 = LoadU(d, raw + 7 * N);
    const VU16 raw8 = LoadU(d, raw + 8 * N);
    const VU16 raw9 = LoadU(d, raw + 9 * N);
    const VU16 rawA = LoadU(d, raw + 0xA * N);
    const VU16 rawB = LoadU(d, raw + 0xB * N);
    const VU16 rawC = LoadU(d, raw + 0xC * N);
    const VU16 rawD = LoadU(d, raw + 0xD * N);
    const VU16 rawE = LoadU(d, raw + 0xE * N);
    const VU16 rawF = LoadU(d, raw + 0xF * N);

    // We can fit 15 raw vectors in five packed vectors (three each).
    VU16 packed0 = Xor3(ShiftLeft<10>(rawA), ShiftLeft<5>(raw5), raw0);
    VU16 packed1 = Xor3(ShiftLeft<10>(rawB), ShiftLeft<5>(raw6), raw1);
    VU16 packed2 = Xor3(ShiftLeft<10>(rawC), ShiftLeft<5>(raw7), raw2);
    VU16 packed3 = Xor3(ShiftLeft<10>(rawD), ShiftLeft<5>(raw8), raw3);
    VU16 packed4 = Xor3(ShiftLeft<10>(rawE), ShiftLeft<5>(raw9), raw4);

    // rawF will be scattered into the upper bits of these five.
    const VU16 hi1 = Set(d, 0x8000u);
    packed0 = Or(packed0, ShiftLeft<15>(rawF));  // MSB only, no mask
    packed1 = OrAnd(packed1, ShiftLeft<14>(rawF), hi1);
    packed2 = OrAnd(packed2, ShiftLeft<13>(rawF), hi1);
    packed3 = OrAnd(packed3, ShiftLeft<12>(rawF), hi1);
    packed4 = OrAnd(packed4, ShiftLeft<11>(rawF), hi1);

    StoreU(packed0, d, packed_out + 0 * N);
    StoreU(packed1, d, packed_out + 1 * N);
    StoreU(packed2, d, packed_out + 2 * N);
    StoreU(packed3, d, packed_out + 3 * N);
    StoreU(packed4, d, packed_out + 4 * N);
  }

  template <class D>
  HWY_INLINE void Unpack(D d, const uint16_t* HWY_RESTRICT packed_in,
                         uint16_t* HWY_RESTRICT raw) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);

    const VU16 packed0 = LoadU(d, packed_in + 0 * N);
    const VU16 packed1 = LoadU(d, packed_in + 1 * N);
    const VU16 packed2 = LoadU(d, packed_in + 2 * N);
    const VU16 packed3 = LoadU(d, packed_in + 3 * N);
    const VU16 packed4 = LoadU(d, packed_in + 4 * N);

    const VU16 mask = Set(d, 0x1Fu);  // Lowest 5 bits

    const VU16 raw0 = And(packed0, mask);
    StoreU(raw0, d, raw + 0 * N);

    const VU16 raw1 = And(packed1, mask);
    StoreU(raw1, d, raw + 1 * N);

    const VU16 raw2 = And(packed2, mask);
    StoreU(raw2, d, raw + 2 * N);

    const VU16 raw3 = And(packed3, mask);
    StoreU(raw3, d, raw + 3 * N);

    const VU16 raw4 = And(packed4, mask);
    StoreU(raw4, d, raw + 4 * N);

    const VU16 raw5 = And(ShiftRight<5>(packed0), mask);
    StoreU(raw5, d, raw + 5 * N);

    const VU16 raw6 = And(ShiftRight<5>(packed1), mask);
    StoreU(raw6, d, raw + 6 * N);

    const VU16 raw7 = And(ShiftRight<5>(packed2), mask);
    StoreU(raw7, d, raw + 7 * N);

    const VU16 raw8 = And(ShiftRight<5>(packed3), mask);
    StoreU(raw8, d, raw + 8 * N);

    const VU16 raw9 = And(ShiftRight<5>(packed4), mask);
    StoreU(raw9, d, raw + 9 * N);

    const VU16 rawA = And(ShiftRight<10>(packed0), mask);
    StoreU(rawA, d, raw + 0xA * N);

    const VU16 rawB = And(ShiftRight<10>(packed1), mask);
    StoreU(rawB, d, raw + 0xB * N);

    const VU16 rawC = And(ShiftRight<10>(packed2), mask);
    StoreU(rawC, d, raw + 0xC * N);

    const VU16 rawD = And(ShiftRight<10>(packed3), mask);
    StoreU(rawD, d, raw + 0xD * N);

    const VU16 rawE = And(ShiftRight<10>(packed4), mask);
    StoreU(rawE, d, raw + 0xE * N);

    // rawF is the concatenation of the lower bit of packed0..4.
    const VU16 down0 = ShiftRight<15>(packed0);
    const VU16 down1 = ShiftRight<15>(packed1);
    const VU16 hi1 = Set(d, 0x8000u);
    const VU16 p0 =
        Xor3(ShiftRight<13>(And(packed2, hi1)), Add(down1, down1), down0);
    const VU16 rawF = Xor3(ShiftRight<11>(And(packed4, hi1)),
                           ShiftRight<12>(And(packed3, hi1)), p0);
    StoreU(rawF, d, raw + 0xF * N);
  }
};  // Pack16<5>

template <>
struct Pack16<6> {
  template <class D>
  HWY_INLINE void Pack(D d, const uint16_t* HWY_RESTRICT raw,
                       uint16_t* HWY_RESTRICT packed_out) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 raw0 = LoadU(d, raw + 0 * N);
    const VU16 raw1 = LoadU(d, raw + 1 * N);
    const VU16 raw2 = LoadU(d, raw + 2 * N);
    const VU16 raw3 = LoadU(d, raw + 3 * N);
    const VU16 raw4 = LoadU(d, raw + 4 * N);
    const VU16 raw5 = LoadU(d, raw + 5 * N);
    const VU16 raw6 = LoadU(d, raw + 6 * N);
    const VU16 raw7 = LoadU(d, raw + 7 * N);
    const VU16 raw8 = LoadU(d, raw + 8 * N);
    const VU16 raw9 = LoadU(d, raw + 9 * N);
    const VU16 rawA = LoadU(d, raw + 0xA * N);
    const VU16 rawB = LoadU(d, raw + 0xB * N);
    const VU16 rawC = LoadU(d, raw + 0xC * N);
    const VU16 rawD = LoadU(d, raw + 0xD * N);
    const VU16 rawE = LoadU(d, raw + 0xE * N);
    const VU16 rawF = LoadU(d, raw + 0xF * N);

    const VU16 packed3 = Or(ShiftLeft<6>(raw7), raw3);
    const VU16 packed7 = Or(ShiftLeft<6>(rawF), rawB);
    // Three vectors, two 6-bit raw each; packed3 (12 bits) is spread over the
    // four remainder bits at the top of each vector.
    const VU16 packed0 = Xor3(ShiftLeft<12>(packed3), ShiftLeft<6>(raw4), raw0);
    VU16 packed1 = Or(ShiftLeft<6>(raw5), raw1);
    VU16 packed2 = Or(ShiftLeft<6>(raw6), raw2);
    const VU16 packed4 = Xor3(ShiftLeft<12>(packed7), ShiftLeft<6>(rawC), raw8);
    VU16 packed5 = Or(ShiftLeft<6>(rawD), raw9);
    VU16 packed6 = Or(ShiftLeft<6>(rawE), rawA);

    const VU16 hi4 = Set(d, 0xF000u);
    packed1 = OrAnd(packed1, ShiftLeft<8>(packed3), hi4);
    packed2 = OrAnd(packed2, ShiftLeft<4>(packed3), hi4);
    packed5 = OrAnd(packed5, ShiftLeft<8>(packed7), hi4);
    packed6 = OrAnd(packed6, ShiftLeft<4>(packed7), hi4);

    StoreU(packed0, d, packed_out + 0 * N);
    StoreU(packed1, d, packed_out + 1 * N);
    StoreU(packed2, d, packed_out + 2 * N);
    StoreU(packed4, d, packed_out + 3 * N);
    StoreU(packed5, d, packed_out + 4 * N);
    StoreU(packed6, d, packed_out + 5 * N);
  }

  template <class D>
  HWY_INLINE void Unpack(D d, const uint16_t* HWY_RESTRICT packed_in,
                         uint16_t* HWY_RESTRICT raw) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 mask = Set(d, 0x3Fu);  // Lowest 6 bits

    const VU16 packed0 = LoadU(d, packed_in + 0 * N);
    const VU16 packed1 = LoadU(d, packed_in + 1 * N);
    const VU16 packed2 = LoadU(d, packed_in + 2 * N);
    const VU16 packed4 = LoadU(d, packed_in + 3 * N);
    const VU16 packed5 = LoadU(d, packed_in + 4 * N);
    const VU16 packed6 = LoadU(d, packed_in + 5 * N);

    const VU16 raw0 = And(packed0, mask);
    StoreU(raw0, d, raw + 0 * N);

    const VU16 raw1 = And(packed1, mask);
    StoreU(raw1, d, raw + 1 * N);

    const VU16 raw2 = And(packed2, mask);
    StoreU(raw2, d, raw + 2 * N);

    const VU16 raw4 = And(ShiftRight<6>(packed0), mask);
    StoreU(raw4, d, raw + 4 * N);

    const VU16 raw5 = And(ShiftRight<6>(packed1), mask);
    StoreU(raw5, d, raw + 5 * N);

    const VU16 raw6 = And(ShiftRight<6>(packed2), mask);
    StoreU(raw6, d, raw + 6 * N);

    const VU16 raw8 = And(packed4, mask);
    StoreU(raw8, d, raw + 8 * N);

    const VU16 raw9 = And(packed5, mask);
    StoreU(raw9, d, raw + 9 * N);

    const VU16 rawA = And(packed6, mask);
    StoreU(rawA, d, raw + 0xA * N);

    const VU16 rawC = And(ShiftRight<6>(packed4), mask);
    StoreU(rawC, d, raw + 0xC * N);

    const VU16 rawD = And(ShiftRight<6>(packed5), mask);
    StoreU(rawD, d, raw + 0xD * N);

    const VU16 rawE = And(ShiftRight<6>(packed6), mask);
    StoreU(rawE, d, raw + 0xE * N);

    // packed3 is the concatenation of the four upper bits in packed0..2.
    const VU16 down0 = ShiftRight<12>(packed0);
    const VU16 down4 = ShiftRight<12>(packed4);
    const VU16 hi4 = Set(d, 0xF000u);
    const VU16 packed3 = Xor3(ShiftRight<4>(And(packed2, hi4)),
                              ShiftRight<8>(And(packed1, hi4)), down0);
    const VU16 packed7 = Xor3(ShiftRight<4>(And(packed6, hi4)),
                              ShiftRight<8>(And(packed5, hi4)), down4);
    const VU16 raw3 = And(packed3, mask);
    StoreU(raw3, d, raw + 3 * N);

    const VU16 rawB = And(packed7, mask);
    StoreU(rawB, d, raw + 0xB * N);

    const VU16 raw7 = ShiftRight<6>(packed3);  // upper bits already zero
    StoreU(raw7, d, raw + 7 * N);

    const VU16 rawF = ShiftRight<6>(packed7);  // upper bits already zero
    StoreU(rawF, d, raw + 0xF * N);
  }
};  // Pack16<6>

template <>
struct Pack16<7> {
  template <class D>
  HWY_INLINE void Pack(D d, const uint16_t* HWY_RESTRICT raw,
                       uint16_t* HWY_RESTRICT packed_out) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 raw0 = LoadU(d, raw + 0 * N);
    const VU16 raw1 = LoadU(d, raw + 1 * N);
    const VU16 raw2 = LoadU(d, raw + 2 * N);
    const VU16 raw3 = LoadU(d, raw + 3 * N);
    const VU16 raw4 = LoadU(d, raw + 4 * N);
    const VU16 raw5 = LoadU(d, raw + 5 * N);
    const VU16 raw6 = LoadU(d, raw + 6 * N);
    const VU16 raw7 = LoadU(d, raw + 7 * N);
    const VU16 raw8 = LoadU(d, raw + 8 * N);
    const VU16 raw9 = LoadU(d, raw + 9 * N);
    const VU16 rawA = LoadU(d, raw + 0xA * N);
    const VU16 rawB = LoadU(d, raw + 0xB * N);
    const VU16 rawC = LoadU(d, raw + 0xC * N);
    const VU16 rawD = LoadU(d, raw + 0xD * N);
    const VU16 rawE = LoadU(d, raw + 0xE * N);
    const VU16 rawF = LoadU(d, raw + 0xF * N);

    const VU16 packed7 = Or(ShiftLeft<7>(rawF), raw7);
    // Seven vectors, two 7-bit raw each; packed7 (14 bits) is spread over the
    // two remainder bits at the top of each vector.
    const VU16 packed0 = Xor3(ShiftLeft<14>(packed7), ShiftLeft<7>(raw8), raw0);
    VU16 packed1 = Or(ShiftLeft<7>(raw9), raw1);
    VU16 packed2 = Or(ShiftLeft<7>(rawA), raw2);
    VU16 packed3 = Or(ShiftLeft<7>(rawB), raw3);
    VU16 packed4 = Or(ShiftLeft<7>(rawC), raw4);
    VU16 packed5 = Or(ShiftLeft<7>(rawD), raw5);
    VU16 packed6 = Or(ShiftLeft<7>(rawE), raw6);

    const VU16 hi2 = Set(d, 0xC000u);
    packed1 = OrAnd(packed1, ShiftLeft<12>(packed7), hi2);
    packed2 = OrAnd(packed2, ShiftLeft<10>(packed7), hi2);
    packed3 = OrAnd(packed3, ShiftLeft<8>(packed7), hi2);
    packed4 = OrAnd(packed4, ShiftLeft<6>(packed7), hi2);
    packed5 = OrAnd(packed5, ShiftLeft<4>(packed7), hi2);
    packed6 = OrAnd(packed6, ShiftLeft<2>(packed7), hi2);

    StoreU(packed0, d, packed_out + 0 * N);
    StoreU(packed1, d, packed_out + 1 * N);
    StoreU(packed2, d, packed_out + 2 * N);
    StoreU(packed3, d, packed_out + 3 * N);
    StoreU(packed4, d, packed_out + 4 * N);
    StoreU(packed5, d, packed_out + 5 * N);
    StoreU(packed6, d, packed_out + 6 * N);
  }

  template <class D>
  HWY_INLINE void Unpack(D d, const uint16_t* HWY_RESTRICT packed_in,
                         uint16_t* HWY_RESTRICT raw) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);

    const VU16 packed0 = BitCast(d, LoadU(d, packed_in + 0 * N));
    const VU16 packed1 = BitCast(d, LoadU(d, packed_in + 1 * N));
    const VU16 packed2 = BitCast(d, LoadU(d, packed_in + 2 * N));
    const VU16 packed3 = BitCast(d, LoadU(d, packed_in + 3 * N));
    const VU16 packed4 = BitCast(d, LoadU(d, packed_in + 4 * N));
    const VU16 packed5 = BitCast(d, LoadU(d, packed_in + 5 * N));
    const VU16 packed6 = BitCast(d, LoadU(d, packed_in + 6 * N));

    const VU16 mask = Set(d, 0x7Fu);  // Lowest 7 bits

    const VU16 raw0 = And(packed0, mask);
    StoreU(raw0, d, raw + 0 * N);

    const VU16 raw1 = And(packed1, mask);
    StoreU(raw1, d, raw + 1 * N);

    const VU16 raw2 = And(packed2, mask);
    StoreU(raw2, d, raw + 2 * N);

    const VU16 raw3 = And(packed3, mask);
    StoreU(raw3, d, raw + 3 * N);

    const VU16 raw4 = And(packed4, mask);
    StoreU(raw4, d, raw + 4 * N);

    const VU16 raw5 = And(packed5, mask);
    StoreU(raw5, d, raw + 5 * N);

    const VU16 raw6 = And(packed6, mask);
    StoreU(raw6, d, raw + 6 * N);

    const VU16 raw8 = And(ShiftRight<7>(packed0), mask);
    StoreU(raw8, d, raw + 8 * N);

    const VU16 raw9 = And(ShiftRight<7>(packed1), mask);
    StoreU(raw9, d, raw + 9 * N);

    const VU16 rawA = And(ShiftRight<7>(packed2), mask);
    StoreU(rawA, d, raw + 0xA * N);

    const VU16 rawB = And(ShiftRight<7>(packed3), mask);
    StoreU(rawB, d, raw + 0xB * N);

    const VU16 rawC = And(ShiftRight<7>(packed4), mask);
    StoreU(rawC, d, raw + 0xC * N);

    const VU16 rawD = And(ShiftRight<7>(packed5), mask);
    StoreU(rawD, d, raw + 0xD * N);

    const VU16 rawE = And(ShiftRight<7>(packed6), mask);
    StoreU(rawE, d, raw + 0xE * N);

    // packed7 is the concatenation of the two upper bits in packed0..6.
    const VU16 down0 = ShiftRight<14>(packed0);
    const VU16 hi2 = Set(d, 0xC000u);
    const VU16 p0 = Xor3(ShiftRight<12>(And(packed1, hi2)),
                         ShiftRight<10>(And(packed2, hi2)), down0);
    const VU16 p1 = Xor3(ShiftRight<8>(And(packed3, hi2)),  //
                         ShiftRight<6>(And(packed4, hi2)),
                         ShiftRight<4>(And(packed5, hi2)));
    const VU16 packed7 = Xor3(ShiftRight<2>(And(packed6, hi2)), p1, p0);

    const VU16 raw7 = And(packed7, mask);
    StoreU(raw7, d, raw + 7 * N);

    const VU16 rawF = ShiftRight<7>(packed7);  // upper bits already zero
    StoreU(rawF, d, raw + 0xF * N);
  }
};  // Pack16<7>

template <>
struct Pack16<8> {
  template <class D>
  HWY_INLINE void Pack(D d, const uint16_t* HWY_RESTRICT raw,
                       uint16_t* HWY_RESTRICT packed_out) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 raw0 = LoadU(d, raw + 0 * N);
    const VU16 raw1 = LoadU(d, raw + 1 * N);
    const VU16 raw2 = LoadU(d, raw + 2 * N);
    const VU16 raw3 = LoadU(d, raw + 3 * N);
    const VU16 raw4 = LoadU(d, raw + 4 * N);
    const VU16 raw5 = LoadU(d, raw + 5 * N);
    const VU16 raw6 = LoadU(d, raw + 6 * N);
    const VU16 raw7 = LoadU(d, raw + 7 * N);
    const VU16 raw8 = LoadU(d, raw + 8 * N);
    const VU16 raw9 = LoadU(d, raw + 9 * N);
    const VU16 rawA = LoadU(d, raw + 0xA * N);
    const VU16 rawB = LoadU(d, raw + 0xB * N);
    const VU16 rawC = LoadU(d, raw + 0xC * N);
    const VU16 rawD = LoadU(d, raw + 0xD * N);
    const VU16 rawE = LoadU(d, raw + 0xE * N);
    const VU16 rawF = LoadU(d, raw + 0xF * N);

    // This is equivalent to ConcatEven with 8-bit lanes, but much more
    // efficient on RVV and slightly less efficient on SVE2.
    const VU16 packed0 = Or(ShiftLeft<8>(raw2), raw0);
    const VU16 packed1 = Or(ShiftLeft<8>(raw3), raw1);
    const VU16 packed2 = Or(ShiftLeft<8>(raw6), raw4);
    const VU16 packed3 = Or(ShiftLeft<8>(raw7), raw5);
    const VU16 packed4 = Or(ShiftLeft<8>(rawA), raw8);
    const VU16 packed5 = Or(ShiftLeft<8>(rawB), raw9);
    const VU16 packed6 = Or(ShiftLeft<8>(rawE), rawC);
    const VU16 packed7 = Or(ShiftLeft<8>(rawF), rawD);

    StoreU(packed0, d, packed_out + 0 * N);
    StoreU(packed1, d, packed_out + 1 * N);
    StoreU(packed2, d, packed_out + 2 * N);
    StoreU(packed3, d, packed_out + 3 * N);
    StoreU(packed4, d, packed_out + 4 * N);
    StoreU(packed5, d, packed_out + 5 * N);
    StoreU(packed6, d, packed_out + 6 * N);
    StoreU(packed7, d, packed_out + 7 * N);
  }

  template <class D>
  HWY_INLINE void Unpack(D d, const uint16_t* HWY_RESTRICT packed_in,
                         uint16_t* HWY_RESTRICT raw) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);

    const VU16 packed0 = BitCast(d, LoadU(d, packed_in + 0 * N));
    const VU16 packed1 = BitCast(d, LoadU(d, packed_in + 1 * N));
    const VU16 packed2 = BitCast(d, LoadU(d, packed_in + 2 * N));
    const VU16 packed3 = BitCast(d, LoadU(d, packed_in + 3 * N));
    const VU16 packed4 = BitCast(d, LoadU(d, packed_in + 4 * N));
    const VU16 packed5 = BitCast(d, LoadU(d, packed_in + 5 * N));
    const VU16 packed6 = BitCast(d, LoadU(d, packed_in + 6 * N));
    const VU16 packed7 = BitCast(d, LoadU(d, packed_in + 7 * N));
    const VU16 mask = Set(d, 0xFFu);  // Lowest 8 bits

    const VU16 raw0 = And(packed0, mask);
    StoreU(raw0, d, raw + 0 * N);

    const VU16 raw1 = And(packed1, mask);
    StoreU(raw1, d, raw + 1 * N);

    const VU16 raw2 = ShiftRight<8>(packed0);  // upper bits already zero
    StoreU(raw2, d, raw + 2 * N);

    const VU16 raw3 = ShiftRight<8>(packed1);  // upper bits already zero
    StoreU(raw3, d, raw + 3 * N);

    const VU16 raw4 = And(packed2, mask);
    StoreU(raw4, d, raw + 4 * N);

    const VU16 raw5 = And(packed3, mask);
    StoreU(raw5, d, raw + 5 * N);

    const VU16 raw6 = ShiftRight<8>(packed2);  // upper bits already zero
    StoreU(raw6, d, raw + 6 * N);

    const VU16 raw7 = ShiftRight<8>(packed3);  // upper bits already zero
    StoreU(raw7, d, raw + 7 * N);

    const VU16 raw8 = And(packed4, mask);
    StoreU(raw8, d, raw + 8 * N);

    const VU16 raw9 = And(packed5, mask);
    StoreU(raw9, d, raw + 9 * N);

    const VU16 rawA = ShiftRight<8>(packed4);  // upper bits already zero
    StoreU(rawA, d, raw + 0xA * N);

    const VU16 rawB = ShiftRight<8>(packed5);  // upper bits already zero
    StoreU(rawB, d, raw + 0xB * N);

    const VU16 rawC = And(packed6, mask);
    StoreU(rawC, d, raw + 0xC * N);

    const VU16 rawD = And(packed7, mask);
    StoreU(rawD, d, raw + 0xD * N);

    const VU16 rawE = ShiftRight<8>(packed6);  // upper bits already zero
    StoreU(rawE, d, raw + 0xE * N);

    const VU16 rawF = ShiftRight<8>(packed7);  // upper bits already zero
    StoreU(rawF, d, raw + 0xF * N);
  }
};  // Pack16<8>

template <>
struct Pack16<9> {
  template <class D>
  HWY_INLINE void Pack(D d, const uint16_t* HWY_RESTRICT raw,
                       uint16_t* HWY_RESTRICT packed_out) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 raw0 = LoadU(d, raw + 0 * N);
    const VU16 raw1 = LoadU(d, raw + 1 * N);
    const VU16 raw2 = LoadU(d, raw + 2 * N);
    const VU16 raw3 = LoadU(d, raw + 3 * N);
    const VU16 raw4 = LoadU(d, raw + 4 * N);
    const VU16 raw5 = LoadU(d, raw + 5 * N);
    const VU16 raw6 = LoadU(d, raw + 6 * N);
    const VU16 raw7 = LoadU(d, raw + 7 * N);
    const VU16 raw8 = LoadU(d, raw + 8 * N);
    const VU16 raw9 = LoadU(d, raw + 9 * N);
    const VU16 rawA = LoadU(d, raw + 0xA * N);
    const VU16 rawB = LoadU(d, raw + 0xB * N);
    const VU16 rawC = LoadU(d, raw + 0xC * N);
    const VU16 rawD = LoadU(d, raw + 0xD * N);
    const VU16 rawE = LoadU(d, raw + 0xE * N);
    const VU16 rawF = LoadU(d, raw + 0xF * N);
    // 8 vectors, each with 9+7 bits; top 2 bits are concatenated into packed8.
    const VU16 packed0 = Or(ShiftLeft<9>(raw8), raw0);
    const VU16 packed1 = Or(ShiftLeft<9>(raw9), raw1);
    const VU16 packed2 = Or(ShiftLeft<9>(rawA), raw2);
    const VU16 packed3 = Or(ShiftLeft<9>(rawB), raw3);
    const VU16 packed4 = Or(ShiftLeft<9>(rawC), raw4);
    const VU16 packed5 = Or(ShiftLeft<9>(rawD), raw5);
    const VU16 packed6 = Or(ShiftLeft<9>(rawE), raw6);
    const VU16 packed7 = Or(ShiftLeft<9>(rawF), raw7);

    // We could shift down, OR and shift up, but two shifts are typically more
    // expensive than AND, shift into position, and OR (which can be further
    // reduced via Xor3).
    const VU16 mid2 = Set(d, 0x180u);  // top 2 in lower 9
    const VU16 part8 = ShiftRight<7>(And(raw8, mid2));
    const VU16 part9 = ShiftRight<5>(And(raw9, mid2));
    const VU16 partA = ShiftRight<3>(And(rawA, mid2));
    const VU16 partB = ShiftRight<1>(And(rawB, mid2));
    const VU16 partC = ShiftLeft<1>(And(rawC, mid2));
    const VU16 partD = ShiftLeft<3>(And(rawD, mid2));
    const VU16 partE = ShiftLeft<5>(And(rawE, mid2));
    const VU16 partF = ShiftLeft<7>(And(rawF, mid2));
    const VU16 packed8 = Xor3(Xor3(part8, part9, partA),
                              Xor3(partB, partC, partD), Or(partE, partF));

    StoreU(packed0, d, packed_out + 0 * N);
    StoreU(packed1, d, packed_out + 1 * N);
    StoreU(packed2, d, packed_out + 2 * N);
    StoreU(packed3, d, packed_out + 3 * N);
    StoreU(packed4, d, packed_out + 4 * N);
    StoreU(packed5, d, packed_out + 5 * N);
    StoreU(packed6, d, packed_out + 6 * N);
    StoreU(packed7, d, packed_out + 7 * N);
    StoreU(packed8, d, packed_out + 8 * N);
  }

  template <class D>
  HWY_INLINE void Unpack(D d, const uint16_t* HWY_RESTRICT packed_in,
                         uint16_t* HWY_RESTRICT raw) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);

    const VU16 packed0 = BitCast(d, LoadU(d, packed_in + 0 * N));
    const VU16 packed1 = BitCast(d, LoadU(d, packed_in + 1 * N));
    const VU16 packed2 = BitCast(d, LoadU(d, packed_in + 2 * N));
    const VU16 packed3 = BitCast(d, LoadU(d, packed_in + 3 * N));
    const VU16 packed4 = BitCast(d, LoadU(d, packed_in + 4 * N));
    const VU16 packed5 = BitCast(d, LoadU(d, packed_in + 5 * N));
    const VU16 packed6 = BitCast(d, LoadU(d, packed_in + 6 * N));
    const VU16 packed7 = BitCast(d, LoadU(d, packed_in + 7 * N));
    const VU16 packed8 = BitCast(d, LoadU(d, packed_in + 8 * N));

    const VU16 mask = Set(d, 0x1FFu);  // Lowest 9 bits

    const VU16 raw0 = And(packed0, mask);
    StoreU(raw0, d, raw + 0 * N);

    const VU16 raw1 = And(packed1, mask);
    StoreU(raw1, d, raw + 1 * N);

    const VU16 raw2 = And(packed2, mask);
    StoreU(raw2, d, raw + 2 * N);

    const VU16 raw3 = And(packed3, mask);
    StoreU(raw3, d, raw + 3 * N);

    const VU16 raw4 = And(packed4, mask);
    StoreU(raw4, d, raw + 4 * N);

    const VU16 raw5 = And(packed5, mask);
    StoreU(raw5, d, raw + 5 * N);

    const VU16 raw6 = And(packed6, mask);
    StoreU(raw6, d, raw + 6 * N);

    const VU16 raw7 = And(packed7, mask);
    StoreU(raw7, d, raw + 7 * N);

    const VU16 mid2 = Set(d, 0x180u);  // top 2 in lower 9
    const VU16 raw8 =
        OrAnd(ShiftRight<9>(packed0), ShiftLeft<7>(packed8), mid2);
    const VU16 raw9 =
        OrAnd(ShiftRight<9>(packed1), ShiftLeft<5>(packed8), mid2);
    const VU16 rawA =
        OrAnd(ShiftRight<9>(packed2), ShiftLeft<3>(packed8), mid2);
    const VU16 rawB =
        OrAnd(ShiftRight<9>(packed3), ShiftLeft<1>(packed8), mid2);
    const VU16 rawC =
        OrAnd(ShiftRight<9>(packed4), ShiftRight<1>(packed8), mid2);
    const VU16 rawD =
        OrAnd(ShiftRight<9>(packed5), ShiftRight<3>(packed8), mid2);
    const VU16 rawE =
        OrAnd(ShiftRight<9>(packed6), ShiftRight<5>(packed8), mid2);
    const VU16 rawF =
        OrAnd(ShiftRight<9>(packed7), ShiftRight<7>(packed8), mid2);

    StoreU(raw8, d, raw + 8 * N);
    StoreU(raw9, d, raw + 9 * N);
    StoreU(rawA, d, raw + 0xA * N);
    StoreU(rawB, d, raw + 0xB * N);
    StoreU(rawC, d, raw + 0xC * N);
    StoreU(rawD, d, raw + 0xD * N);
    StoreU(rawE, d, raw + 0xE * N);
    StoreU(rawF, d, raw + 0xF * N);
  }
};  // Pack16<9>

template <>
struct Pack16<10> {
  template <class D>
  HWY_INLINE void Pack(D d, const uint16_t* HWY_RESTRICT raw,
                       uint16_t* HWY_RESTRICT packed_out) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 raw0 = LoadU(d, raw + 0 * N);
    const VU16 raw1 = LoadU(d, raw + 1 * N);
    const VU16 raw2 = LoadU(d, raw + 2 * N);
    const VU16 raw3 = LoadU(d, raw + 3 * N);
    const VU16 raw4 = LoadU(d, raw + 4 * N);
    const VU16 raw5 = LoadU(d, raw + 5 * N);
    const VU16 raw6 = LoadU(d, raw + 6 * N);
    const VU16 raw7 = LoadU(d, raw + 7 * N);
    const VU16 raw8 = LoadU(d, raw + 8 * N);
    const VU16 raw9 = LoadU(d, raw + 9 * N);
    const VU16 rawA = LoadU(d, raw + 0xA * N);
    const VU16 rawB = LoadU(d, raw + 0xB * N);
    const VU16 rawC = LoadU(d, raw + 0xC * N);
    const VU16 rawD = LoadU(d, raw + 0xD * N);
    const VU16 rawE = LoadU(d, raw + 0xE * N);
    const VU16 rawF = LoadU(d, raw + 0xF * N);

    // 8 vectors, each with 10+6 bits; top 4 bits are concatenated into
    // packed8 and packed9.
    const VU16 packed0 = Or(ShiftLeft<10>(raw8), raw0);
    const VU16 packed1 = Or(ShiftLeft<10>(raw9), raw1);
    const VU16 packed2 = Or(ShiftLeft<10>(rawA), raw2);
    const VU16 packed3 = Or(ShiftLeft<10>(rawB), raw3);
    const VU16 packed4 = Or(ShiftLeft<10>(rawC), raw4);
    const VU16 packed5 = Or(ShiftLeft<10>(rawD), raw5);
    const VU16 packed6 = Or(ShiftLeft<10>(rawE), raw6);
    const VU16 packed7 = Or(ShiftLeft<10>(rawF), raw7);

    // We could shift down, OR and shift up, but two shifts are typically more
    // expensive than AND, shift into position, and OR (which can be further
    // reduced via Xor3).
    const VU16 mid4 = Set(d, 0x3C0u);  // top 4 in lower 10
    const VU16 part8 = ShiftRight<6>(And(raw8, mid4));
    const VU16 part9 = ShiftRight<2>(And(raw9, mid4));
    const VU16 partA = ShiftLeft<2>(And(rawA, mid4));
    const VU16 partB = ShiftLeft<6>(And(rawB, mid4));
    const VU16 partC = ShiftRight<6>(And(rawC, mid4));
    const VU16 partD = ShiftRight<2>(And(rawD, mid4));
    const VU16 partE = ShiftLeft<2>(And(rawE, mid4));
    const VU16 partF = ShiftLeft<6>(And(rawF, mid4));
    const VU16 packed8 = Or(Xor3(part8, part9, partA), partB);
    const VU16 packed9 = Or(Xor3(partC, partD, partE), partF);

    StoreU(packed0, d, packed_out + 0 * N);
    StoreU(packed1, d, packed_out + 1 * N);
    StoreU(packed2, d, packed_out + 2 * N);
    StoreU(packed3, d, packed_out + 3 * N);
    StoreU(packed4, d, packed_out + 4 * N);
    StoreU(packed5, d, packed_out + 5 * N);
    StoreU(packed6, d, packed_out + 6 * N);
    StoreU(packed7, d, packed_out + 7 * N);
    StoreU(packed8, d, packed_out + 8 * N);
    StoreU(packed9, d, packed_out + 9 * N);
  }

  template <class D>
  HWY_INLINE void Unpack(D d, const uint16_t* HWY_RESTRICT packed_in,
                         uint16_t* HWY_RESTRICT raw) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);

    const VU16 packed0 = BitCast(d, LoadU(d, packed_in + 0 * N));
    const VU16 packed1 = BitCast(d, LoadU(d, packed_in + 1 * N));
    const VU16 packed2 = BitCast(d, LoadU(d, packed_in + 2 * N));
    const VU16 packed3 = BitCast(d, LoadU(d, packed_in + 3 * N));
    const VU16 packed4 = BitCast(d, LoadU(d, packed_in + 4 * N));
    const VU16 packed5 = BitCast(d, LoadU(d, packed_in + 5 * N));
    const VU16 packed6 = BitCast(d, LoadU(d, packed_in + 6 * N));
    const VU16 packed7 = BitCast(d, LoadU(d, packed_in + 7 * N));
    const VU16 packed8 = BitCast(d, LoadU(d, packed_in + 8 * N));
    const VU16 packed9 = BitCast(d, LoadU(d, packed_in + 9 * N));

    const VU16 mask = Set(d, 0x3FFu);  // Lowest 10 bits

    const VU16 raw0 = And(packed0, mask);
    StoreU(raw0, d, raw + 0 * N);

    const VU16 raw1 = And(packed1, mask);
    StoreU(raw1, d, raw + 1 * N);

    const VU16 raw2 = And(packed2, mask);
    StoreU(raw2, d, raw + 2 * N);

    const VU16 raw3 = And(packed3, mask);
    StoreU(raw3, d, raw + 3 * N);

    const VU16 raw4 = And(packed4, mask);
    StoreU(raw4, d, raw + 4 * N);

    const VU16 raw5 = And(packed5, mask);
    StoreU(raw5, d, raw + 5 * N);

    const VU16 raw6 = And(packed6, mask);
    StoreU(raw6, d, raw + 6 * N);

    const VU16 raw7 = And(packed7, mask);
    StoreU(raw7, d, raw + 7 * N);

    const VU16 mid4 = Set(d, 0x3C0u);  // top 4 in lower 10
    const VU16 raw8 =
        OrAnd(ShiftRight<10>(packed0), ShiftLeft<6>(packed8), mid4);
    const VU16 raw9 =
        OrAnd(ShiftRight<10>(packed1), ShiftLeft<2>(packed8), mid4);
    const VU16 rawA =
        OrAnd(ShiftRight<10>(packed2), ShiftRight<2>(packed8), mid4);
    const VU16 rawB =
        OrAnd(ShiftRight<10>(packed3), ShiftRight<6>(packed8), mid4);
    const VU16 rawC =
        OrAnd(ShiftRight<10>(packed4), ShiftLeft<6>(packed9), mid4);
    const VU16 rawD =
        OrAnd(ShiftRight<10>(packed5), ShiftLeft<2>(packed9), mid4);
    const VU16 rawE =
        OrAnd(ShiftRight<10>(packed6), ShiftRight<2>(packed9), mid4);
    const VU16 rawF =
        OrAnd(ShiftRight<10>(packed7), ShiftRight<6>(packed9), mid4);

    StoreU(raw8, d, raw + 8 * N);
    StoreU(raw9, d, raw + 9 * N);
    StoreU(rawA, d, raw + 0xA * N);
    StoreU(rawB, d, raw + 0xB * N);
    StoreU(rawC, d, raw + 0xC * N);
    StoreU(rawD, d, raw + 0xD * N);
    StoreU(rawE, d, raw + 0xE * N);
    StoreU(rawF, d, raw + 0xF * N);
  }
};  // Pack16<10>

template <>
struct Pack16<11> {
  template <class D>
  HWY_INLINE void Pack(D d, const uint16_t* HWY_RESTRICT raw,
                       uint16_t* HWY_RESTRICT packed_out) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 raw0 = LoadU(d, raw + 0 * N);
    const VU16 raw1 = LoadU(d, raw + 1 * N);
    const VU16 raw2 = LoadU(d, raw + 2 * N);
    const VU16 raw3 = LoadU(d, raw + 3 * N);
    const VU16 raw4 = LoadU(d, raw + 4 * N);
    const VU16 raw5 = LoadU(d, raw + 5 * N);
    const VU16 raw6 = LoadU(d, raw + 6 * N);
    const VU16 raw7 = LoadU(d, raw + 7 * N);
    const VU16 raw8 = LoadU(d, raw + 8 * N);
    const VU16 raw9 = LoadU(d, raw + 9 * N);
    const VU16 rawA = LoadU(d, raw + 0xA * N);
    const VU16 rawB = LoadU(d, raw + 0xB * N);
    const VU16 rawC = LoadU(d, raw + 0xC * N);
    const VU16 rawD = LoadU(d, raw + 0xD * N);
    const VU16 rawE = LoadU(d, raw + 0xE * N);
    const VU16 rawF = LoadU(d, raw + 0xF * N);

    // It is not obvious what the optimal partitioning looks like. To reduce the
    // number of constants, we want to minimize the number of distinct bit
    // lengths. 11+5 also requires 6-bit remnants with 4-bit leftovers.
    // 8+3 seems better: it is easier to scatter 3 bits into the MSBs.
    const VU16 lo8 = Set(d, 0xFFu);

    // Lower 8 bits of all raw
    const VU16 packed0 = OrAnd(ShiftLeft<8>(raw1), raw0, lo8);
    const VU16 packed1 = OrAnd(ShiftLeft<8>(raw3), raw2, lo8);
    const VU16 packed2 = OrAnd(ShiftLeft<8>(raw5), raw4, lo8);
    const VU16 packed3 = OrAnd(ShiftLeft<8>(raw7), raw6, lo8);
    const VU16 packed4 = OrAnd(ShiftLeft<8>(raw9), raw8, lo8);
    const VU16 packed5 = OrAnd(ShiftLeft<8>(rawB), rawA, lo8);
    const VU16 packed6 = OrAnd(ShiftLeft<8>(rawD), rawC, lo8);
    const VU16 packed7 = OrAnd(ShiftLeft<8>(rawF), rawE, lo8);

    StoreU(packed0, d, packed_out + 0 * N);
    StoreU(packed1, d, packed_out + 1 * N);
    StoreU(packed2, d, packed_out + 2 * N);
    StoreU(packed3, d, packed_out + 3 * N);
    StoreU(packed4, d, packed_out + 4 * N);
    StoreU(packed5, d, packed_out + 5 * N);
    StoreU(packed6, d, packed_out + 6 * N);
    StoreU(packed7, d, packed_out + 7 * N);

    // Three vectors, five 3bit remnants each, plus one 3bit in their MSB.
    const VU16 top0 = ShiftRight<8>(raw0);
    const VU16 top1 = ShiftRight<8>(raw1);
    const VU16 top2 = ShiftRight<8>(raw2);
    // Insert top raw bits into 3-bit groups within packed8..A. Moving the
    // mask along avoids masking each of raw0..E and enables OrAnd.
    VU16 next = Set(d, 0x38u);  // 0x7 << 3
    VU16 packed8 = OrAnd(top0, ShiftRight<5>(raw3), next);
    VU16 packed9 = OrAnd(top1, ShiftRight<5>(raw4), next);
    VU16 packedA = OrAnd(top2, ShiftRight<5>(raw5), next);
    next = ShiftLeft<3>(next);
    packed8 = OrAnd(packed8, ShiftRight<2>(raw6), next);
    packed9 = OrAnd(packed9, ShiftRight<2>(raw7), next);
    packedA = OrAnd(packedA, ShiftRight<2>(raw8), next);
    next = ShiftLeft<3>(next);
    packed8 = OrAnd(packed8, Add(raw9, raw9), next);
    packed9 = OrAnd(packed9, Add(rawA, rawA), next);
    packedA = OrAnd(packedA, Add(rawB, rawB), next);
    next = ShiftLeft<3>(next);
    packed8 = OrAnd(packed8, ShiftLeft<4>(rawC), next);
    packed9 = OrAnd(packed9, ShiftLeft<4>(rawD), next);
    packedA = OrAnd(packedA, ShiftLeft<4>(rawE), next);

    // Scatter upper 3 bits of rawF into the upper bits.
    next = ShiftLeft<3>(next);  // = 0x8000u
    packed8 = OrAnd(packed8, ShiftLeft<7>(rawF), next);
    packed9 = OrAnd(packed9, ShiftLeft<6>(rawF), next);
    packedA = OrAnd(packedA, ShiftLeft<5>(rawF), next);

    StoreU(packed8, d, packed_out + 8 * N);
    StoreU(packed9, d, packed_out + 9 * N);
    StoreU(packedA, d, packed_out + 0xA * N);
  }

  template <class D>
  HWY_INLINE void Unpack(D d, const uint16_t* HWY_RESTRICT packed_in,
                         uint16_t* HWY_RESTRICT raw) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);

    const VU16 packed0 = BitCast(d, LoadU(d, packed_in + 0 * N));
    const VU16 packed1 = BitCast(d, LoadU(d, packed_in + 1 * N));
    const VU16 packed2 = BitCast(d, LoadU(d, packed_in + 2 * N));
    const VU16 packed3 = BitCast(d, LoadU(d, packed_in + 3 * N));
    const VU16 packed4 = BitCast(d, LoadU(d, packed_in + 4 * N));
    const VU16 packed5 = BitCast(d, LoadU(d, packed_in + 5 * N));
    const VU16 packed6 = BitCast(d, LoadU(d, packed_in + 6 * N));
    const VU16 packed7 = BitCast(d, LoadU(d, packed_in + 7 * N));
    const VU16 packed8 = BitCast(d, LoadU(d, packed_in + 8 * N));
    const VU16 packed9 = BitCast(d, LoadU(d, packed_in + 9 * N));
    const VU16 packedA = BitCast(d, LoadU(d, packed_in + 0xA * N));

    const VU16 mask = Set(d, 0xFFu);  // Lowest 8 bits

    const VU16 down0 = And(packed0, mask);
    const VU16 down1 = ShiftRight<8>(packed0);
    const VU16 down2 = And(packed1, mask);
    const VU16 down3 = ShiftRight<8>(packed1);
    const VU16 down4 = And(packed2, mask);
    const VU16 down5 = ShiftRight<8>(packed2);
    const VU16 down6 = And(packed3, mask);
    const VU16 down7 = ShiftRight<8>(packed3);
    const VU16 down8 = And(packed4, mask);
    const VU16 down9 = ShiftRight<8>(packed4);
    const VU16 downA = And(packed5, mask);
    const VU16 downB = ShiftRight<8>(packed5);
    const VU16 downC = And(packed6, mask);
    const VU16 downD = ShiftRight<8>(packed6);
    const VU16 downE = And(packed7, mask);
    const VU16 downF = ShiftRight<8>(packed7);

    // Three bits from packed8..A, eight bits from down0..F.
    const VU16 hi3 = Set(d, 0x700u);
    const VU16 raw0 = OrAnd(down0, ShiftLeft<8>(packed8), hi3);
    const VU16 raw1 = OrAnd(down1, ShiftLeft<8>(packed9), hi3);
    const VU16 raw2 = OrAnd(down2, ShiftLeft<8>(packedA), hi3);

    const VU16 raw3 = OrAnd(down3, ShiftLeft<5>(packed8), hi3);
    const VU16 raw4 = OrAnd(down4, ShiftLeft<5>(packed9), hi3);
    const VU16 raw5 = OrAnd(down5, ShiftLeft<5>(packedA), hi3);

    const VU16 raw6 = OrAnd(down6, ShiftLeft<2>(packed8), hi3);
    const VU16 raw7 = OrAnd(down7, ShiftLeft<2>(packed9), hi3);
    const VU16 raw8 = OrAnd(down8, ShiftLeft<2>(packedA), hi3);

    const VU16 raw9 = OrAnd(down9, ShiftRight<1>(packed8), hi3);
    const VU16 rawA = OrAnd(downA, ShiftRight<1>(packed9), hi3);
    const VU16 rawB = OrAnd(downB, ShiftRight<1>(packedA), hi3);

    const VU16 rawC = OrAnd(downC, ShiftRight<4>(packed8), hi3);
    const VU16 rawD = OrAnd(downD, ShiftRight<4>(packed9), hi3);
    const VU16 rawE = OrAnd(downE, ShiftRight<4>(packedA), hi3);

    // Shift MSB into the top 3-of-11 and mask.
    const VU16 rawF = Or(downF, Xor3(And(ShiftRight<7>(packed8), hi3),
                                     And(ShiftRight<6>(packed9), hi3),
                                     And(ShiftRight<5>(packedA), hi3)));

    StoreU(raw0, d, raw + 0 * N);
    StoreU(raw1, d, raw + 1 * N);
    StoreU(raw2, d, raw + 2 * N);
    StoreU(raw3, d, raw + 3 * N);
    StoreU(raw4, d, raw + 4 * N);
    StoreU(raw5, d, raw + 5 * N);
    StoreU(raw6, d, raw + 6 * N);
    StoreU(raw7, d, raw + 7 * N);
    StoreU(raw8, d, raw + 8 * N);
    StoreU(raw9, d, raw + 9 * N);
    StoreU(rawA, d, raw + 0xA * N);
    StoreU(rawB, d, raw + 0xB * N);
    StoreU(rawC, d, raw + 0xC * N);
    StoreU(rawD, d, raw + 0xD * N);
    StoreU(rawE, d, raw + 0xE * N);
    StoreU(rawF, d, raw + 0xF * N);
  }
};  // Pack16<11>

template <>
struct Pack16<12> {
  template <class D>
  HWY_INLINE void Pack(D d, const uint16_t* HWY_RESTRICT raw,
                       uint16_t* HWY_RESTRICT packed_out) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 raw0 = LoadU(d, raw + 0 * N);
    const VU16 raw1 = LoadU(d, raw + 1 * N);
    const VU16 raw2 = LoadU(d, raw + 2 * N);
    const VU16 raw3 = LoadU(d, raw + 3 * N);
    const VU16 raw4 = LoadU(d, raw + 4 * N);
    const VU16 raw5 = LoadU(d, raw + 5 * N);
    const VU16 raw6 = LoadU(d, raw + 6 * N);
    const VU16 raw7 = LoadU(d, raw + 7 * N);
    const VU16 raw8 = LoadU(d, raw + 8 * N);
    const VU16 raw9 = LoadU(d, raw + 9 * N);
    const VU16 rawA = LoadU(d, raw + 0xA * N);
    const VU16 rawB = LoadU(d, raw + 0xB * N);
    const VU16 rawC = LoadU(d, raw + 0xC * N);
    const VU16 rawD = LoadU(d, raw + 0xD * N);
    const VU16 rawE = LoadU(d, raw + 0xE * N);
    const VU16 rawF = LoadU(d, raw + 0xF * N);

    // 8 vectors, each with 12+4 bits; top 8 bits are concatenated into
    // packed8 to packedB.
    const VU16 packed0 = Or(ShiftLeft<12>(raw8), raw0);
    const VU16 packed1 = Or(ShiftLeft<12>(raw9), raw1);
    const VU16 packed2 = Or(ShiftLeft<12>(rawA), raw2);
    const VU16 packed3 = Or(ShiftLeft<12>(rawB), raw3);
    const VU16 packed4 = Or(ShiftLeft<12>(rawC), raw4);
    const VU16 packed5 = Or(ShiftLeft<12>(rawD), raw5);
    const VU16 packed6 = Or(ShiftLeft<12>(rawE), raw6);
    const VU16 packed7 = Or(ShiftLeft<12>(rawF), raw7);

    // Masking after shifting left enables OrAnd.
    const VU16 hi8 = Set(d, 0xFF00u);
    const VU16 packed8 = OrAnd(ShiftRight<4>(raw8), ShiftLeft<4>(raw9), hi8);
    const VU16 packed9 = OrAnd(ShiftRight<4>(rawA), ShiftLeft<4>(rawB), hi8);
    const VU16 packedA = OrAnd(ShiftRight<4>(rawC), ShiftLeft<4>(rawD), hi8);
    const VU16 packedB = OrAnd(ShiftRight<4>(rawE), ShiftLeft<4>(rawF), hi8);
    StoreU(packed0, d, packed_out + 0 * N);
    StoreU(packed1, d, packed_out + 1 * N);
    StoreU(packed2, d, packed_out + 2 * N);
    StoreU(packed3, d, packed_out + 3 * N);
    StoreU(packed4, d, packed_out + 4 * N);
    StoreU(packed5, d, packed_out + 5 * N);
    StoreU(packed6, d, packed_out + 6 * N);
    StoreU(packed7, d, packed_out + 7 * N);
    StoreU(packed8, d, packed_out + 8 * N);
    StoreU(packed9, d, packed_out + 9 * N);
    StoreU(packedA, d, packed_out + 0xA * N);
    StoreU(packedB, d, packed_out + 0xB * N);
  }

  template <class D>
  HWY_INLINE void Unpack(D d, const uint16_t* HWY_RESTRICT packed_in,
                         uint16_t* HWY_RESTRICT raw) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);

    const VU16 packed0 = BitCast(d, LoadU(d, packed_in + 0 * N));
    const VU16 packed1 = BitCast(d, LoadU(d, packed_in + 1 * N));
    const VU16 packed2 = BitCast(d, LoadU(d, packed_in + 2 * N));
    const VU16 packed3 = BitCast(d, LoadU(d, packed_in + 3 * N));
    const VU16 packed4 = BitCast(d, LoadU(d, packed_in + 4 * N));
    const VU16 packed5 = BitCast(d, LoadU(d, packed_in + 5 * N));
    const VU16 packed6 = BitCast(d, LoadU(d, packed_in + 6 * N));
    const VU16 packed7 = BitCast(d, LoadU(d, packed_in + 7 * N));
    const VU16 packed8 = BitCast(d, LoadU(d, packed_in + 8 * N));
    const VU16 packed9 = BitCast(d, LoadU(d, packed_in + 9 * N));
    const VU16 packedA = BitCast(d, LoadU(d, packed_in + 0xA * N));
    const VU16 packedB = BitCast(d, LoadU(d, packed_in + 0xB * N));

    const VU16 mask = Set(d, 0xFFFu);  // Lowest 12 bits

    const VU16 raw0 = And(packed0, mask);
    StoreU(raw0, d, raw + 0 * N);

    const VU16 raw1 = And(packed1, mask);
    StoreU(raw1, d, raw + 1 * N);

    const VU16 raw2 = And(packed2, mask);
    StoreU(raw2, d, raw + 2 * N);

    const VU16 raw3 = And(packed3, mask);
    StoreU(raw3, d, raw + 3 * N);

    const VU16 raw4 = And(packed4, mask);
    StoreU(raw4, d, raw + 4 * N);

    const VU16 raw5 = And(packed5, mask);
    StoreU(raw5, d, raw + 5 * N);

    const VU16 raw6 = And(packed6, mask);
    StoreU(raw6, d, raw + 6 * N);

    const VU16 raw7 = And(packed7, mask);
    StoreU(raw7, d, raw + 7 * N);

    const VU16 mid8 = Set(d, 0xFF0u);  // upper 8 in lower 12
    const VU16 raw8 =
        OrAnd(ShiftRight<12>(packed0), ShiftLeft<4>(packed8), mid8);
    const VU16 raw9 =
        OrAnd(ShiftRight<12>(packed1), ShiftRight<4>(packed8), mid8);
    const VU16 rawA =
        OrAnd(ShiftRight<12>(packed2), ShiftLeft<4>(packed9), mid8);
    const VU16 rawB =
        OrAnd(ShiftRight<12>(packed3), ShiftRight<4>(packed9), mid8);
    const VU16 rawC =
        OrAnd(ShiftRight<12>(packed4), ShiftLeft<4>(packedA), mid8);
    const VU16 rawD =
        OrAnd(ShiftRight<12>(packed5), ShiftRight<4>(packedA), mid8);
    const VU16 rawE =
        OrAnd(ShiftRight<12>(packed6), ShiftLeft<4>(packedB), mid8);
    const VU16 rawF =
        OrAnd(ShiftRight<12>(packed7), ShiftRight<4>(packedB), mid8);
    StoreU(raw8, d, raw + 8 * N);
    StoreU(raw9, d, raw + 9 * N);
    StoreU(rawA, d, raw + 0xA * N);
    StoreU(rawB, d, raw + 0xB * N);
    StoreU(rawC, d, raw + 0xC * N);
    StoreU(rawD, d, raw + 0xD * N);
    StoreU(rawE, d, raw + 0xE * N);
    StoreU(rawF, d, raw + 0xF * N);
  }
};  // Pack16<12>

template <>
struct Pack16<13> {
  template <class D>
  HWY_INLINE void Pack(D d, const uint16_t* HWY_RESTRICT raw,
                       uint16_t* HWY_RESTRICT packed_out) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 raw0 = LoadU(d, raw + 0 * N);
    const VU16 raw1 = LoadU(d, raw + 1 * N);
    const VU16 raw2 = LoadU(d, raw + 2 * N);
    const VU16 raw3 = LoadU(d, raw + 3 * N);
    const VU16 raw4 = LoadU(d, raw + 4 * N);
    const VU16 raw5 = LoadU(d, raw + 5 * N);
    const VU16 raw6 = LoadU(d, raw + 6 * N);
    const VU16 raw7 = LoadU(d, raw + 7 * N);
    const VU16 raw8 = LoadU(d, raw + 8 * N);
    const VU16 raw9 = LoadU(d, raw + 9 * N);
    const VU16 rawA = LoadU(d, raw + 0xA * N);
    const VU16 rawB = LoadU(d, raw + 0xB * N);
    const VU16 rawC = LoadU(d, raw + 0xC * N);
    const VU16 rawD = LoadU(d, raw + 0xD * N);
    const VU16 rawE = LoadU(d, raw + 0xE * N);
    const VU16 rawF = LoadU(d, raw + 0xF * N);

    // As with 11 bits, it is not obvious what the optimal partitioning looks
    // like. We similarly go with an 8+5 split.
    const VU16 lo8 = Set(d, 0xFFu);

    // Lower 8 bits of all raw
    const VU16 packed0 = OrAnd(ShiftLeft<8>(raw1), raw0, lo8);
    const VU16 packed1 = OrAnd(ShiftLeft<8>(raw3), raw2, lo8);
    const VU16 packed2 = OrAnd(ShiftLeft<8>(raw5), raw4, lo8);
    const VU16 packed3 = OrAnd(ShiftLeft<8>(raw7), raw6, lo8);
    const VU16 packed4 = OrAnd(ShiftLeft<8>(raw9), raw8, lo8);
    const VU16 packed5 = OrAnd(ShiftLeft<8>(rawB), rawA, lo8);
    const VU16 packed6 = OrAnd(ShiftLeft<8>(rawD), rawC, lo8);
    const VU16 packed7 = OrAnd(ShiftLeft<8>(rawF), rawE, lo8);

    StoreU(packed0, d, packed_out + 0 * N);
    StoreU(packed1, d, packed_out + 1 * N);
    StoreU(packed2, d, packed_out + 2 * N);
    StoreU(packed3, d, packed_out + 3 * N);
    StoreU(packed4, d, packed_out + 4 * N);
    StoreU(packed5, d, packed_out + 5 * N);
    StoreU(packed6, d, packed_out + 6 * N);
    StoreU(packed7, d, packed_out + 7 * N);

    // Five vectors, three 5bit remnants each, plus one 5bit in their MSB.
    const VU16 top0 = ShiftRight<8>(raw0);
    const VU16 top1 = ShiftRight<8>(raw1);
    const VU16 top2 = ShiftRight<8>(raw2);
    const VU16 top3 = ShiftRight<8>(raw3);
    const VU16 top4 = ShiftRight<8>(raw4);

    // Insert top raw bits into 5-bit groups within packed8..C. Moving the
    // mask along avoids masking each of raw0..E and enables OrAnd.
    VU16 next = Set(d, 0x3E0u);  // 0x1F << 5
    VU16 packed8 = OrAnd(top0, ShiftRight<3>(raw5), next);
    VU16 packed9 = OrAnd(top1, ShiftRight<3>(raw6), next);
    VU16 packedA = OrAnd(top2, ShiftRight<3>(raw7), next);
    VU16 packedB = OrAnd(top3, ShiftRight<3>(raw8), next);
    VU16 packedC = OrAnd(top4, ShiftRight<3>(raw9), next);
    next = ShiftLeft<5>(next);
    packed8 = OrAnd(packed8, ShiftLeft<2>(rawA), next);
    packed9 = OrAnd(packed9, ShiftLeft<2>(rawB), next);
    packedA = OrAnd(packedA, ShiftLeft<2>(rawC), next);
    packedB = OrAnd(packedB, ShiftLeft<2>(rawD), next);
    packedC = OrAnd(packedC, ShiftLeft<2>(rawE), next);

    // Scatter upper 5 bits of rawF into the upper bits.
    next = ShiftLeft<3>(next);  // = 0x8000u
    packed8 = OrAnd(packed8, ShiftLeft<7>(rawF), next);
    packed9 = OrAnd(packed9, ShiftLeft<6>(rawF), next);
    packedA = OrAnd(packedA, ShiftLeft<5>(rawF), next);
    packedB = OrAnd(packedB, ShiftLeft<4>(rawF), next);
    packedC = OrAnd(packedC, ShiftLeft<3>(rawF), next);

    StoreU(packed8, d, packed_out + 8 * N);
    StoreU(packed9, d, packed_out + 9 * N);
    StoreU(packedA, d, packed_out + 0xA * N);
    StoreU(packedB, d, packed_out + 0xB * N);
    StoreU(packedC, d, packed_out + 0xC * N);
  }

  template <class D>
  HWY_INLINE void Unpack(D d, const uint16_t* HWY_RESTRICT packed_in,
                         uint16_t* HWY_RESTRICT raw) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);

    const VU16 packed0 = BitCast(d, LoadU(d, packed_in + 0 * N));
    const VU16 packed1 = BitCast(d, LoadU(d, packed_in + 1 * N));
    const VU16 packed2 = BitCast(d, LoadU(d, packed_in + 2 * N));
    const VU16 packed3 = BitCast(d, LoadU(d, packed_in + 3 * N));
    const VU16 packed4 = BitCast(d, LoadU(d, packed_in + 4 * N));
    const VU16 packed5 = BitCast(d, LoadU(d, packed_in + 5 * N));
    const VU16 packed6 = BitCast(d, LoadU(d, packed_in + 6 * N));
    const VU16 packed7 = BitCast(d, LoadU(d, packed_in + 7 * N));
    const VU16 packed8 = BitCast(d, LoadU(d, packed_in + 8 * N));
    const VU16 packed9 = BitCast(d, LoadU(d, packed_in + 9 * N));
    const VU16 packedA = BitCast(d, LoadU(d, packed_in + 0xA * N));
    const VU16 packedB = BitCast(d, LoadU(d, packed_in + 0xB * N));
    const VU16 packedC = BitCast(d, LoadU(d, packed_in + 0xC * N));

    const VU16 mask = Set(d, 0xFFu);  // Lowest 8 bits

    const VU16 down0 = And(packed0, mask);
    const VU16 down1 = ShiftRight<8>(packed0);
    const VU16 down2 = And(packed1, mask);
    const VU16 down3 = ShiftRight<8>(packed1);
    const VU16 down4 = And(packed2, mask);
    const VU16 down5 = ShiftRight<8>(packed2);
    const VU16 down6 = And(packed3, mask);
    const VU16 down7 = ShiftRight<8>(packed3);
    const VU16 down8 = And(packed4, mask);
    const VU16 down9 = ShiftRight<8>(packed4);
    const VU16 downA = And(packed5, mask);
    const VU16 downB = ShiftRight<8>(packed5);
    const VU16 downC = And(packed6, mask);
    const VU16 downD = ShiftRight<8>(packed6);
    const VU16 downE = And(packed7, mask);
    const VU16 downF = ShiftRight<8>(packed7);

    // Upper five bits from packed8..C, eight bits from down0..F.
    const VU16 hi5 = Set(d, 0x1F00u);
    const VU16 raw0 = OrAnd(down0, ShiftLeft<8>(packed8), hi5);
    const VU16 raw1 = OrAnd(down1, ShiftLeft<8>(packed9), hi5);
    const VU16 raw2 = OrAnd(down2, ShiftLeft<8>(packedA), hi5);
    const VU16 raw3 = OrAnd(down3, ShiftLeft<8>(packedB), hi5);
    const VU16 raw4 = OrAnd(down4, ShiftLeft<8>(packedC), hi5);

    const VU16 raw5 = OrAnd(down5, ShiftLeft<3>(packed8), hi5);
    const VU16 raw6 = OrAnd(down6, ShiftLeft<3>(packed9), hi5);
    const VU16 raw7 = OrAnd(down7, ShiftLeft<3>(packedA), hi5);
    const VU16 raw8 = OrAnd(down8, ShiftLeft<3>(packed9), hi5);
    const VU16 raw9 = OrAnd(down9, ShiftLeft<3>(packedA), hi5);

    const VU16 rawA = OrAnd(downA, ShiftRight<2>(packed8), hi5);
    const VU16 rawB = OrAnd(downB, ShiftRight<2>(packed9), hi5);
    const VU16 rawC = OrAnd(downC, ShiftRight<2>(packedA), hi5);
    const VU16 rawD = OrAnd(downD, ShiftRight<2>(packed9), hi5);
    const VU16 rawE = OrAnd(downE, ShiftRight<2>(packedA), hi5);

    // Shift MSB into the top 5-of-11 and mask.
    const VU16 p0 = Xor3(And(ShiftRight<7>(packed8), hi5),  //
                         And(ShiftRight<6>(packed9), hi5),
                         And(ShiftRight<5>(packedA), hi5));
    const VU16 p1 = Xor3(And(ShiftRight<4>(packedB), hi5),
                         And(ShiftRight<3>(packedC), hi5), downF);
    const VU16 rawF = Or(p0, p1);

    StoreU(raw0, d, raw + 0 * N);
    StoreU(raw1, d, raw + 1 * N);
    StoreU(raw2, d, raw + 2 * N);
    StoreU(raw3, d, raw + 3 * N);
    StoreU(raw4, d, raw + 4 * N);
    StoreU(raw5, d, raw + 5 * N);
    StoreU(raw6, d, raw + 6 * N);
    StoreU(raw7, d, raw + 7 * N);
    StoreU(raw8, d, raw + 8 * N);
    StoreU(raw9, d, raw + 9 * N);
    StoreU(rawA, d, raw + 0xA * N);
    StoreU(rawB, d, raw + 0xB * N);
    StoreU(rawC, d, raw + 0xC * N);
    StoreU(rawD, d, raw + 0xD * N);
    StoreU(rawE, d, raw + 0xE * N);
    StoreU(rawF, d, raw + 0xF * N);
  }
};  // Pack16<13>

template <>
struct Pack16<14> {
  template <class D>
  HWY_INLINE void Pack(D d, const uint16_t* HWY_RESTRICT raw,
                       uint16_t* HWY_RESTRICT packed_out) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 raw0 = LoadU(d, raw + 0 * N);
    const VU16 raw1 = LoadU(d, raw + 1 * N);
    const VU16 raw2 = LoadU(d, raw + 2 * N);
    const VU16 raw3 = LoadU(d, raw + 3 * N);
    const VU16 raw4 = LoadU(d, raw + 4 * N);
    const VU16 raw5 = LoadU(d, raw + 5 * N);
    const VU16 raw6 = LoadU(d, raw + 6 * N);
    const VU16 raw7 = LoadU(d, raw + 7 * N);
    const VU16 raw8 = LoadU(d, raw + 8 * N);
    const VU16 raw9 = LoadU(d, raw + 9 * N);
    const VU16 rawA = LoadU(d, raw + 0xA * N);
    const VU16 rawB = LoadU(d, raw + 0xB * N);
    const VU16 rawC = LoadU(d, raw + 0xC * N);
    const VU16 rawD = LoadU(d, raw + 0xD * N);
    const VU16 rawE = LoadU(d, raw + 0xE * N);
    const VU16 rawF = LoadU(d, raw + 0xF * N);

    // 14 vectors, each with 14+2 bits; two raw vectors are scattered
    // across the upper 2 bits.
    const VU16 hi2 = Set(d, 0xC000u);
    const VU16 packed0 = Or(raw0, ShiftLeft<14>(rawE));
    const VU16 packed1 = OrAnd(raw1, ShiftLeft<12>(rawE), hi2);
    const VU16 packed2 = OrAnd(raw2, ShiftLeft<10>(rawE), hi2);
    const VU16 packed3 = OrAnd(raw3, ShiftLeft<8>(rawE), hi2);
    const VU16 packed4 = OrAnd(raw4, ShiftLeft<6>(rawE), hi2);
    const VU16 packed5 = OrAnd(raw5, ShiftLeft<4>(rawE), hi2);
    const VU16 packed6 = OrAnd(raw6, ShiftLeft<2>(rawE), hi2);
    const VU16 packed7 = Or(raw7, ShiftLeft<14>(rawF));
    const VU16 packed8 = OrAnd(raw8, ShiftLeft<12>(rawF), hi2);
    const VU16 packed9 = OrAnd(raw9, ShiftLeft<10>(rawF), hi2);
    const VU16 packedA = OrAnd(rawA, ShiftLeft<8>(rawF), hi2);
    const VU16 packedB = OrAnd(rawB, ShiftLeft<6>(rawF), hi2);
    const VU16 packedC = OrAnd(rawC, ShiftLeft<4>(rawF), hi2);
    const VU16 packedD = OrAnd(rawD, ShiftLeft<2>(rawF), hi2);

    StoreU(packed0, d, packed_out + 0 * N);
    StoreU(packed1, d, packed_out + 1 * N);
    StoreU(packed2, d, packed_out + 2 * N);
    StoreU(packed3, d, packed_out + 3 * N);
    StoreU(packed4, d, packed_out + 4 * N);
    StoreU(packed5, d, packed_out + 5 * N);
    StoreU(packed6, d, packed_out + 6 * N);
    StoreU(packed7, d, packed_out + 7 * N);
    StoreU(packed8, d, packed_out + 8 * N);
    StoreU(packed9, d, packed_out + 9 * N);
    StoreU(packedA, d, packed_out + 0xA * N);
    StoreU(packedB, d, packed_out + 0xB * N);
    StoreU(packedC, d, packed_out + 0xC * N);
    StoreU(packedD, d, packed_out + 0xD * N);
  }

  template <class D>
  HWY_INLINE void Unpack(D d, const uint16_t* HWY_RESTRICT packed_in,
                         uint16_t* HWY_RESTRICT raw) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);

    const VU16 packed0 = BitCast(d, LoadU(d, packed_in + 0 * N));
    const VU16 packed1 = BitCast(d, LoadU(d, packed_in + 1 * N));
    const VU16 packed2 = BitCast(d, LoadU(d, packed_in + 2 * N));
    const VU16 packed3 = BitCast(d, LoadU(d, packed_in + 3 * N));
    const VU16 packed4 = BitCast(d, LoadU(d, packed_in + 4 * N));
    const VU16 packed5 = BitCast(d, LoadU(d, packed_in + 5 * N));
    const VU16 packed6 = BitCast(d, LoadU(d, packed_in + 6 * N));
    const VU16 packed7 = BitCast(d, LoadU(d, packed_in + 7 * N));
    const VU16 packed8 = BitCast(d, LoadU(d, packed_in + 8 * N));
    const VU16 packed9 = BitCast(d, LoadU(d, packed_in + 9 * N));
    const VU16 packedA = BitCast(d, LoadU(d, packed_in + 0xA * N));
    const VU16 packedB = BitCast(d, LoadU(d, packed_in + 0xB * N));
    const VU16 packedC = BitCast(d, LoadU(d, packed_in + 0xC * N));
    const VU16 packedD = BitCast(d, LoadU(d, packed_in + 0xD * N));

    const VU16 mask = Set(d, 0x3FFFu);  // Lowest 14 bits

    const VU16 raw0 = And(packed0, mask);
    StoreU(raw0, d, raw + 0 * N);

    const VU16 raw1 = And(packed1, mask);
    StoreU(raw1, d, raw + 1 * N);

    const VU16 raw2 = And(packed2, mask);
    StoreU(raw2, d, raw + 2 * N);

    const VU16 raw3 = And(packed3, mask);
    StoreU(raw3, d, raw + 3 * N);

    const VU16 raw4 = And(packed4, mask);
    StoreU(raw4, d, raw + 4 * N);

    const VU16 raw5 = And(packed5, mask);
    StoreU(raw5, d, raw + 5 * N);

    const VU16 raw6 = And(packed6, mask);
    StoreU(raw6, d, raw + 6 * N);

    const VU16 raw7 = And(packed7, mask);
    StoreU(raw7, d, raw + 7 * N);

    const VU16 raw8 = And(packed8, mask);
    StoreU(raw8, d, raw + 8 * N);

    const VU16 raw9 = And(packed9, mask);
    StoreU(raw9, d, raw + 9 * N);

    const VU16 rawA = And(packedA, mask);
    StoreU(rawA, d, raw + 0xA * N);

    const VU16 rawB = And(packedB, mask);
    StoreU(rawB, d, raw + 0xB * N);

    const VU16 rawC = And(packedC, mask);
    StoreU(rawC, d, raw + 0xC * N);

    const VU16 rawD = And(packedD, mask);
    StoreU(rawD, d, raw + 0xD * N);

    // rawE is the concatenation of the top two bits in packed0..6.
    const VU16 E0 = Xor3(ShiftRight<14>(packed0),  //
                         ShiftRight<12>(AndNot(mask, packed1)),
                         ShiftRight<10>(AndNot(mask, packed2)));
    const VU16 E1 = Xor3(ShiftRight<8>(AndNot(mask, packed3)),
                         ShiftRight<6>(AndNot(mask, packed4)),
                         ShiftRight<4>(AndNot(mask, packed5)));
    const VU16 rawE = Xor3(ShiftRight<2>(AndNot(mask, packed6)), E0, E1);
    const VU16 F0 = Xor3(ShiftRight<14>(AndNot(mask, packed7)),
                         ShiftRight<12>(AndNot(mask, packed8)),
                         ShiftRight<10>(AndNot(mask, packed9)));
    const VU16 F1 = Xor3(ShiftRight<8>(AndNot(mask, packedA)),
                         ShiftRight<6>(AndNot(mask, packedB)),
                         ShiftRight<4>(AndNot(mask, packedC)));
    const VU16 rawF = Xor3(ShiftRight<2>(AndNot(mask, packedD)), F0, F1);
    StoreU(rawE, d, raw + 0xE * N);
    StoreU(rawF, d, raw + 0xF * N);
  }
};  // Pack16<14>

template <>
struct Pack16<15> {
  template <class D>
  HWY_INLINE void Pack(D d, const uint16_t* HWY_RESTRICT raw,
                       uint16_t* HWY_RESTRICT packed_out) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 raw0 = LoadU(d, raw + 0 * N);
    const VU16 raw1 = LoadU(d, raw + 1 * N);
    const VU16 raw2 = LoadU(d, raw + 2 * N);
    const VU16 raw3 = LoadU(d, raw + 3 * N);
    const VU16 raw4 = LoadU(d, raw + 4 * N);
    const VU16 raw5 = LoadU(d, raw + 5 * N);
    const VU16 raw6 = LoadU(d, raw + 6 * N);
    const VU16 raw7 = LoadU(d, raw + 7 * N);
    const VU16 raw8 = LoadU(d, raw + 8 * N);
    const VU16 raw9 = LoadU(d, raw + 9 * N);
    const VU16 rawA = LoadU(d, raw + 0xA * N);
    const VU16 rawB = LoadU(d, raw + 0xB * N);
    const VU16 rawC = LoadU(d, raw + 0xC * N);
    const VU16 rawD = LoadU(d, raw + 0xD * N);
    const VU16 rawE = LoadU(d, raw + 0xE * N);
    const VU16 rawF = LoadU(d, raw + 0xF * N);

    // 15 vectors, each with 15+1 bits; one packed vector is scattered
    // across the upper bit.
    const VU16 hi1 = Set(d, 0x8000u);
    const VU16 packed0 = Or(raw0, ShiftLeft<15>(rawF));
    const VU16 packed1 = OrAnd(raw1, ShiftLeft<14>(rawF), hi1);
    const VU16 packed2 = OrAnd(raw2, ShiftLeft<13>(rawF), hi1);
    const VU16 packed3 = OrAnd(raw3, ShiftLeft<12>(rawF), hi1);
    const VU16 packed4 = OrAnd(raw4, ShiftLeft<11>(rawF), hi1);
    const VU16 packed5 = OrAnd(raw5, ShiftLeft<10>(rawF), hi1);
    const VU16 packed6 = OrAnd(raw6, ShiftLeft<9>(rawF), hi1);
    const VU16 packed7 = OrAnd(raw7, ShiftLeft<8>(rawF), hi1);
    const VU16 packed8 = OrAnd(raw8, ShiftLeft<7>(rawF), hi1);
    const VU16 packed9 = OrAnd(raw9, ShiftLeft<6>(rawF), hi1);
    const VU16 packedA = OrAnd(rawA, ShiftLeft<5>(rawF), hi1);
    const VU16 packedB = OrAnd(rawB, ShiftLeft<4>(rawF), hi1);
    const VU16 packedC = OrAnd(rawC, ShiftLeft<3>(rawF), hi1);
    const VU16 packedD = OrAnd(rawD, ShiftLeft<2>(rawF), hi1);
    const VU16 packedE = OrAnd(rawE, ShiftLeft<1>(rawF), hi1);

    StoreU(packed0, d, packed_out + 0 * N);
    StoreU(packed1, d, packed_out + 1 * N);
    StoreU(packed2, d, packed_out + 2 * N);
    StoreU(packed3, d, packed_out + 3 * N);
    StoreU(packed4, d, packed_out + 4 * N);
    StoreU(packed5, d, packed_out + 5 * N);
    StoreU(packed6, d, packed_out + 6 * N);
    StoreU(packed7, d, packed_out + 7 * N);
    StoreU(packed8, d, packed_out + 8 * N);
    StoreU(packed9, d, packed_out + 9 * N);
    StoreU(packedA, d, packed_out + 0xA * N);
    StoreU(packedB, d, packed_out + 0xB * N);
    StoreU(packedC, d, packed_out + 0xC * N);
    StoreU(packedD, d, packed_out + 0xD * N);
    StoreU(packedE, d, packed_out + 0xE * N);
  }

  template <class D>
  HWY_INLINE void Unpack(D d, const uint16_t* HWY_RESTRICT packed_in,
                         uint16_t* HWY_RESTRICT raw) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);

    const VU16 packed0 = BitCast(d, LoadU(d, packed_in + 0 * N));
    const VU16 packed1 = BitCast(d, LoadU(d, packed_in + 1 * N));
    const VU16 packed2 = BitCast(d, LoadU(d, packed_in + 2 * N));
    const VU16 packed3 = BitCast(d, LoadU(d, packed_in + 3 * N));
    const VU16 packed4 = BitCast(d, LoadU(d, packed_in + 4 * N));
    const VU16 packed5 = BitCast(d, LoadU(d, packed_in + 5 * N));
    const VU16 packed6 = BitCast(d, LoadU(d, packed_in + 6 * N));
    const VU16 packed7 = BitCast(d, LoadU(d, packed_in + 7 * N));
    const VU16 packed8 = BitCast(d, LoadU(d, packed_in + 8 * N));
    const VU16 packed9 = BitCast(d, LoadU(d, packed_in + 9 * N));
    const VU16 packedA = BitCast(d, LoadU(d, packed_in + 0xA * N));
    const VU16 packedB = BitCast(d, LoadU(d, packed_in + 0xB * N));
    const VU16 packedC = BitCast(d, LoadU(d, packed_in + 0xC * N));
    const VU16 packedD = BitCast(d, LoadU(d, packed_in + 0xD * N));
    const VU16 packedE = BitCast(d, LoadU(d, packed_in + 0xE * N));

    const VU16 mask = Set(d, 0x7FFFu);  // Lowest 15 bits

    const VU16 raw0 = And(packed0, mask);
    StoreU(raw0, d, raw + 0 * N);

    const VU16 raw1 = And(packed1, mask);
    StoreU(raw1, d, raw + 1 * N);

    const VU16 raw2 = And(packed2, mask);
    StoreU(raw2, d, raw + 2 * N);

    const VU16 raw3 = And(packed3, mask);
    StoreU(raw3, d, raw + 3 * N);

    const VU16 raw4 = And(packed4, mask);
    StoreU(raw4, d, raw + 4 * N);

    const VU16 raw5 = And(packed5, mask);
    StoreU(raw5, d, raw + 5 * N);

    const VU16 raw6 = And(packed6, mask);
    StoreU(raw6, d, raw + 6 * N);

    const VU16 raw7 = And(packed7, mask);
    StoreU(raw7, d, raw + 7 * N);

    const VU16 raw8 = And(packed8, mask);
    StoreU(raw8, d, raw + 8 * N);

    const VU16 raw9 = And(packed9, mask);
    StoreU(raw9, d, raw + 9 * N);

    const VU16 rawA = And(packedA, mask);
    StoreU(rawA, d, raw + 0xA * N);

    const VU16 rawB = And(packedB, mask);
    StoreU(rawB, d, raw + 0xB * N);

    const VU16 rawC = And(packedC, mask);
    StoreU(rawC, d, raw + 0xC * N);

    const VU16 rawD = And(packedD, mask);
    StoreU(rawD, d, raw + 0xD * N);

    const VU16 rawE = And(packedE, mask);
    StoreU(rawE, d, raw + 0xE * N);

    // rawF is the concatenation of the top bit in packed0..E.
    const VU16 F0 = Xor3(ShiftRight<15>(packed0),  //
                         ShiftRight<14>(AndNot(mask, packed1)),
                         ShiftRight<13>(AndNot(mask, packed2)));
    const VU16 F1 = Xor3(ShiftRight<12>(AndNot(mask, packed3)),
                         ShiftRight<11>(AndNot(mask, packed4)),
                         ShiftRight<10>(AndNot(mask, packed5)));
    const VU16 F2 = Xor3(ShiftRight<9>(AndNot(mask, packed6)),
                         ShiftRight<8>(AndNot(mask, packed7)),
                         ShiftRight<7>(AndNot(mask, packed8)));
    const VU16 F3 = Xor3(ShiftRight<6>(AndNot(mask, packed9)),
                         ShiftRight<5>(AndNot(mask, packedA)),
                         ShiftRight<4>(AndNot(mask, packedB)));
    const VU16 F4 = Xor3(ShiftRight<3>(AndNot(mask, packedC)),
                         ShiftRight<2>(AndNot(mask, packedD)),
                         ShiftRight<1>(AndNot(mask, packedE)));
    const VU16 rawF = Xor3(F0, F1, Xor3(F2, F3, F4));
    StoreU(rawF, d, raw + 0xF * N);
  }
};  // Pack16<15>

template <>
struct Pack16<16> {
  template <class D>
  HWY_INLINE void Pack(D d, const uint16_t* HWY_RESTRICT raw,
                       uint16_t* HWY_RESTRICT packed_out) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);
    const VU16 raw0 = LoadU(d, raw + 0 * N);
    const VU16 raw1 = LoadU(d, raw + 1 * N);
    const VU16 raw2 = LoadU(d, raw + 2 * N);
    const VU16 raw3 = LoadU(d, raw + 3 * N);
    const VU16 raw4 = LoadU(d, raw + 4 * N);
    const VU16 raw5 = LoadU(d, raw + 5 * N);
    const VU16 raw6 = LoadU(d, raw + 6 * N);
    const VU16 raw7 = LoadU(d, raw + 7 * N);
    const VU16 raw8 = LoadU(d, raw + 8 * N);
    const VU16 raw9 = LoadU(d, raw + 9 * N);
    const VU16 rawA = LoadU(d, raw + 0xA * N);
    const VU16 rawB = LoadU(d, raw + 0xB * N);
    const VU16 rawC = LoadU(d, raw + 0xC * N);
    const VU16 rawD = LoadU(d, raw + 0xD * N);
    const VU16 rawE = LoadU(d, raw + 0xE * N);
    const VU16 rawF = LoadU(d, raw + 0xF * N);

    StoreU(raw0, d, packed_out + 0 * N);
    StoreU(raw1, d, packed_out + 1 * N);
    StoreU(raw2, d, packed_out + 2 * N);
    StoreU(raw3, d, packed_out + 3 * N);
    StoreU(raw4, d, packed_out + 4 * N);
    StoreU(raw5, d, packed_out + 5 * N);
    StoreU(raw6, d, packed_out + 6 * N);
    StoreU(raw7, d, packed_out + 7 * N);
    StoreU(raw8, d, packed_out + 8 * N);
    StoreU(raw9, d, packed_out + 9 * N);
    StoreU(rawA, d, packed_out + 0xA * N);
    StoreU(rawB, d, packed_out + 0xB * N);
    StoreU(rawC, d, packed_out + 0xC * N);
    StoreU(rawD, d, packed_out + 0xD * N);
    StoreU(rawE, d, packed_out + 0xE * N);
    StoreU(rawF, d, packed_out + 0xF * N);
  }

  template <class D>
  HWY_INLINE void Unpack(D d, const uint16_t* HWY_RESTRICT packed_in,
                         uint16_t* HWY_RESTRICT raw) const {
    using VU16 = Vec<decltype(d)>;
    const size_t N = Lanes(d);

    const VU16 raw0 = BitCast(d, LoadU(d, packed_in + 0 * N));
    const VU16 raw1 = BitCast(d, LoadU(d, packed_in + 1 * N));
    const VU16 raw2 = BitCast(d, LoadU(d, packed_in + 2 * N));
    const VU16 raw3 = BitCast(d, LoadU(d, packed_in + 3 * N));
    const VU16 raw4 = BitCast(d, LoadU(d, packed_in + 4 * N));
    const VU16 raw5 = BitCast(d, LoadU(d, packed_in + 5 * N));
    const VU16 raw6 = BitCast(d, LoadU(d, packed_in + 6 * N));
    const VU16 raw7 = BitCast(d, LoadU(d, packed_in + 7 * N));
    const VU16 raw8 = BitCast(d, LoadU(d, packed_in + 8 * N));
    const VU16 raw9 = BitCast(d, LoadU(d, packed_in + 9 * N));
    const VU16 rawA = BitCast(d, LoadU(d, packed_in + 0xA * N));
    const VU16 rawB = BitCast(d, LoadU(d, packed_in + 0xB * N));
    const VU16 rawC = BitCast(d, LoadU(d, packed_in + 0xC * N));
    const VU16 rawD = BitCast(d, LoadU(d, packed_in + 0xD * N));
    const VU16 rawE = BitCast(d, LoadU(d, packed_in + 0xE * N));
    const VU16 rawF = BitCast(d, LoadU(d, packed_in + 0xF * N));

    StoreU(raw0, d, raw + 0 * N);
    StoreU(raw1, d, raw + 1 * N);
    StoreU(raw2, d, raw + 2 * N);
    StoreU(raw3, d, raw + 3 * N);
    StoreU(raw4, d, raw + 4 * N);
    StoreU(raw5, d, raw + 5 * N);
    StoreU(raw6, d, raw + 6 * N);
    StoreU(raw7, d, raw + 7 * N);
    StoreU(raw8, d, raw + 8 * N);
    StoreU(raw9, d, raw + 9 * N);
    StoreU(rawA, d, raw + 0xA * N);
    StoreU(rawB, d, raw + 0xB * N);
    StoreU(rawC, d, raw + 0xC * N);
    StoreU(rawD, d, raw + 0xD * N);
    StoreU(rawE, d, raw + 0xE * N);
    StoreU(rawF, d, raw + 0xF * N);
  }
};  // Pack16<16>

// The supported packing types for 32/64 bits.
enum BlockPackingType {
  // Simple fixed bit-packing.
  kBitPacked,
  // Bit packing after subtracting a `frame of reference` value from input.
  kFoRBitPacked,
};

namespace detail {

// Generates the implementation for bit-packing/un-packing `T` type numbers
// where each number takes `kBits` bits.
// `S` is the remainder bits left from the previous bit-packed block.
// `kLoadPos` is the offset from which the next vector block should be loaded.
// `kStorePos` is the offset into which the next vector block should be stored.
// `BlockPackingType` is the type of packing/unpacking for this block.
template <typename T, size_t kBits, size_t S, size_t kLoadPos, size_t kStorePos,
          BlockPackingType block_packing_type>
struct BitPackUnroller {
  static constexpr size_t B = sizeof(T) * 8;

  template <class D, typename V>
  static inline void Pack(D d, const T* HWY_RESTRICT raw,
                          T* HWY_RESTRICT packed_out, const V& mask,
                          const V& frame_of_reference, V& in, V& out) {
    // Avoid compilation errors and unnecessary template instantiation if
    // compiling in C++11 or C++14 mode
    using NextUnroller = BitPackUnroller<
        T, kBits, ((S <= B) ? (S + ((S < B) ? kBits : 0)) : (S % B)),
        kLoadPos + static_cast<size_t>(S < B),
        kStorePos + static_cast<size_t>(S > B), block_packing_type>;

    (void)raw;
    (void)mask;
    (void)in;

    const size_t N = Lanes(d);
    HWY_IF_CONSTEXPR(S >= B) {
      StoreU(out, d, packed_out + kStorePos * N);
      HWY_IF_CONSTEXPR(S == B) { return; }
      HWY_IF_CONSTEXPR(S != B) {
        constexpr size_t shr_amount = (kBits - S % B) % B;
        out = ShiftRight<shr_amount>(in);
        // NextUnroller is a typedef for
        // Unroller<T, kBits, S % B, kLoadPos, kStorePos + 1> if S > B is true
        return NextUnroller::Pack(d, raw, packed_out, mask, frame_of_reference,
                                  in, out);
      }
    }
    HWY_IF_CONSTEXPR(S < B) {
      HWY_IF_CONSTEXPR(block_packing_type == BlockPackingType::kBitPacked) {
        in = LoadU(d, raw + kLoadPos * N);
      }
      HWY_IF_CONSTEXPR(block_packing_type == BlockPackingType::kFoRBitPacked) {
        in = Sub(LoadU(d, raw + kLoadPos * N), frame_of_reference);
      }
      // Optimize for the case when `S` is zero.
      // We can skip `Or` + ShiftLeft` to align `in`.
      HWY_IF_CONSTEXPR(S == 0) { out = in; }
      HWY_IF_CONSTEXPR(S != 0) { out = Or(out, ShiftLeft<S % B>(in)); }
      // NextUnroller is a typedef for
      // Unroller<T, kBits, S + kBits, kLoadPos + 1, kStorePos> if S < B is true
      return NextUnroller::Pack(d, raw, packed_out, mask, frame_of_reference,
                                in, out);
    }
  }

  template <class D, typename V>
  static inline void Unpack(D d, const T* HWY_RESTRICT packed_in,
                            T* HWY_RESTRICT raw, const V& mask,
                            const V& frame_of_reference, V& in, V& out) {
    // Avoid compilation errors and unnecessary template instantiation if
    // compiling in C++11 or C++14 mode
    using NextUnroller = BitPackUnroller<
        T, kBits, ((S <= B) ? (S + ((S < B) ? kBits : 0)) : (S % B)),
        kLoadPos + static_cast<size_t>(S > B),
        kStorePos + static_cast<size_t>(S < B), block_packing_type>;

    (void)packed_in;
    (void)mask;
    (void)in;

    const size_t N = Lanes(d);
    HWY_IF_CONSTEXPR(S >= B) {
      HWY_IF_CONSTEXPR(S == B) {
        V bitpacked_output = out;
        HWY_IF_CONSTEXPR(block_packing_type ==
                         BlockPackingType::kFoRBitPacked) {
          bitpacked_output = Add(bitpacked_output, frame_of_reference);
        }
        StoreU(bitpacked_output, d, raw + kStorePos * N);
        return;
      }
      HWY_IF_CONSTEXPR(S != B) {
        in = LoadU(d, packed_in + kLoadPos * N);
        constexpr size_t shl_amount = (kBits - S % B) % B;
        out = And(Or(out, ShiftLeft<shl_amount>(in)), mask);
        // NextUnroller is a typedef for
        // Unroller<T, kBits, S % B, kLoadPos + 1, kStorePos> if S > B is true
        return NextUnroller::Unpack(d, packed_in, raw, mask, frame_of_reference,
                                    in, out);
      }
    }
    HWY_IF_CONSTEXPR(S < B) {
      V bitpacked_output = out;
      HWY_IF_CONSTEXPR(block_packing_type == BlockPackingType::kFoRBitPacked) {
        bitpacked_output = Add(bitpacked_output, frame_of_reference);
      }
      StoreU(bitpacked_output, d, raw + kStorePos * N);
      HWY_IF_CONSTEXPR(S + kBits < B) {
        // Optimize for the case when `S` is zero.
        // We can skip the `ShiftRight` to align `in`.
        HWY_IF_CONSTEXPR(S == 0) { out = And(in, mask); }
        HWY_IF_CONSTEXPR(S != 0) { out = And(ShiftRight<S % B>(in), mask); }
      }
      HWY_IF_CONSTEXPR(S + kBits >= B) { out = ShiftRight<S % B>(in); }
      // NextUnroller is a typedef for
      // Unroller<T, kBits, S + kBits, kLoadPos, kStorePos + 1> if S < B is true
      return NextUnroller::Unpack(d, packed_in, raw, mask, frame_of_reference,
                                  in, out);
    }
  }
};

// Computes the highest power of two that divides `kBits`.
template <size_t kBits>
constexpr size_t NumLoops() {
  return (kBits & ~(kBits - 1));
}

template <size_t kBits>
constexpr size_t PackedIncr() {
  return kBits / NumLoops<kBits>();
}

template <typename T, size_t kBits>
constexpr size_t UnpackedIncr() {
  return (sizeof(T) * 8) / NumLoops<kBits>();
}

template <size_t kBits>
constexpr uint32_t MaskBits32() {
  return static_cast<uint32_t>((1ull << kBits) - 1);
}

template <size_t kBits>
constexpr uint64_t MaskBits64() {
  return (uint64_t{1} << kBits) - 1;
}
template <>
constexpr uint64_t MaskBits64<64>() {
  return ~uint64_t{0};
}

}  // namespace detail

template <size_t kBits>  // <= 32
struct Pack32 {
  template <class D,
            BlockPackingType block_packing_type = BlockPackingType::kBitPacked>
  HWY_INLINE void Pack(D d, const uint32_t* HWY_RESTRICT raw,
                       uint32_t* HWY_RESTRICT packed_out,
                       const uint32_t frame_of_reference_value = 0) const {
    using V = VFromD<D>;
    const V mask = Set(d, detail::MaskBits32<kBits>());
    const V frame_of_reference = Set(d, frame_of_reference_value);
    for (size_t i = 0; i < detail::NumLoops<kBits>(); ++i) {
      V in = Zero(d);
      V out = Zero(d);
      detail::BitPackUnroller<uint32_t, kBits, 0, 0, 0,
                              block_packing_type>::Pack(d, raw, packed_out,
                                                        mask,
                                                        frame_of_reference, in,
                                                        out);
      raw += detail::UnpackedIncr<uint32_t, kBits>() * Lanes(d);
      packed_out += detail::PackedIncr<kBits>() * Lanes(d);
    }
  }

  template <class D,
            BlockPackingType block_packing_type = BlockPackingType::kBitPacked>
  HWY_INLINE void Unpack(D d, const uint32_t* HWY_RESTRICT packed_in,
                         uint32_t* HWY_RESTRICT raw,
                         const uint32_t frame_of_reference_value = 0) const {
    using V = VFromD<D>;
    const V mask = Set(d, detail::MaskBits32<kBits>());
    const V frame_of_reference = Set(d, frame_of_reference_value);
    for (size_t i = 0; i < detail::NumLoops<kBits>(); ++i) {
      V in = LoadU(d, packed_in + 0 * Lanes(d));
      V out = And(in, mask);
      detail::BitPackUnroller<uint32_t, kBits, kBits, 1, 0,
                              block_packing_type>::Unpack(d, packed_in, raw,
                                                          mask,
                                                          frame_of_reference,
                                                          in, out);
      raw += detail::UnpackedIncr<uint32_t, kBits>() * Lanes(d);
      packed_in += detail::PackedIncr<kBits>() * Lanes(d);
    }
  }
};

template <size_t kBits>  // <= 64
struct Pack64 {
  template <class D,
            BlockPackingType block_packing_type = BlockPackingType::kBitPacked>
  HWY_INLINE void Pack(D d, const uint64_t* HWY_RESTRICT raw,
                       uint64_t* HWY_RESTRICT packed_out,
                       const uint64_t frame_of_reference_value = 0) const {
    using V = VFromD<D>;
    const V mask = Set(d, detail::MaskBits64<kBits>());
    const V frame_of_reference = Set(d, frame_of_reference_value);
    for (size_t i = 0; i < detail::NumLoops<kBits>(); ++i) {
      V in = Zero(d);
      V out = Zero(d);
      detail::BitPackUnroller<uint64_t, kBits, 0, 0, 0,
                              block_packing_type>::Pack(d, raw, packed_out,
                                                        mask,
                                                        frame_of_reference, in,
                                                        out);
      raw += detail::UnpackedIncr<uint64_t, kBits>() * Lanes(d);
      packed_out += detail::PackedIncr<kBits>() * Lanes(d);
    }
  }

  template <class D,
            BlockPackingType block_packing_type = BlockPackingType::kBitPacked>
  HWY_INLINE void Unpack(D d, const uint64_t* HWY_RESTRICT packed_in,
                         uint64_t* HWY_RESTRICT raw,
                         const uint64_t frame_of_reference_value = 0) const {
    using V = VFromD<D>;
    const V mask = Set(d, detail::MaskBits64<kBits>());
    const V frame_of_reference = Set(d, frame_of_reference_value);
    for (size_t i = 0; i < detail::NumLoops<kBits>(); ++i) {
      V in = LoadU(d, packed_in + 0 * Lanes(d));
      V out = And(in, mask);
      detail::BitPackUnroller<uint64_t, kBits, kBits, 1, 0,
                              block_packing_type>::Unpack(d, packed_in, raw,
                                                          mask,
                                                          frame_of_reference,
                                                          in, out);
      raw += detail::UnpackedIncr<uint64_t, kBits>() * Lanes(d);
      packed_in += detail::PackedIncr<kBits>() * Lanes(d);
    }
  }
};

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#endif  // HIGHWAY_HWY_CONTRIB_BIT_PACK_INL_H_
