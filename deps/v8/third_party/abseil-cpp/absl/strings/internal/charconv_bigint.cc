// Copyright 2018 The Abseil Authors.
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

#include "absl/strings/internal/charconv_bigint.h"

#include <algorithm>
#include <cassert>
#include <string>

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

namespace {

// Table containing some large powers of 5, for fast computation.

// Constant step size for entries in the kLargePowersOfFive table.  Each entry
// is larger than the previous entry by a factor of 5**kLargePowerOfFiveStep
// (or 5**27).
//
// In other words, the Nth entry in the table is 5**(27*N).
//
// 5**27 is the largest power of 5 that fits in 64 bits.
constexpr int kLargePowerOfFiveStep = 27;

// The largest legal index into the kLargePowersOfFive table.
//
// In other words, the largest precomputed power of 5 is 5**(27*20).
constexpr int kLargestPowerOfFiveIndex = 20;

// Table of powers of (5**27), up to (5**27)**20 == 5**540.
//
// Used to generate large powers of 5 while limiting the number of repeated
// multiplications required.
//
// clang-format off
const uint32_t kLargePowersOfFive[] = {
// 5**27 (i=1), start=0, end=2
  0xfa10079dU, 0x6765c793U,
// 5**54 (i=2), start=2, end=6
  0x97d9f649U, 0x6664242dU, 0x29939b14U, 0x29c30f10U,
// 5**81 (i=3), start=6, end=12
  0xc4f809c5U, 0x7bf3f22aU, 0x67bdae34U, 0xad340517U, 0x369d1b5fU, 0x10de1593U,
// 5**108 (i=4), start=12, end=20
  0x92b260d1U, 0x9efff7c7U, 0x81de0ec6U, 0xaeba5d56U, 0x410664a4U, 0x4f40737aU,
  0x20d3846fU, 0x06d00f73U,
// 5**135 (i=5), start=20, end=30
  0xff1b172dU, 0x13a1d71cU, 0xefa07617U, 0x7f682d3dU, 0xff8c90c0U, 0x3f0131e7U,
  0x3fdcb9feU, 0x917b0177U, 0x16c407a7U, 0x02c06b9dU,
// 5**162 (i=6), start=30, end=42
  0x960f7199U, 0x056667ecU, 0xe07aefd8U, 0x80f2b9ccU, 0x8273f5e3U, 0xeb9a214aU,
  0x40b38005U, 0x0e477ad4U, 0x277d08e6U, 0xfa28b11eU, 0xd3f7d784U, 0x011c835bU,
// 5**189 (i=7), start=42, end=56
  0xf723d9d5U, 0x3282d3f3U, 0xe00857d1U, 0x69659d25U, 0x2cf117cfU, 0x24da6d07U,
  0x954d1417U, 0x3e5d8cedU, 0x7a8bb766U, 0xfd785ae6U, 0x645436d2U, 0x40c78b34U,
  0x94151217U, 0x0072e9f7U,
// 5**216 (i=8), start=56, end=72
  0x2b416aa1U, 0x7893c5a7U, 0xe37dc6d4U, 0x2bad2beaU, 0xf0fc846cU, 0x7575ae4bU,
  0x62587b14U, 0x83b67a34U, 0x02110cdbU, 0xf7992f55U, 0x00deb022U, 0xa4a23becU,
  0x8af5c5cdU, 0xb85b654fU, 0x818df38bU, 0x002e69d2U,
// 5**243 (i=9), start=72, end=90
  0x3518cbbdU, 0x20b0c15fU, 0x38756c2fU, 0xfb5dc3ddU, 0x22ad2d94U, 0xbf35a952U,
  0xa699192aU, 0x9a613326U, 0xad2a9cedU, 0xd7f48968U, 0xe87dfb54U, 0xc8f05db6U,
  0x5ef67531U, 0x31c1ab49U, 0xe202ac9fU, 0x9b2957b5U, 0xa143f6d3U, 0x0012bf07U,
// 5**270 (i=10), start=90, end=110
  0x8b971de9U, 0x21aba2e1U, 0x63944362U, 0x57172336U, 0xd9544225U, 0xfb534166U,
  0x08c563eeU, 0x14640ee2U, 0x24e40d31U, 0x02b06537U, 0x03887f14U, 0x0285e533U,
  0xb744ef26U, 0x8be3a6c4U, 0x266979b4U, 0x6761ece2U, 0xd9cb39e4U, 0xe67de319U,
  0x0d39e796U, 0x00079250U,
// 5**297 (i=11), start=110, end=132
  0x260eb6e5U, 0xf414a796U, 0xee1a7491U, 0xdb9368ebU, 0xf50c105bU, 0x59157750U,
  0x9ed2fb5cU, 0xf6e56d8bU, 0xeaee8d23U, 0x0f319f75U, 0x2aa134d6U, 0xac2908e9U,
  0xd4413298U, 0x02f02a55U, 0x989d5a7aU, 0x70dde184U, 0xba8040a7U, 0x03200981U,
  0xbe03b11cU, 0x3c1c2a18U, 0xd60427a1U, 0x00030ee0U,
// 5**324 (i=12), start=132, end=156
  0xce566d71U, 0xf1c4aa25U, 0x4e93ca53U, 0xa72283d0U, 0x551a73eaU, 0x3d0538e2U,
  0x8da4303fU, 0x6a58de60U, 0x0e660221U, 0x49cf61a6U, 0x8d058fc1U, 0xb9d1a14cU,
  0x4bab157dU, 0xc85c6932U, 0x518c8b9eU, 0x9b92b8d0U, 0x0d8a0e21U, 0xbd855df9U,
  0xb3ea59a1U, 0x8da29289U, 0x4584d506U, 0x3752d80fU, 0xb72569c6U, 0x00013c33U,
// 5**351 (i=13), start=156, end=182
  0x190f354dU, 0x83695cfeU, 0xe5a4d0c7U, 0xb60fb7e8U, 0xee5bbcc4U, 0xb922054cU,
  0xbb4f0d85U, 0x48394028U, 0x1d8957dbU, 0x0d7edb14U, 0x4ecc7587U, 0x505e9e02U,
  0x4c87f36bU, 0x99e66bd6U, 0x44b9ed35U, 0x753037d4U, 0xe5fe5f27U, 0x2742c203U,
  0x13b2ed2bU, 0xdc525d2cU, 0xe6fde59aU, 0x77ffb18fU, 0x13c5752cU, 0x08a84bccU,
  0x859a4940U, 0x00007fb6U,
// 5**378 (i=14), start=182, end=210
  0x4f98cb39U, 0xa60edbbcU, 0x83b5872eU, 0xa501acffU, 0x9cc76f78U, 0xbadd4c73U,
  0x43e989faU, 0xca7acf80U, 0x2e0c824fU, 0xb19f4ffcU, 0x092fd81cU, 0xe4eb645bU,
  0xa1ff84c2U, 0x8a5a83baU, 0xa8a1fae9U, 0x1db43609U, 0xb0fed50bU, 0x0dd7d2bdU,
  0x7d7accd8U, 0x91fa640fU, 0x37dcc6c5U, 0x1c417fd5U, 0xe4d462adU, 0xe8a43399U,
  0x131bf9a5U, 0x8df54d29U, 0x36547dc1U, 0x00003395U,
// 5**405 (i=15), start=210, end=240
  0x5bd330f5U, 0x77d21967U, 0x1ac481b7U, 0x6be2f7ceU, 0x7f4792a9U, 0xe84c2c52U,
  0x84592228U, 0x9dcaf829U, 0xdab44ce1U, 0x3d0c311bU, 0x532e297dU, 0x4704e8b4U,
  0x9cdc32beU, 0x41e64d9dU, 0x7717bea1U, 0xa824c00dU, 0x08f50b27U, 0x0f198d77U,
  0x49bbfdf0U, 0x025c6c69U, 0xd4e55cd3U, 0xf083602bU, 0xb9f0fecdU, 0xc0864aeaU,
  0x9cb98681U, 0xaaf620e9U, 0xacb6df30U, 0x4faafe66U, 0x8af13c3bU, 0x000014d5U,
// 5**432 (i=16), start=240, end=272
  0x682bb941U, 0x89a9f297U, 0xcba75d7bU, 0x404217b1U, 0xb4e519e9U, 0xa1bc162bU,
  0xf7f5910aU, 0x98715af5U, 0x2ff53e57U, 0xe3ef118cU, 0x490c4543U, 0xbc9b1734U,
  0x2affbe4dU, 0x4cedcb4cU, 0xfb14e99eU, 0x35e34212U, 0xece39c24U, 0x07673ab3U,
  0xe73115ddU, 0xd15d38e7U, 0x093eed3bU, 0xf8e7eac5U, 0x78a8cc80U, 0x25227aacU,
  0x3f590551U, 0x413da1cbU, 0xdf643a55U, 0xab65ad44U, 0xd70b23d7U, 0xc672cd76U,
  0x3364ea62U, 0x0000086aU,
// 5**459 (i=17), start=272, end=306
  0x22f163ddU, 0x23cf07acU, 0xbe2af6c2U, 0xf412f6f6U, 0xc3ff541eU, 0x6eeaf7deU,
  0xa47047e0U, 0x408cda92U, 0x0f0eeb08U, 0x56deba9dU, 0xcfc6b090U, 0x8bbbdf04U,
  0x3933cdb3U, 0x9e7bb67dU, 0x9f297035U, 0x38946244U, 0xee1d37bbU, 0xde898174U,
  0x63f3559dU, 0x705b72fbU, 0x138d27d9U, 0xf8603a78U, 0x735eec44U, 0xe30987d5U,
  0xc6d38070U, 0x9cfe548eU, 0x9ff01422U, 0x7c564aa8U, 0x91cc60baU, 0xcbc3565dU,
  0x7550a50bU, 0x6909aeadU, 0x13234c45U, 0x00000366U,
// 5**486 (i=18), start=306, end=342
  0x17954989U, 0x3a7d7709U, 0x98042de5U, 0xa9011443U, 0x45e723c2U, 0x269ffd6fU,
  0x58852a46U, 0xaaa1042aU, 0x2eee8153U, 0xb2b6c39eU, 0xaf845b65U, 0xf6c365d7U,
  0xe4cffb2bU, 0xc840e90cU, 0xabea8abbU, 0x5c58f8d2U, 0x5c19fa3aU, 0x4670910aU,
  0x4449f21cU, 0xefa645b3U, 0xcc427decU, 0x083c3d73U, 0x467cb413U, 0x6fe10ae4U,
  0x3caffc72U, 0x9f8da55eU, 0x5e5c8ea7U, 0x490594bbU, 0xf0871b0bU, 0xdd89816cU,
  0x8e931df8U, 0xe85ce1c9U, 0xcca090a5U, 0x575fa16bU, 0x6b9f106cU, 0x0000015fU,
// 5**513 (i=19), start=342, end=380
  0xee20d805U, 0x57bc3c07U, 0xcdea624eU, 0xd3f0f52dU, 0x9924b4f4U, 0xcf968640U,
  0x61d41962U, 0xe87fb464U, 0xeaaf51c7U, 0x564c8b60U, 0xccda4028U, 0x529428bbU,
  0x313a1fa8U, 0x96bd0f94U, 0x7a82ebaaU, 0xad99e7e9U, 0xf2668cd4U, 0xbe33a45eU,
  0xfd0db669U, 0x87ee369fU, 0xd3ec20edU, 0x9c4d7db7U, 0xdedcf0d8U, 0x7cd2ca64U,
  0xe25a6577U, 0x61003fd4U, 0xe56f54ccU, 0x10b7c748U, 0x40526e5eU, 0x7300ae87U,
  0x5c439261U, 0x2c0ff469U, 0xbf723f12U, 0xb2379b61U, 0xbf59b4f5U, 0xc91b1c3fU,
  0xf0046d27U, 0x0000008dU,
// 5**540 (i=20), start=380, end=420
  0x525c9e11U, 0xf4e0eb41U, 0xebb2895dU, 0x5da512f9U, 0x7d9b29d4U, 0x452f4edcU,
  0x0b90bc37U, 0x341777cbU, 0x63d269afU, 0x1da77929U, 0x0a5c1826U, 0x77991898U,
  0x5aeddf86U, 0xf853a877U, 0x538c31ccU, 0xe84896daU, 0xb7a0010bU, 0x17ef4de5U,
  0xa52a2adeU, 0x029fd81cU, 0x987ce701U, 0x27fefd77U, 0xdb46c66fU, 0x5d301900U,
  0x496998c0U, 0xbb6598b9U, 0x5eebb607U, 0xe547354aU, 0xdf4a2f7eU, 0xf06c4955U,
  0x96242ffaU, 0x1775fb27U, 0xbecc58ceU, 0xebf2a53bU, 0x3eaad82aU, 0xf41137baU,
  0x573e6fbaU, 0xfb4866b8U, 0x54002148U, 0x00000039U,
};
// clang-format on

// Returns a pointer to the big integer data for (5**27)**i.  i must be
// between 1 and 20, inclusive.
const uint32_t* LargePowerOfFiveData(int i) {
  return kLargePowersOfFive + i * (i - 1);
}

// Returns the size of the big integer data for (5**27)**i, in words.  i must be
// between 1 and 20, inclusive.
int LargePowerOfFiveSize(int i) { return 2 * i; }
}  // namespace

