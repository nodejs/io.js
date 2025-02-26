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

#ifndef ABSL_RANDOM_INTERNAL_IOSTREAM_STATE_SAVER_H_
#define ABSL_RANDOM_INTERNAL_IOSTREAM_STATE_SAVER_H_

#include <cmath>
#include <cstdint>
#include <ios>
#include <istream>
#include <limits>
#include <ostream>
#include <type_traits>

#include "absl/base/config.h"
#include "absl/meta/type_traits.h"
#include "absl/numeric/int128.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// The null_state_saver does nothing.
template <typename T>
class null_state_saver {
 public:
  using stream_type = T;
  using flags_type = std::ios_base::fmtflags;

  null_state_saver(T&, flags_type) {}
  ~null_state_saver() {}
};

// ostream_state_saver is a RAII object to save and restore the common
// basic_ostream flags used when implementing `operator <<()` on any of
// the absl random distributions.
template <typename OStream>
class ostream_state_saver {
 public:
  using ostream_type = OStream;
  using flags_type = std::ios_base::fmtflags;
  using fill_type = typename ostream_type::char_type;
  using precision_type = std::streamsize;

  ostream_state_saver(ostream_type& os,  // NOLINT(runtime/references)
                      flags_type flags, fill_type fill)
      : os_(os),
        flags_(os.flags(flags)),
        fill_(os.fill(fill)),
        precision_(os.precision()) {
    // Save state in initialized variables.
  }

  ~ostream_state_saver() {
    // Restore saved state.
    os_.precision(precision_);
    os_.fill(fill_);
    os_.flags(flags_);
  }

 private:
  ostream_type& os_;
  const flags_type flags_;
  const fill_type fill_;
  const precision_type precision_;
};

#if defined(__NDK_MAJOR__) && __NDK_MAJOR__ < 16
#define ABSL_RANDOM_INTERNAL_IOSTREAM_HEXFLOAT 1
#else
#define ABSL_RANDOM_INTERNAL_IOSTREAM_HEXFLOAT 0
#endif

template <typename CharT, typename Traits>
ostream_state_saver<std::basic_ostream<CharT, Traits>> make_ostream_state_saver(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    std::ios_base::fmtflags flags = std::ios_base::dec | std::ios_base::left |
#if ABSL_RANDOM_INTERNAL_IOSTREAM_HEXFLOAT
                                    std::ios_base::fixed |
#endif
                                    std::ios_base::scientific) {
  using result_type = ostream_state_saver<std::basic_ostream<CharT, Traits>>;
  return result_type(os, flags, os.widen(' '));
}

template <typename T>
typename absl::enable_if_t<!std::is_base_of<std::ios_base, T>::value,
                           null_state_saver<T>>
make_ostream_state_saver(T& is,  // NOLINT(runtime/references)
                         std::ios_base::fmtflags flags = std::ios_base::dec) {
  using result_type = null_state_saver<T>;
  return result_type(is, flags);
}

// stream_precision_helper<type>::kPrecision returns the base 10 precision
// required to stream and reconstruct a real type exact binary value through
// a binary->decimal->binary transition.
template <typename T>
struct stream_precision_helper {
  // max_digits10 may be 0 on MSVC; if so, use digits10 + 3.
  static constexpr int kPrecision =
      (std::numeric_limits<T>::max_digits10 > std::numeric_limits<T>::digits10)
          ? std::numeric_limits<T>::max_digits10
          : (std::numeric_limits<T>::digits10 + 3);
};

template <>
struct stream_precision_helper<float> {
  static constexpr int kPrecision = 9;
};
template <>
struct stream_precision_helper<double> {
  static constexpr int kPrecision = 17;
};
template <>
struct stream_precision_helper<long double> {
  static constexpr int kPrecision = 36;  // assuming fp128
};

