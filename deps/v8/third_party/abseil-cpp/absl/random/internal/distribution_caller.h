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

#ifndef ABSL_RANDOM_INTERNAL_DISTRIBUTION_CALLER_H_
#define ABSL_RANDOM_INTERNAL_DISTRIBUTION_CALLER_H_

#include <utility>
#include <type_traits>

#include "absl/base/config.h"
#include "absl/base/internal/fast_type_id.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// DistributionCaller provides an opportunity to overload the general
// mechanism for calling a distribution, allowing for mock-RNG classes
// to intercept such calls.
template <typename URBG>
struct DistributionCaller {
  static_assert(!std::is_pointer<URBG>::value,
                "You must pass a reference, not a pointer.");
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
      std::declval<::absl::base_internal::FastTypeIdType>(),
      std::declval<void*>(), std::declval<void*>()));

  using HasInvokeMock = typename detector<invoke_mock_t, void, URBG>::type;

  // Default implementation of distribution caller.
  template <typename DistrT, typename... Args>
  static typename DistrT::result_type Impl(std::false_type, URBG* urbg,
                                           Args&&... args) {
    DistrT dist(std::forward<Args>(args)...);
    return dist(*urbg);
  }

  // Mock implementation of distribution caller.
  // The underlying KeyT must match the KeyT constructed by MockOverloadSet.
  template <typename DistrT, typename... Args>
  static typename DistrT::result_type Impl(std::true_type, URBG* urbg,
                                           Args&&... args) {
    using ResultT = typename DistrT::result_type;
    using ArgTupleT = std::tuple<absl::decay_t<Args>...>;
    using KeyT = ResultT(DistrT, ArgTupleT);

    ArgTupleT arg_tuple(std::forward<Args>(args)...);
    ResultT result;
    if (!urbg->InvokeMock(::absl::base_internal::FastTypeId<KeyT>(), &arg_tuple,
                          &result)) {
      auto dist = absl::make_from_tuple<DistrT>(arg_tuple);
      result = dist(*urbg);
    }
    return result;
  }

  // Default implementation of distribution caller.
  template <typename DistrT, typename... Args>
  static typename DistrT::result_type Call(URBG* urbg, Args&&... args) {
    return Impl<DistrT, Args...>(HasInvokeMock{}, urbg,
                                 std::forward<Args>(args)...);
  }
};

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_DISTRIBUTION_CALLER_H_
