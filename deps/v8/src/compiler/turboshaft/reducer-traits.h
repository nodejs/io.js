// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_REDUCER_TRAITS_H_
#define V8_COMPILER_TURBOSHAFT_REDUCER_TRAITS_H_

#include <limits>
#include <type_traits>

#include "src/base/template-meta-programming/common.h"
#include "src/base/template-meta-programming/list.h"

namespace v8::internal::compiler::turboshaft {

template <typename Next>
class GenericReducerBase;
template <typename Next>
class EmitProjectionReducer;
template <typename Next>
class TSReducerBase;

template <template <typename> typename... Ts>
using reducer_list = base::tmp::list1<Ts...>;

// Get the length of a reducer_list<> {RL}.
template <typename RL>
struct reducer_list_length : base::tmp::length1<RL> {};

// Checks if a reducer_list<> {RL} contains reducer {R}.
template <typename RL, template <typename> typename R>
struct reducer_list_contains : base::tmp::contains1<RL, R> {};

// Checks if a reducer_list<> {RL} starts with reducer {R}.
template <typename RL, template <typename> typename R>
struct reducer_list_starts_with {
  static constexpr bool value = base::tmp::index_of1<RL, R>::value == 0;
};

// Get the index of {R} in the reducer_list<> {RL} or {Otherwise} if it is not
// in the list.
template <typename RL, template <typename> typename R,
          size_t Otherwise = std::numeric_limits<size_t>::max()>
struct reducer_list_index_of : public base::tmp::index_of1<RL, R, Otherwise> {};

// Inserts reducer {R} into reducer_list<> {RL} at index {I}. If I >= length of
// {RL}, then {R} is appended.
template <typename RL, size_t I, template <typename> typename R>
struct reducer_list_insert_at : base::tmp::insert_at1<RL, I, R> {};

// Turns a reducer_list<> into the instantiated class for the stack.
template <typename RL, typename Bottom>
struct reducer_list_to_stack
    : base::tmp::fold_right1<base::tmp::instantiate, RL, Bottom> {};

// Check if in the {Next} ReducerStack, any of {Reducer} comes next.
template <typename Next, template <typename> typename... Reducer>
struct next_reducer_is {
  static constexpr bool value =
      (base::tmp::is_instantiation_of<Next, Reducer>::value || ...);
};

// Check if the {Next} ReducerStack contains {Reducer}.
template <typename Next, template <typename> typename Reducer>
struct next_contains_reducer : public std::bool_constant<false> {};

template <template <typename> typename R, typename T,
          template <typename> typename Reducer>
struct next_contains_reducer<R<T>, Reducer> {
  static constexpr bool value = base::tmp::equals1<R, Reducer>::value ||
                                next_contains_reducer<T, Reducer>::value;
};

// TODO(dmercadier): EmitProjectionReducer is not always the bottom of the stack
// because it could be succeeded by a ValueNumberingReducer. We should take this
// into account in next_is_bottom_of_assembler_stack.
template <typename Next>
struct next_is_bottom_of_assembler_stack
    : public next_reducer_is<Next, GenericReducerBase, EmitProjectionReducer,
                             TSReducerBase> {};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_REDUCER_TRAITS_H_