// istream_state_saver is a RAII object to save and restore the common
// std::basic_istream<> flags used when implementing `operator >>()` on any of
// the absl random distributions.
template <typename IStream>
class istream_state_saver {
 public:
  using istream_type = IStream;
  using flags_type = std::ios_base::fmtflags;

  istream_state_saver(istream_type& is,  // NOLINT(runtime/references)
                      flags_type flags)
      : is_(is), flags_(is.flags(flags)) {}

  ~istream_state_saver() { is_.flags(flags_); }

 private:
  istream_type& is_;
  flags_type flags_;
};

template <typename CharT, typename Traits>
istream_state_saver<std::basic_istream<CharT, Traits>> make_istream_state_saver(
    std::basic_istream<CharT, Traits>& is,  // NOLINT(runtime/references)
    std::ios_base::fmtflags flags = std::ios_base::dec |
                                    std::ios_base::scientific |
                                    std::ios_base::skipws) {
  using result_type = istream_state_saver<std::basic_istream<CharT, Traits>>;
  return result_type(is, flags);
}

template <typename T>
typename absl::enable_if_t<!std::is_base_of<std::ios_base, T>::value,
                           null_state_saver<T>>
make_istream_state_saver(T& is,  // NOLINT(runtime/references)
                         std::ios_base::fmtflags flags = std::ios_base::dec) {
  using result_type = null_state_saver<T>;
  return result_type(is, flags);
}

// stream_format_type<T> is a helper struct to convert types which
// basic_iostream cannot output as decimal numbers into types which
// basic_iostream can output as decimal numbers. Specifically:
// * signed/unsigned char-width types are converted to int.
// * TODO(lar): __int128 => uint128, except there is no operator << yet.
//
template <typename T>
struct stream_format_type
    : public std::conditional<(sizeof(T) == sizeof(char)), int, T> {};

// stream_u128_helper allows us to write out either absl::uint128 or
// __uint128_t types in the same way, which enables their use as internal
// state of PRNG engines.
template <typename T>
struct stream_u128_helper;

template <>
struct stream_u128_helper<absl::uint128> {
  template <typename IStream>
  inline absl::uint128 read(IStream& in) {
    uint64_t h = 0;
    uint64_t l = 0;
    in >> h >> l;
    return absl::MakeUint128(h, l);
  }

  template <typename OStream>
  inline void write(absl::uint128 val, OStream& out) {
    uint64_t h = absl::Uint128High64(val);
    uint64_t l = absl::Uint128Low64(val);
    out << h << out.fill() << l;
  }
};

#ifdef ABSL_HAVE_INTRINSIC_INT128
template <>
struct stream_u128_helper<__uint128_t> {
  template <typename IStream>
  inline __uint128_t read(IStream& in) {
    uint64_t h = 0;
    uint64_t l = 0;
    in >> h >> l;
    return (static_cast<__uint128_t>(h) << 64) | l;
  }

  template <typename OStream>
  inline void write(__uint128_t val, OStream& out) {
    uint64_t h = static_cast<uint64_t>(val >> 64u);
    uint64_t l = static_cast<uint64_t>(val);
    out << h << out.fill() << l;
  }
};
#endif

template <typename FloatType, typename IStream>
inline FloatType read_floating_point(IStream& is) {
  static_assert(std::is_floating_point<FloatType>::value, "");
  FloatType dest;
  is >> dest;
  // Parsing a double value may report a subnormal value as an error
  // despite being able to represent it.
  // See https://stackoverflow.com/q/52410931/3286653
  // It may also report an underflow when parsing DOUBLE_MIN as an
  // ERANGE error, as the parsed value may be smaller than DOUBLE_MIN
  // and rounded up.
  // See: https://stackoverflow.com/q/42005462
  if (is.fail() &&
      (std::fabs(dest) == (std::numeric_limits<FloatType>::min)() ||
       std::fpclassify(dest) == FP_SUBNORMAL)) {
    is.clear(is.rdstate() & (~std::ios_base::failbit));
  }
  return dest;
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_IOSTREAM_STATE_SAVER_H_
