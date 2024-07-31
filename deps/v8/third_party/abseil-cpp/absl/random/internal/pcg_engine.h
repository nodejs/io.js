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

#ifndef ABSL_RANDOM_INTERNAL_PCG_ENGINE_H_
#define ABSL_RANDOM_INTERNAL_PCG_ENGINE_H_

#include <type_traits>

#include "absl/base/config.h"
#include "absl/meta/type_traits.h"
#include "absl/numeric/bits.h"
#include "absl/numeric/int128.h"
#include "absl/random/internal/fastmath.h"
#include "absl/random/internal/iostream_state_saver.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// pcg_engine is a simplified implementation of Melissa O'Neil's PCG engine in
// C++.  PCG combines a linear congruential generator (LCG) with output state
// mixing functions to generate each random variate.  pcg_engine supports only a
// single sequence (oneseq), and does not support streams.
//
// pcg_engine is parameterized by two types:
//   Params, which provides the multiplier and increment values;
//   Mix, which mixes the state into the result.
//
template <typename Params, typename Mix>
class pcg_engine {
  static_assert(std::is_same<typename Params::state_type,
                             typename Mix::state_type>::value,
                "Class-template absl::pcg_engine must be parameterized by "
                "Params and Mix with identical state_type");

  static_assert(std::is_unsigned<typename Mix::result_type>::value,
                "Class-template absl::pcg_engine must be parameterized by "
                "an unsigned Mix::result_type");

  using params_type = Params;
  using mix_type = Mix;
  using state_type = typename Mix::state_type;

 public:
  // C++11 URBG interface:
  using result_type = typename Mix::result_type;

  static constexpr result_type(min)() {
    return (std::numeric_limits<result_type>::min)();
  }

  static constexpr result_type(max)() {
    return (std::numeric_limits<result_type>::max)();
  }

  explicit pcg_engine(uint64_t seed_value = 0) { seed(seed_value); }

  template <class SeedSequence,
            typename = typename absl::enable_if_t<
                !std::is_same<SeedSequence, pcg_engine>::value>>
  explicit pcg_engine(SeedSequence&& seq) {
    seed(seq);
  }

  pcg_engine(const pcg_engine&) = default;
  pcg_engine& operator=(const pcg_engine&) = default;
  pcg_engine(pcg_engine&&) = default;
  pcg_engine& operator=(pcg_engine&&) = default;

  result_type operator()() {
    // Advance the LCG state, always using the new value to generate the output.
    state_ = lcg(state_);
    return Mix{}(state_);
  }

  void seed(uint64_t seed_value = 0) {
    state_type tmp = seed_value;
    state_ = lcg(tmp + Params::increment());
  }

  template <class SeedSequence>
  typename absl::enable_if_t<
      !std::is_convertible<SeedSequence, uint64_t>::value, void>
  seed(SeedSequence&& seq) {
    reseed(seq);
  }

  void discard(uint64_t count) { state_ = advance(state_, count); }

  bool operator==(const pcg_engine& other) const {
    return state_ == other.state_;
  }

  bool operator!=(const pcg_engine& other) const { return !(*this == other); }

  template <class CharT, class Traits>
  friend typename absl::enable_if_t<(sizeof(state_type) == 16),
                                    std::basic_ostream<CharT, Traits>&>
  operator<<(
      std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
      const pcg_engine& engine) {
    auto saver = random_internal::make_ostream_state_saver(os);
    random_internal::stream_u128_helper<state_type> helper;
    helper.write(pcg_engine::params_type::multiplier(), os);
    os << os.fill();
    helper.write(pcg_engine::params_type::increment(), os);
    os << os.fill();
    helper.write(engine.state_, os);
    return os;
  }

  template <class CharT, class Traits>
  friend typename absl::enable_if_t<(sizeof(state_type) <= 8),
                                    std::basic_ostream<CharT, Traits>&>
  operator<<(
      std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
      const pcg_engine& engine) {
    auto saver = random_internal::make_ostream_state_saver(os);
    os << pcg_engine::params_type::multiplier() << os.fill();
    os << pcg_engine::params_type::increment() << os.fill();
    os << engine.state_;
    return os;
  }

  template <class CharT, class Traits>
  friend typename absl::enable_if_t<(sizeof(state_type) == 16),
                                    std::basic_istream<CharT, Traits>&>
  operator>>(
      std::basic_istream<CharT, Traits>& is,  // NOLINT(runtime/references)
      pcg_engine& engine) {                   // NOLINT(runtime/references)
    random_internal::stream_u128_helper<state_type> helper;
    auto mult = helper.read(is);
    auto inc = helper.read(is);
    auto tmp = helper.read(is);
    if (mult != pcg_engine::params_type::multiplier() ||
        inc != pcg_engine::params_type::increment()) {
      // signal failure by setting the failbit.
      is.setstate(is.rdstate() | std::ios_base::failbit);
    }
    if (!is.fail()) {
      engine.state_ = tmp;
    }
    return is;
  }

