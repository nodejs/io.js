// Copyright 2024 Google LLC
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

#ifndef HIGHWAY_HWY_BIT_SET_H_
#define HIGHWAY_HWY_BIT_SET_H_

// BitSet with fast Foreach for up to 64 and 4096 members.

#include <stddef.h>

#include "hwy/base.h"

namespace hwy {

// 64-bit specialization of std::bitset, which lacks Foreach.
class BitSet64 {
 public:
  // No harm if `i` is already set.
  void Set(size_t i) {
    HWY_DASSERT(i < 64);
    bits_ |= (1ULL << i);
    HWY_DASSERT(Get(i));
  }

  // Equivalent to Set(i) for i in [0, 64) where (bits >> i) & 1. This does
  // not clear any existing bits.
  void SetNonzeroBitsFrom64(uint64_t bits) { bits_ |= bits; }

  void Clear(size_t i) {
    HWY_DASSERT(i < 64);
    bits_ &= ~(1ULL << i);
  }

  bool Get(size_t i) const {
    HWY_DASSERT(i < 64);
    return (bits_ & (1ULL << i)) != 0;
  }

  // Returns true if any Get(i) would return true for i in [0, 64).
  bool Any() const { return bits_ != 0; }

  // Returns lowest i such that Get(i). Caller must ensure Any() beforehand!
  size_t First() const {
    HWY_DASSERT(Any());
    return Num0BitsBelowLS1Bit_Nonzero64(bits_);
  }

  // Returns uint64_t(Get(i)) << i for i in [0, 64).
  uint64_t Get64() const { return bits_; }

  // Calls `func(i)` for each `i` in the set. It is safe for `func` to modify
  // the set, but the current Foreach call is unaffected.
  template <class Func>
  void Foreach(const Func& func) const {
    uint64_t remaining_bits = bits_;
    while (remaining_bits != 0) {
      const size_t i = Num0BitsBelowLS1Bit_Nonzero64(remaining_bits);
      remaining_bits &= remaining_bits - 1;  // clear LSB
      func(i);
    }
  }

  size_t Count() const { return PopCount(bits_); }

 private:
  uint64_t bits_ = 0;
};

// Two-level bitset for up to kMaxSize <= 4096 values.
template <size_t kMaxSize = 4096>
class BitSet4096 {
 public:
  // No harm if `i` is already set.
  void Set(size_t i) {
    HWY_DASSERT(i < kMaxSize);
    const size_t idx = i / 64;
    const size_t mod = i % 64;
    bits_[idx].Set(mod);
    nonzero_.Set(idx);
    HWY_DASSERT(Get(i));
  }

  // Equivalent to Set(i) for i in [0, 64) where (bits >> i) & 1. This does
  // not clear any existing bits.
  void SetNonzeroBitsFrom64(uint64_t bits) {
    bits_[0].SetNonzeroBitsFrom64(bits);
    if (bits) nonzero_.Set(0);
  }

  void Clear(size_t i) {
    HWY_DASSERT(i < kMaxSize);
    const size_t idx = i / 64;
    const size_t mod = i % 64;
    bits_[idx].Clear(mod);
    if (!bits_[idx].Any()) {
      nonzero_.Clear(idx);
    }
    HWY_DASSERT(!Get(i));
  }

  bool Get(size_t i) const {
    HWY_DASSERT(i < kMaxSize);
    const size_t idx = i / 64;
    const size_t mod = i % 64;
    return bits_[idx].Get(mod);
  }

  // Returns true if any Get(i) would return true for i in [0, 64).
  bool Any() const { return nonzero_.Any(); }

  // Returns lowest i such that Get(i). Caller must ensure Any() beforehand!
  size_t First() const {
    HWY_DASSERT(Any());
    const size_t idx = nonzero_.First();
    return idx * 64 + bits_[idx].First();
  }

  // Returns uint64_t(Get(i)) << i for i in [0, 64).
  uint64_t Get64() const { return bits_[0].Get64(); }

  // Calls `func(i)` for each `i` in the set. It is safe for `func` to modify
  // the set, but the current Foreach call is only affected if changing one of
  // the not yet visited BitSet64 for which Any() is true.
  template <class Func>
  void Foreach(const Func& func) const {
    nonzero_.Foreach([&func, this](size_t idx) {
      bits_[idx].Foreach([idx, &func](size_t mod) { func(idx * 64 + mod); });
    });
  }

  size_t Count() const {
    size_t total = 0;
    nonzero_.Foreach(
        [&total, this](size_t idx) { total += bits_[idx].Count(); });
    return total;
  }

 private:
  static_assert(kMaxSize <= 64 * 64, "One BitSet64 insufficient");
  BitSet64 nonzero_;
  BitSet64 bits_[kMaxSize / 64];
};

}  // namespace hwy

#endif  // HIGHWAY_HWY_BIT_SET_H_
