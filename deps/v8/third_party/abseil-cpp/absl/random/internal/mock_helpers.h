//
// Copyright 2019 The Abseil Authors.
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

#ifndef ABSL_RANDOM_INTERNAL_MOCK_HELPERS_H_
#define ABSL_RANDOM_INTERNAL_MOCK_HELPERS_H_

#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/internal/fast_type_id.h"
#include "absl/types/optional.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// MockHelpers works in conjunction with MockOverloadSet, MockingBitGen, and
// BitGenRef to enable the mocking capability for absl distribution functions.
//
// MockingBitGen registers mocks based on the typeid of a mock signature, KeyT,
// which is used to generate a unique id.
//
// KeyT is a signature of the form:
//   result_type(discriminator_type, std::tuple<args...>)
// The mocked function signature will be composed from KeyT as:
//   result_type(args...)
//
class MockHelpers {
  using IdType = ::absl::base_internal::FastTypeIdType;

  // Given a key signature type used to index the mock, extract the components.
  // KeyT is expected to have the form:
  //   result_type(discriminator_type, arg_tuple_type)
  template <typename KeyT>
  struct KeySignature;

  template <typename ResultT, typename DiscriminatorT, typename ArgTupleT>
  struct KeySignature<ResultT(DiscriminatorT, ArgTupleT)> {
    using result_type = ResultT;
    using discriminator_type = DiscriminatorT;
    using arg_tuple_type = ArgTupleT;
  };

  // Detector for InvokeMock.
  template <class T>
  using invoke_mock_t = decltype(std::declval<T*>()->InvokeMock(
      std::declval<IdType>(), std::declval<void*>(), std::declval<void*>()));

  // Empty implementation of InvokeMock.
  template <typename KeyT, typename ReturnT, typename ArgTupleT, typename URBG,
            typename... Args>
  static absl::optional<ReturnT> InvokeMockImpl(char, URBG*, Args&&...) {
    return absl::nullopt;
  }

  // Non-empty implementation of InvokeMock.
  template <typename KeyT, typename ReturnT, typename ArgTupleT, typename URBG,
            typename = invoke_mock_t<URBG>, typename... Args>
  static absl::optional<ReturnT> InvokeMockImpl(int, URBG* urbg,
                                                Args&&... args) {
    ArgTupleT arg_tuple(std::forward<Args>(args)...);
    ReturnT result;
    if (urbg->InvokeMock(::absl::base_internal::FastTypeId<KeyT>(), &arg_tuple,
                         &result)) {
      return result;
    }
    return absl::nullopt;
  }

 public:
  // InvokeMock is private; this provides access for some specialized use cases.
  template <typename URBG>
  static inline bool PrivateInvokeMock(URBG* urbg, IdType type,
                                       void* args_tuple, void* result) {
    return urbg->InvokeMock(type, args_tuple, result);
  }

  // Invoke a mock for the KeyT (may or may not be a signature).
  //
  // KeyT is used to generate a typeid-based lookup key for the mock.
  // KeyT is a signature of the form:
  //   result_type(discriminator_type, std::tuple<args...>)
  // The mocked function signature will be composed from KeyT as:
  //   result_type(args...)
  //
  // An instance of arg_tuple_type must be constructable from Args..., since
  // the underlying mechanism requires a pointer to an argument tuple.
  template <typename KeyT, typename URBG, typename... Args>
  static auto MaybeInvokeMock(URBG* urbg, Args&&... args)
      -> absl::optional<typename KeySignature<KeyT>::result_type> {
    // Use function overloading to dispatch to the implementation since
    // more modern patterns (e.g. require + constexpr) are not supported in all
    // compiler configurations.
    return InvokeMockImpl<KeyT, typename KeySignature<KeyT>::result_type,
                          typename KeySignature<KeyT>::arg_tuple_type, URBG>(
        0, urbg, std::forward<Args>(args)...);
  }

  // Acquire a mock for the KeyT (may or may not be a signature).
  //
  // KeyT is used to generate a typeid-based lookup for the mock.
  // KeyT is a signature of the form:
  //   result_type(discriminator_type, std::tuple<args...>)
  // The mocked function signature will be composed from KeyT as:
  //   result_type(args...)
  template <typename KeyT, typename MockURBG>
  static auto MockFor(MockURBG& m)
      -> decltype(m.template RegisterMock<
                  typename KeySignature<KeyT>::result_type,
                  typename KeySignature<KeyT>::arg_tuple_type>(
          m, std::declval<IdType>())) {
    return m.template RegisterMock<typename KeySignature<KeyT>::result_type,
                                   typename KeySignature<KeyT>::arg_tuple_type>(
        m, ::absl::base_internal::FastTypeId<KeyT>());
  }
};

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_MOCK_HELPERS_H_
