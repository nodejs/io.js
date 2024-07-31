// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_REDUCER_TRAITS_H_
#define V8_COMPILER_TURBOSHAFT_REDUCER_TRAITS_H_

#include <type_traits>

namespace v8::internal::compiler::turboshaft {

template <class Assembler, bool has_gvn, template <class> class... Reducers>
class ReducerStack;

template <typename Next>
class GenericReducerBase;
template <typename Next>
class EmitProjectionReducer;
template <typename Next>
class TSReducerBase;

// is_same_reducer compares two reducers.
template <template <typename> typename T, template <typename> typename U>
struct is_same_reducer : public std::bool_constant<false> {};

template <template <typename> typename Reducer>
struct is_same_reducer<Reducer, Reducer> : public std::bool_constant<true> {};

template <template <typename> typename...>
struct reducer_list {};
// Converts a ReducerStack {Next} to a reducer_list<>;
template <typename Next>
struct reducer_stack_to_list;
template <typename A, bool has_gvn, template <typename> typename... Reducers>
struct reducer_stack_to_list<ReducerStack<A, has_gvn, Reducers...>> {
  using type = reducer_list<Reducers...>;
};

// Checks if a reducer_list<> {RL} contains reducer {R}.
template <typename RL, template <typename> typename R>
struct reducer_list_contains;
template <template <typename> typename R, template <typename> typename Head,
          template <typename> typename... Tail>
struct reducer_list_contains<reducer_list<Head, Tail...>, R> {
  static constexpr bool value =
      is_same_reducer<Head, R>::value ||
      reducer_list_contains<reducer_list<Tail...>, R>::value;
};
template <template <typename> typename R>
struct reducer_list_contains<reducer_list<>, R>
    : public std::bool_constant<false> {};

// Checks if a reducer_list<> {RL} starts with reducer {R}.
template <typename RL, template <typename> typename R>
struct reducer_list_starts_with;
template <template <typename> typename R, template <typename> typename Head,
          template <typename> typename... Tail>
struct reducer_list_starts_with<reducer_list<Head, Tail...>, R>
    : public std::bool_constant<is_same_reducer<Head, R>::value> {};
template <template <typename> typename R>
struct reducer_list_starts_with<reducer_list<>, R>
    : public std::bool_constant<false> {};

// Check if the {Next} ReducerStack contains {Reducer}.
template <typename Next, template <typename> typename Reducer>
struct next_contains_reducer {
  using list = typename reducer_stack_to_list<Next>::type;
  static constexpr bool value = reducer_list_contains<list, Reducer>::value;
};

// Check if in the {Next} ReducerStack, any of {Reducer} comes next.
template <typename Next, template <typename> typename... Reducer>
struct next_reducer_is {
  using list = typename reducer_stack_to_list<Next>::type;
  static constexpr bool value =
      (reducer_list_starts_with<list, Reducer>::value || ...);
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