  template <class CharT, class Traits>
  friend typename absl::enable_if_t<(sizeof(state_type) <= 8),
                                    std::basic_istream<CharT, Traits>&>
  operator>>(
      std::basic_istream<CharT, Traits>& is,  // NOLINT(runtime/references)
      pcg_engine& engine) {                   // NOLINT(runtime/references)
    state_type mult{}, inc{}, tmp{};
    is >> mult >> inc >> tmp;
    if (mult != pcg_engine::params_type::multiplier() ||
        inc != pcg_engine::params_type::increment()) {
      // signal failure by setting the failbit.
      is.setstate(is.rdstate() | std::ios_base::failbit);
    }
    if (!is.fail()) {
      engine.state_ = tmp;
    }
    return is;
  }

 private:
  state_type state_;

  // Returns the linear-congruential generator next state.
  static inline constexpr state_type lcg(state_type s) {
    return s * Params::multiplier() + Params::increment();
  }

  // Returns the linear-congruential arbitrary seek state.
  inline state_type advance(state_type s, uint64_t n) const {
    state_type mult = Params::multiplier();
    state_type inc = Params::increment();
    state_type m = 1;
    state_type i = 0;
    while (n > 0) {
      if (n & 1) {
        m *= mult;
        i = i * mult + inc;
      }
      inc = (mult + 1) * inc;
      mult *= mult;
      n >>= 1;
    }
    return m * s + i;
  }

  template <class SeedSequence>
  void reseed(SeedSequence& seq) {
    using sequence_result_type = typename SeedSequence::result_type;
    constexpr size_t kBufferSize =
        sizeof(state_type) / sizeof(sequence_result_type);
    sequence_result_type buffer[kBufferSize];
    seq.generate(std::begin(buffer), std::end(buffer));
    // Convert the seed output to a single state value.
    state_type tmp = buffer[0];
    for (size_t i = 1; i < kBufferSize; i++) {
      tmp <<= (sizeof(sequence_result_type) * 8);
      tmp |= buffer[i];
    }
    state_ = lcg(tmp + params_type::increment());
  }
};

// Parameterized implementation of the PCG 128-bit oneseq state.
// This provides state_type, multiplier, and increment for pcg_engine.
template <uint64_t kMultA, uint64_t kMultB, uint64_t kIncA, uint64_t kIncB>
class pcg128_params {
 public:
  using state_type = absl::uint128;
  static inline constexpr state_type multiplier() {
    return absl::MakeUint128(kMultA, kMultB);
  }
  static inline constexpr state_type increment() {
    return absl::MakeUint128(kIncA, kIncB);
  }
};

// Implementation of the PCG xsl_rr_128_64 128-bit mixing function, which
// accepts an input of state_type and mixes it into an output of result_type.
struct pcg_xsl_rr_128_64 {
  using state_type = absl::uint128;
  using result_type = uint64_t;

  inline uint64_t operator()(state_type state) {
    // This is equivalent to the xsl_rr_128_64 mixing function.
    uint64_t rotate = static_cast<uint64_t>(state >> 122u);
    state ^= state >> 64;
    uint64_t s = static_cast<uint64_t>(state);
    return rotr(s, static_cast<int>(rotate));
  }
};

// Parameterized implementation of the PCG 64-bit oneseq state.
// This provides state_type, multiplier, and increment for pcg_engine.
template <uint64_t kMult, uint64_t kInc>
class pcg64_params {
 public:
  using state_type = uint64_t;
  static inline constexpr state_type multiplier() { return kMult; }
  static inline constexpr state_type increment() { return kInc; }
};

// Implementation of the PCG xsh_rr_64_32 64-bit mixing function, which accepts
// an input of state_type and mixes it into an output of result_type.
struct pcg_xsh_rr_64_32 {
  using state_type = uint64_t;
  using result_type = uint32_t;
  inline uint32_t operator()(uint64_t state) {
    return rotr(static_cast<uint32_t>(((state >> 18) ^ state) >> 27),
                state >> 59);
  }
};

// Stable pcg_engine implementations:
// This is a 64-bit generator using 128-bits of state.
// The output sequence is equivalent to Melissa O'Neil's pcg64_oneseq.
using pcg64_2018_engine = pcg_engine<
    random_internal::pcg128_params<0x2360ed051fc65da4ull, 0x4385df649fccf645ull,
                                   0x5851f42d4c957f2d, 0x14057b7ef767814f>,
    random_internal::pcg_xsl_rr_128_64>;

// This is a 32-bit generator using 64-bits of state.
// This is equivalent to Melissa O'Neil's pcg32_oneseq.
using pcg32_2018_engine = pcg_engine<
    random_internal::pcg64_params<0x5851f42d4c957f2dull, 0x14057b7ef767814full>,
    random_internal::pcg_xsh_rr_64_32>;

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_PCG_ENGINE_H_
