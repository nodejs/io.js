//
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
//
// -----------------------------------------------------------------------------
// File: bit_gen_ref.h
// -----------------------------------------------------------------------------
//
// This header defines a bit generator "reference" class, for use in interfaces
// that take both Abseil (e.g. `absl::BitGen`) and standard library (e.g.
// `std::mt19937`) bit generators.

#ifndef ABSL_RANDOM_BIT_GEN_REF_H_
#define ABSL_RANDOM_BIT_GEN_REF_H_

#include <limits>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/internal/fast_type_id.h"
#include "absl/base/macros.h"
#include "absl/meta/type_traits.h"
#include "absl/random/internal/distribution_caller.h"
#include "absl/random/internal/fast_uniform_bits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

template <typename URBG, typename = void, typename = void, typename = void>
struct is_urbg : std::false_type {};

template <typename URBG>
struct is_urbg<
    URBG,
    absl::enable_if_t<std::is_same<
        typename URBG::result_type,
        typename std::decay<decltype((URBG::min)())>::type>::value>,
    absl::enable_if_t<std::is_same<
        typename URBG::result_type,
        typename std::decay<decltype((URBG::max)())>::type>::value>,
    absl::enable_if_t<std::is_same<
        typename URBG::result_type,
        typename std::decay<decltype(std::declval<URBG>()())>::type>::value>>
    : std::true_type {};

template <typename>
struct DistributionCaller;
class MockHelpers;

}  // namespace random_internal

// -----------------------------------------------------------------------------
// absl::BitGenRef
// -----------------------------------------------------------------------------
//
// `absl::BitGenRef` is a type-erasing class that provides a generator-agnostic
// non-owning "reference" interface for use in place of any specific uniform
// random bit generator (URBG). This class may be used for both Abseil
// (e.g. `absl::BitGen`, `absl::InsecureBitGen`) and Standard library (e.g
// `std::mt19937`, `std::minstd_rand`) bit generators.
//
// Like other reference classes, `absl::BitGenRef` does not own the
// underlying bit generator, and the underlying instance must outlive the
// `absl::BitGenRef`.
//
// `absl::BitGenRef` is particularly useful when used with an
// `absl::MockingBitGen` to test specific paths in functions which use random
// values.
//
// Example:
//    void TakesBitGenRef(absl::BitGenRef gen) {
//      int x = absl::Uniform<int>(gen, 0, 1000);
//    }
//
class BitGenRef {
  // SFINAE to detect whether the URBG type includes a member matching
  // bool InvokeMock(base_internal::FastTypeIdType, void*, void*).
  //
  // These live inside BitGenRef so that they have friend access
  // to MockingBitGen. (see similar methods in DistributionCaller).
  template <template <class...> class Trait, class AlwaysVoid, class... Args>
  struct detector : std::false_type {};
  template <template <class...> class Trait, class... Args>
  struct detector<Trait, absl::void_t<Trait<Args...>>, Args...>
      : std::true_type {};

  template <class T>
  using invoke_mock_t = decltype(std::declval<T*>()->InvokeMock(
      std::declval<base_internal::FastTypeIdType>(), std::declval<void*>(),
      std::declval<void*>()));

  template <typename T>
  using HasInvokeMock = typename detector<invoke_mock_t, void, T>::type;

 public:
  BitGenRef(const BitGenRef&) = default;
  BitGenRef(BitGenRef&&) = default;
  BitGenRef& operator=(const BitGenRef&) = default;
  BitGenRef& operator=(BitGenRef&&) = default;

  template <typename URBG, typename absl::enable_if_t<
                               (!std::is_same<URBG, BitGenRef>::value &&
                                random_internal::is_urbg<URBG>::value &&
                                !HasInvokeMock<URBG>::value)>* = nullptr>
  BitGenRef(URBG& gen ABSL_ATTRIBUTE_LIFETIME_BOUND)  // NOLINT
      : t_erased_gen_ptr_(reinterpret_cast<uintptr_t>(&gen)),
        mock_call_(NotAMock),
        generate_impl_fn_(ImplFn<URBG>) {}

  template <typename URBG,
            typename absl::enable_if_t<(!std::is_same<URBG, BitGenRef>::value &&
                                        random_internal::is_urbg<URBG>::value &&
                                        HasInvokeMock<URBG>::value)>* = nullptr>
  BitGenRef(URBG& gen ABSL_ATTRIBUTE_LIFETIME_BOUND)  // NOLINT
      : t_erased_gen_ptr_(reinterpret_cast<uintptr_t>(&gen)),
        mock_call_(&MockCall<URBG>),
        generate_impl_fn_(ImplFn<URBG>) {}

  using result_type = uint64_t;

  static constexpr result_type(min)() {
    return (std::numeric_limits<result_type>::min)();
  }

  static constexpr result_type(max)() {
    return (std::numeric_limits<result_type>::max)();
  }

  result_type operator()() { return generate_impl_fn_(t_erased_gen_ptr_); }

 private:
  using impl_fn = result_type (*)(uintptr_t);
  using mock_call_fn = bool (*)(uintptr_t, base_internal::FastTypeIdType, void*,
                                void*);

  template <typename URBG>
  static result_type ImplFn(uintptr_t ptr) {
    // Ensure that the return values from operator() fill the entire
    // range promised by result_type, min() and max().
    absl::random_internal::FastUniformBits<result_type> fast_uniform_bits;
    return fast_uniform_bits(*reinterpret_cast<URBG*>(ptr));
  }

  // Get a type-erased InvokeMock pointer.
  template <typename URBG>
  static bool MockCall(uintptr_t gen_ptr, base_internal::FastTypeIdType type,
                       void* result, void* arg_tuple) {
    return reinterpret_cast<URBG*>(gen_ptr)->InvokeMock(type, result,
                                                        arg_tuple);
  }
  static bool NotAMock(uintptr_t, base_internal::FastTypeIdType, void*, void*) {
    return false;
  }

  inline bool InvokeMock(base_internal::FastTypeIdType type, void* args_tuple,
                         void* result) {
    if (mock_call_ == NotAMock) return false;  // avoids an indirect call.
    return mock_call_(t_erased_gen_ptr_, type, args_tuple, result);
  }

  uintptr_t t_erased_gen_ptr_;
  mock_call_fn mock_call_;
  impl_fn generate_impl_fn_;

  template <typename>
  friend struct ::absl::random_internal::DistributionCaller;  // for InvokeMock
  friend class ::absl::random_internal::MockHelpers;          // for InvokeMock
};

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_BIT_GEN_REF_H_