ABSL_DLL const uint32_t kFiveToNth[14] = {
    1,     5,      25,      125,     625,      3125,      15625,
    78125, 390625, 1953125, 9765625, 48828125, 244140625, 1220703125,
};

ABSL_DLL const uint32_t kTenToNth[10] = {
    1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000,
};

template <int max_words>
int BigUnsigned<max_words>::ReadFloatMantissa(const ParsedFloat& fp,
                                              int significant_digits) {
  SetToZero();
  assert(fp.type == FloatType::kNumber);

  if (fp.subrange_begin == nullptr) {
    // We already exactly parsed the mantissa, so no more work is necessary.
    words_[0] = fp.mantissa & 0xffffffffu;
    words_[1] = fp.mantissa >> 32;
    if (words_[1]) {
      size_ = 2;
    } else if (words_[0]) {
      size_ = 1;
    }
    return fp.exponent;
  }
  int exponent_adjust =
      ReadDigits(fp.subrange_begin, fp.subrange_end, significant_digits);
  return fp.literal_exponent + exponent_adjust;
}

template <int max_words>
int BigUnsigned<max_words>::ReadDigits(const char* begin, const char* end,
                                       int significant_digits) {
  assert(significant_digits <= Digits10() + 1);
  SetToZero();

  bool after_decimal_point = false;
  // Discard any leading zeroes before the decimal point
  while (begin < end && *begin == '0') {
    ++begin;
  }
  int dropped_digits = 0;
  // Discard any trailing zeroes.  These may or may not be after the decimal
  // point.
  while (begin < end && *std::prev(end) == '0') {
    --end;
    ++dropped_digits;
  }
  if (begin < end && *std::prev(end) == '.') {
    // If the string ends in '.', either before or after dropping zeroes, then
    // drop the decimal point and look for more digits to drop.
    dropped_digits = 0;
    --end;
    while (begin < end && *std::prev(end) == '0') {
      --end;
      ++dropped_digits;
    }
  } else if (dropped_digits) {
    // We dropped digits, and aren't sure if they're before or after the decimal
    // point.  Figure that out now.
    const char* dp = std::find(begin, end, '.');
    if (dp != end) {
      // The dropped trailing digits were after the decimal point, so don't
      // count them.
      dropped_digits = 0;
    }
  }
  // Any non-fraction digits we dropped need to be accounted for in our exponent
  // adjustment.
  int exponent_adjust = dropped_digits;

  uint32_t queued = 0;
  int digits_queued = 0;
  for (; begin != end && significant_digits > 0; ++begin) {
    if (*begin == '.') {
      after_decimal_point = true;
      continue;
    }
    if (after_decimal_point) {
      // For each fractional digit we emit in our parsed integer, adjust our
      // decimal exponent to compensate.
      --exponent_adjust;
    }
    char digit = (*begin - '0');
    --significant_digits;
    if (significant_digits == 0 && std::next(begin) != end &&
        (digit == 0 || digit == 5)) {
      // If this is the very last significant digit, but insignificant digits
      // remain, we know that the last of those remaining significant digits is
      // nonzero.  (If it wasn't, we would have stripped it before we got here.)
      // So if this final digit is a 0 or 5, adjust it upward by 1.
      //
      // This adjustment is what allows incredibly large mantissas ending in
      // 500000...000000000001 to correctly round up, rather than to nearest.
      ++digit;
    }
    queued = 10 * queued + static_cast<uint32_t>(digit);
    ++digits_queued;
    if (digits_queued == kMaxSmallPowerOfTen) {
      MultiplyBy(kTenToNth[kMaxSmallPowerOfTen]);
      AddWithCarry(0, queued);
      queued = digits_queued = 0;
    }
  }
  // Encode any remaining digits.
  if (digits_queued) {
    MultiplyBy(kTenToNth[digits_queued]);
    AddWithCarry(0, queued);
  }

  // If any insignificant digits remain, we will drop them.  But if we have not
  // yet read the decimal point, then we have to adjust the exponent to account
  // for the dropped digits.
  if (begin < end && !after_decimal_point) {
    // This call to std::find will result in a pointer either to the decimal
    // point, or to the end of our buffer if there was none.
    //
    // Either way, [begin, decimal_point) will contain the set of dropped digits
    // that require an exponent adjustment.
    const char* decimal_point = std::find(begin, end, '.');
    exponent_adjust += (decimal_point - begin);
  }
  return exponent_adjust;
}

template <int max_words>
/* static */ BigUnsigned<max_words> BigUnsigned<max_words>::FiveToTheNth(
    int n) {
  BigUnsigned answer(1u);

  // Seed from the table of large powers, if possible.
  bool first_pass = true;
  while (n >= kLargePowerOfFiveStep) {
    int big_power =
        std::min(n / kLargePowerOfFiveStep, kLargestPowerOfFiveIndex);
    if (first_pass) {
      // just copy, rather than multiplying by 1
      std::copy_n(LargePowerOfFiveData(big_power),
                  LargePowerOfFiveSize(big_power), answer.words_);
      answer.size_ = LargePowerOfFiveSize(big_power);
      first_pass = false;
    } else {
      answer.MultiplyBy(LargePowerOfFiveSize(big_power),
                        LargePowerOfFiveData(big_power));
    }
    n -= kLargePowerOfFiveStep * big_power;
  }
  answer.MultiplyByFiveToTheNth(n);
  return answer;
}

template <int max_words>
void BigUnsigned<max_words>::MultiplyStep(int original_size,
                                          const uint32_t* other_words,
                                          int other_size, int step) {
  int this_i = std::min(original_size - 1, step);
  int other_i = step - this_i;

  uint64_t this_word = 0;
  uint64_t carry = 0;
  for (; this_i >= 0 && other_i < other_size; --this_i, ++other_i) {
    uint64_t product = words_[this_i];
    product *= other_words[other_i];
    this_word += product;
    carry += (this_word >> 32);
    this_word &= 0xffffffff;
  }
  AddWithCarry(step + 1, carry);
  words_[step] = this_word & 0xffffffff;
  if (this_word > 0 && size_ <= step) {
    size_ = step + 1;
  }
}

template <int max_words>
std::string BigUnsigned<max_words>::ToString() const {
  BigUnsigned<max_words> copy = *this;
  std::string result;
  // Build result in reverse order
  while (copy.size() > 0) {
    uint32_t next_digit = copy.DivMod<10>();
    result.push_back('0' + static_cast<char>(next_digit));
  }
  if (result.empty()) {
    result.push_back('0');
  }
  std::reverse(result.begin(), result.end());
  return result;
}

template class BigUnsigned<4>;
template class BigUnsigned<84>;

}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl
