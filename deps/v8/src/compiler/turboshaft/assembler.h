// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_ASSEMBLER_H_
#define V8_COMPILER_TURBOSHAFT_ASSEMBLER_H_

#include <concepts>
#include <cstring>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "include/v8-source-location.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/small-vector.h"
#include "src/base/string-format.h"
#include "src/base/template-utils.h"
#include "src/base/vector.h"
#include "src/codegen/callable.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/heap-object-list.h"
#include "src/codegen/reloc-info.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/code-assembler.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/globals.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/turboshaft/access-builder.h"
#include "src/compiler/turboshaft/builtin-call-descriptors.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operation-matcher.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/reducer-traits.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/runtime-call-descriptors.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/compiler/turboshaft/snapshot-table.h"
#include "src/compiler/turboshaft/uniform-reducer-adapter.h"
#include "src/compiler/turboshaft/utils.h"
#include "src/compiler/write-barrier-kind.h"
#include "src/flags/flags.h"
#include "src/logging/runtime-call-stats.h"
#include "src/objects/dictionary.h"
#include "src/objects/elements-kind.h"
#include "src/objects/fixed-array.h"
#include "src/objects/heap-number.h"
#include "src/objects/oddball.h"
#include "src/objects/property-cell.h"
#include "src/objects/scope-info.h"
#include "src/objects/swiss-name-dictionary.h"
#include "src/objects/tagged.h"
#include "src/objects/turbofan-types.h"

#ifdef V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-objects.h"
#endif

namespace v8::internal {
enum class Builtin : int32_t;
}

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <class AssemblerT>
class CatchScopeImpl;

// GotoIf(cond, dst) and GotoIfNot(cond, dst) are not guaranteed to actually
// generate a Branch with `dst` as one of the destination, because some reducer
// in the stack could realize that `cond` is statically known and optimize away
// the Branch. Thus, GotoIf and GotoIfNot return a {ConditionalGotoStatus},
// which represents whether a GotoIf/GotoIfNot was emitted as a Branch or a Goto
// (and if a Goto, then to what: `dst` or the fallthrough block).
enum ConditionalGotoStatus {
  kGotoDestination = 1,  // The conditional Goto became an unconditional Goto to
                         // the destination.
  kGotoEliminated = 2,   // The conditional GotoIf/GotoIfNot would never be
                         // executed and only the fallthrough path remains.
  kBranch = 3            // The conditional Goto became a branch.

  // Some examples of this:
  //   GotoIf(true, dst)     ===> kGotoDestination
  //   GotoIf(false, dst)    ===> kGotoEliminated
  //   GotoIf(var, dst)      ===> kBranch
  //   GotoIfNot(true, dst)  ===> kGotoEliminated
  //   GotoIfNot(false, dst) ===> kGotoDestination
  //   GotoIfNot(var, dst)   ===> kBranch
};
static_assert((ConditionalGotoStatus::kGotoDestination |
               ConditionalGotoStatus::kGotoEliminated) ==
              ConditionalGotoStatus::kBranch);

#ifdef HAS_CPP_CONCEPTS

template <typename It, typename A>
concept ForeachIterable = requires(It iterator, A& assembler) {
  { iterator.Begin(assembler) } -> std::same_as<typename It::iterator_type>;
  {
    iterator.IsEnd(assembler, typename It::iterator_type{})
  } -> std::same_as<OptionalV<Word32>>;
  {
    iterator.Advance(assembler, typename It::iterator_type{})
  } -> std::same_as<typename It::iterator_type>;
  {
    iterator.Dereference(assembler, typename It::iterator_type{})
  } -> std::same_as<typename It::value_type>;
};

#endif

// `Range<T>` implements the `ForeachIterable` concept to iterate over a range
// of values inside a `FOREACH` loop. The range can be specified with a begin,
// an (exclusive) end and an optional stride.
//
// Example:
//
//   FOREACH(offset, Range(start, end, 4)) {
//     // Load the value at `offset`.
//     auto value = __ Load(offset, LoadOp::Kind::RawAligned(), ...);
//     // ...
//   }
//
template <typename T>
class Range {
 public:
  using value_type = V<T>;
  using iterator_type = value_type;

  Range(ConstOrV<T> begin, ConstOrV<T> end, ConstOrV<T> stride = 1)
      : begin_(begin), end_(end), stride_(stride) {}

  template <typename A>
  iterator_type Begin(A& assembler) const {
    return assembler.resolve(begin_);
  }

  template <typename A>
  OptionalV<Word32> IsEnd(A& assembler, iterator_type current_iterator) const {
    if constexpr (std::is_same_v<T, Word32>) {
      return assembler.Uint32LessThanOrEqual(assembler.resolve(end_),
                                             current_iterator);
    } else {
      static_assert(std::is_same_v<T, Word64>);
      return assembler.Uint64LessThanOrEqual(assembler.resolve(end_),
                                             current_iterator);
    }
  }

  template <typename A>
  iterator_type Advance(A& assembler, iterator_type current_iterator) const {
    if constexpr (std::is_same_v<T, Word32>) {
      return assembler.Word32Add(current_iterator, assembler.resolve(stride_));
    } else {
      static_assert(std::is_same_v<T, Word64>);
      return assembler.Word64Add(current_iterator, assembler.resolve(stride_));
    }
  }

  template <typename A>
  value_type Dereference(A& assembler, iterator_type current_iterator) const {
    return current_iterator;
  }

 private:
  ConstOrV<T> begin_;
  ConstOrV<T> end_;
  ConstOrV<T> stride_;
};

// Deduction guides for `Range`.
template <typename T>
Range(V<T>, V<T>, V<T>) -> Range<T>;
template <typename T>
Range(V<T>, V<T>, typename ConstOrV<T>::constant_type) -> Range<T>;
template <typename T>
Range(V<T>, typename ConstOrV<T>::constant_type,
      typename ConstOrV<T>::constant_type) -> Range<T>;

// `IndexRange<T>` is a short hand for a Range<T> that iterates the range [0,
// count) with steps of 1. This is the ideal iterator to generate a `for(int i =
// 0; i < count; ++i) {}`-style loop.
//
//  Example:
//
//    FOREACH(i, IndexRange(count)) { ... }
//
template <typename T>
class IndexRange : public Range<T> {
 public:
  using base = Range<T>;
  using value_type = typename base::value_type;
  using iterator_type = typename base::iterator_type;

  explicit IndexRange(ConstOrV<T> count) : Range<T>(0, count, 1) {}
};

// `Sequence<T>` implements the `ForeachIterable` concept to iterate an
// unlimited sequence of inside a `FOREACH` loop. The iteration begins at the
// given start value and during each iteration the value is incremented by the
// optional `stride` argument. Note that there is no termination condition, so
// the end of the loop needs to be terminated in another way. This could be
// either by a conditional break inside the loop or by combining the `Sequence`
// iterator with another iterator that provides the termination condition (see
// Zip below).
//
// Example:
//
//   FOREACH(index, Sequence<WordPtr>(0)) {
//     // ...
//     V<Object> value = __ Load(object, index, LoadOp::Kind::TaggedBase(),
//                               offset, field_size);
//     GOTO_IF(__ IsSmi(value), done, index);
//   }
//
template <typename T>
class Sequence : private Range<T> {
  using base = Range<T>;

 public:
  using value_type = typename base::value_type;
  using iterator_type = typename base::iterator_type;

  explicit Sequence(ConstOrV<T> begin, ConstOrV<T> stride = 1)
      : base(begin, 0, stride) {}

  using base::Advance;
  using base::Begin;
  using base::Dereference;

  template <typename A>
  OptionalV<Word32> IsEnd(A&, iterator_type) const {
    // Sequence doesn't have a termination condition.
    return OptionalV<Word32>::Nullopt();
  }
};

// Deduction guide for `Sequence`.
template <typename T>
Sequence(V<T>, V<T>) -> Sequence<T>;
template <typename T>
Sequence(V<T>, typename ConstOrV<T>::constant_type) -> Sequence<T>;
template <typename T>
Sequence(V<T>) -> Sequence<T>;

// `Zip<T>` implements the `ForeachIterable` concept to iterate multiple
// iterators at the same time inside a `FOREACH` loop. The loop terminates once
// any of the zipped iterators signals end of iteration. The number of iteration
// variables specified in the `FOREACH` loop has to match the number of zipped
// iterators.
//
// Example:
//
//   FOREACH(offset, index, Zip(Range(start, end, 4),
//                              Sequence<Word32>(0)) {
//     // `offset` iterates [start, end) with steps of 4.
//     // `index` counts 0, 1, 2, ...
//   }
//
// NOTE: The generated loop is only controlled by the `offset < end` condition
// as `Sequence` has no upper bound. Hence, the above loop resembles a loop like
// (assuming start, end and therefore offset are WordPtr):
//
//   for(auto [offset, index] = {start, 0};
//       offset < end;
//       offset += 4, ++index) {
//     // ...
//   }
//
template <typename... Iterables>
class Zip {
 public:
  using value_type = std::tuple<typename Iterables::value_type...>;
  using iterator_type = std::tuple<typename Iterables::iterator_type...>;

  explicit Zip(Iterables... iterables) : iterables_(std::move(iterables)...) {}

  template <typename A>
  iterator_type Begin(A& assembler) {
    return base::tuple_map(
        iterables_, [&assembler](auto& it) { return it.Begin(assembler); });
  }

  template <typename A>
  OptionalV<Word32> IsEnd(A& assembler, iterator_type current_iterator) {
    // TODO(nicohartmann): Currently we don't short-circuit the disjunction here
    // because that's slightly more difficult to do with the current `IsEnd`
    // predicate. We can consider making this more powerful if we see use cases.
    auto results = base::tuple_map2(iterables_, current_iterator,
                                    [&assembler](auto& it, auto current) {
                                      return it.IsEnd(assembler, current);
                                    });
    return base::tuple_fold(
        OptionalV<Word32>::Nullopt(), results,
        [&assembler](OptionalV<Word32> acc, OptionalV<Word32> next) {
          if (!next.has_value()) return acc;
          if (!acc.has_value()) return next;
          return OptionalV(
              assembler.Word32BitwiseOr(acc.value(), next.value()));
        });
  }

  template <typename A>
  iterator_type Advance(A& assembler, iterator_type current_iterator) {
    return base::tuple_map2(iterables_, current_iterator,
                            [&assembler](auto& it, auto current) {
                              return it.Advance(assembler, current);
                            });
  }

  template <typename A>
  value_type Dereference(A& assembler, iterator_type current_iterator) {
    return base::tuple_map2(iterables_, current_iterator,
                            [&assembler](auto& it, auto current) {
                              return it.Dereference(assembler, current);
                            });
  }

 private:
  std::tuple<Iterables...> iterables_;
};

// Deduction guide for `Zip`.
template <typename... Iterables>
Zip(Iterables... iterables) -> Zip<Iterables...>;

class ConditionWithHint final {
 public:
  ConditionWithHint(
      V<Word32> condition,
      BranchHint hint = BranchHint::kNone)  // NOLINT(runtime/explicit)
      : condition_(condition), hint_(hint) {}

  template <typename T, typename = std::enable_if_t<std::is_same_v<T, OpIndex>>>
  ConditionWithHint(
      T condition,
      BranchHint hint = BranchHint::kNone)  // NOLINT(runtime/explicit)
      : ConditionWithHint(V<Word32>{condition}, hint) {}

  V<Word32> condition() const { return condition_; }
  BranchHint hint() const { return hint_; }

 private:
  V<Word32> condition_;
  BranchHint hint_;
};

namespace detail {
template <typename A, typename ConstOrValues>
auto ResolveAll(A& assembler, const ConstOrValues& const_or_values) {
  return std::apply(
      [&](auto&... args) { return std::tuple{assembler.resolve(args)...}; },
      const_or_values);
}

template <typename T>
struct IndexTypeFor {
  using type = OpIndex;
};
template <typename T>
struct IndexTypeFor<std::tuple<T>> {
  using type = T;
};

template <typename T>
using index_type_for_t = typename IndexTypeFor<T>::type;

inline bool SuppressUnusedWarning(bool b) { return b; }
template <typename T>
auto unwrap_unary_tuple(std::tuple<T>&& tpl) {
  return std::get<0>(std::forward<std::tuple<T>>(tpl));
}
template <typename T1, typename T2, typename... Rest>
auto unwrap_unary_tuple(std::tuple<T1, T2, Rest...>&& tpl) {
  return tpl;
}
}  // namespace detail

template <bool loop, typename... Ts>
class LabelBase {
 protected:
  static constexpr size_t size = sizeof...(Ts);

  LabelBase(const LabelBase&) = delete;
  LabelBase& operator=(const LabelBase&) = delete;

 public:
  static constexpr bool is_loop = loop;
  using values_t = std::tuple<V<Ts>...>;
  using const_or_values_t = std::tuple<maybe_const_or_v_t<Ts>...>;
  using recorded_values_t = std::tuple<base::SmallVector<V<Ts>, 2>...>;

  Block* block() { return data_.block; }

  bool has_incoming_jump() const { return has_incoming_jump_; }

  template <typename A>
  void Goto(A& assembler, const values_t& values) {
    if (assembler.generating_unreachable_operations()) return;
    has_incoming_jump_ = true;
    Block* current_block = assembler.current_block();
    DCHECK_NOT_NULL(current_block);
    assembler.Goto(data_.block);
    RecordValues(current_block, data_, values);
  }

  template <typename A>
  void GotoIf(A& assembler, OpIndex condition, BranchHint hint,
              const values_t& values) {
    if (assembler.generating_unreachable_operations()) return;
    has_incoming_jump_ = true;
    Block* current_block = assembler.current_block();
    DCHECK_NOT_NULL(current_block);
    if (assembler.GotoIf(condition, data_.block, hint) &
        ConditionalGotoStatus::kGotoDestination) {
      RecordValues(current_block, data_, values);
    }
  }

  template <typename A>
  void GotoIfNot(A& assembler, OpIndex condition, BranchHint hint,
                 const values_t& values) {
    if (assembler.generating_unreachable_operations()) return;
    has_incoming_jump_ = true;
    Block* current_block = assembler.current_block();
    DCHECK_NOT_NULL(current_block);
    if (assembler.GotoIfNot(condition, data_.block, hint) &
        ConditionalGotoStatus::kGotoDestination) {
      RecordValues(current_block, data_, values);
    }
  }

  template <typename A>
  base::prepend_tuple_type<bool, values_t> Bind(A& assembler) {
    DCHECK(!data_.block->IsBound());
    if (!assembler.Bind(data_.block)) {
      return std::tuple_cat(std::tuple{false}, values_t{});
    }
    DCHECK_EQ(data_.block, assembler.current_block());
    return std::tuple_cat(std::tuple{true}, MaterializePhis(assembler));
  }

 protected:
  struct BlockData {
    Block* block;
    base::SmallVector<Block*, 4> predecessors;
    recorded_values_t recorded_values;

    explicit BlockData(Block* block) : block(block) {}
  };

  explicit LabelBase(Block* block) : data_(block) {
    DCHECK_NOT_NULL(data_.block);
  }

  LabelBase(LabelBase&& other) V8_NOEXCEPT
      : data_(std::move(other.data_)),
        has_incoming_jump_(other.has_incoming_jump_) {}

  static void RecordValues(Block* source, BlockData& data,
                           const values_t& values) {
    DCHECK_NOT_NULL(source);
    if (data.block->IsBound()) {
      // Cannot `Goto` to a bound block. If you are trying to construct a
      // loop, use a `LoopLabel` instead!
      UNREACHABLE();
    }
    RecordValuesImpl(data, source, values, std::make_index_sequence<size>());
  }

  template <size_t... indices>
  static void RecordValuesImpl(BlockData& data, Block* source,
                               const values_t& values,
                               std::index_sequence<indices...>) {
#ifdef DEBUG
    std::initializer_list<size_t> sizes{
        std::get<indices>(data.recorded_values).size()...};
    // There a -1 on the PredecessorCounts below, because we've emitted the
    // Goto/Branch before calling RecordValues (which we do because the
    // condition of the Goto might have been constant-folded, resulting in the
    // destination not actually being reachable).
    DCHECK(base::all_equal(
        sizes, static_cast<size_t>(data.block->PredecessorCount() - 1)));
    DCHECK_EQ(data.block->PredecessorCount() - 1, data.predecessors.size());
#endif
    (std::get<indices>(data.recorded_values)
         .push_back(std::get<indices>(values)),
     ...);
    data.predecessors.push_back(source);
  }

  template <typename A>
  values_t MaterializePhis(A& assembler) {
    return MaterializePhisImpl(assembler, data_,
                               std::make_index_sequence<size>());
  }

  template <typename A, size_t... indices>
  static values_t MaterializePhisImpl(A& assembler, BlockData& data,
                                      std::index_sequence<indices...>) {
    size_t predecessor_count = data.block->PredecessorCount();
    DCHECK_EQ(data.predecessors.size(), predecessor_count);
    // If this label has no values, we don't need any Phis.
    if constexpr (size == 0) return values_t{};

    // If this block does not have any predecessors, we shouldn't call this.
    DCHECK_LT(0, predecessor_count);
    // With 1 predecessor, we don't need any Phis.
    if (predecessor_count == 1) {
      return values_t{std::get<indices>(data.recorded_values)[0]...};
    }
    DCHECK_LT(1, predecessor_count);

    // Construct Phis.
    return values_t{assembler.Phi(
        base::VectorOf(std::get<indices>(data.recorded_values)))...};
  }

  BlockData data_;
  bool has_incoming_jump_ = false;
};

template <typename... Ts>
class Label : public LabelBase<false, Ts...> {
  using super = LabelBase<false, Ts...>;

  Label(const Label&) = delete;
  Label& operator=(const Label&) = delete;

 public:
  template <typename Reducer>
  explicit Label(Reducer* reducer) : super(reducer->Asm().NewBlock()) {}

  Label(Label&& other) V8_NOEXCEPT : super(std::move(other)) {}
};

template <typename... Ts>
class LoopLabel : public LabelBase<true, Ts...> {
  using super = LabelBase<true, Ts...>;
  using BlockData = typename super::BlockData;

  LoopLabel(const LoopLabel&) = delete;
  LoopLabel& operator=(const LoopLabel&) = delete;

 public:
  using values_t = typename super::values_t;
  template <typename Reducer>
  explicit LoopLabel(Reducer* reducer)
      : super(reducer->Asm().NewBlock()),
        loop_header_data_{reducer->Asm().NewLoopHeader()} {}

  LoopLabel(LoopLabel&& other) V8_NOEXCEPT
      : super(std::move(other)),
        loop_header_data_(std::move(other.loop_header_data_)),
        pending_loop_phis_(std::move(other.pending_loop_phis_)) {}

  Block* loop_header() const { return loop_header_data_.block; }

  template <typename A>
  void Goto(A& assembler, const values_t& values) {
    if (assembler.generating_unreachable_operations()) return;
    if (!loop_header_data_.block->IsBound()) {
      // If the loop header is not bound yet, we have the forward edge to the
      // loop.
      DCHECK_EQ(0, loop_header_data_.block->PredecessorCount());
      Block* current_block = assembler.current_block();
      DCHECK_NOT_NULL(current_block);
      assembler.Goto(loop_header_data_.block);
      super::RecordValues(current_block, loop_header_data_, values);
    } else {
      // We have a jump back to the loop header and wire it to the single
      // backedge block.
      this->super::Goto(assembler, values);
    }
  }

  template <typename A>
  void GotoIf(A& assembler, OpIndex condition, BranchHint hint,
              const values_t& values) {
    if (assembler.generating_unreachable_operations()) return;
    if (!loop_header_data_.block->IsBound()) {
      // If the loop header is not bound yet, we have the forward edge to the
      // loop.
      DCHECK_EQ(0, loop_header_data_.block->PredecessorCount());
      Block* current_block = assembler.current_block();
      DCHECK_NOT_NULL(current_block);
      if (assembler.GotoIf(condition, loop_header_data_.block, hint) &
          ConditionalGotoStatus::kGotoDestination) {
        super::RecordValues(current_block, loop_header_data_, values);
      }
    } else {
      // We have a jump back to the loop header and wire it to the single
      // backedge block.
      this->super::GotoIf(assembler, condition, hint, values);
    }
  }

  template <typename A>
  void GotoIfNot(A& assembler, OpIndex condition, BranchHint hint,
                 const values_t& values) {
    if (assembler.generating_unreachable_operations()) return;
    if (!loop_header_data_.block->IsBound()) {
      // If the loop header is not bound yet, we have the forward edge to the
      // loop.
      DCHECK_EQ(0, loop_header_data_.block->PredecessorCount());
      Block* current_block = assembler.current_block();
      DCHECK_NOT_NULL(current_block);
      if (assembler.GotoIf(condition, loop_header_data_.block, hint) &
          ConditionalGotoStatus::kGotoDestination) {
        super::RecordValues(current_block, loop_header_data_, values);
      }
    } else {
      // We have a jump back to the loop header and wire it to the single
      // backedge block.
      this->super::GotoIfNot(assembler, condition, hint, values);
    }
  }

  template <typename A>
  base::prepend_tuple_type<bool, values_t> Bind(A& assembler) {
    // LoopLabels must not be bound  using `Bind`, but with `Loop`.
    UNREACHABLE();
  }

  template <typename A>
  base::prepend_tuple_type<bool, values_t> BindLoop(A& assembler) {
    DCHECK(!loop_header_data_.block->IsBound());
    if (!assembler.Bind(loop_header_data_.block)) {
      return std::tuple_cat(std::tuple{false}, values_t{});
    }
    DCHECK_EQ(loop_header_data_.block, assembler.current_block());
    values_t pending_loop_phis =
        MaterializeLoopPhis(assembler, loop_header_data_);
    pending_loop_phis_ = pending_loop_phis;
    return std::tuple_cat(std::tuple{true}, pending_loop_phis);
  }

  template <typename A>
  void EndLoop(A& assembler) {
    // First, we need to bind the backedge block.
    auto bind_result = this->super::Bind(assembler);
    // `Bind` returns a tuple with a `bool` as first entry that indicates
    // whether the block was bound. The rest of the tuple contains the phi
    // values. Check if this block was bound (aka is reachable).
    if (std::get<0>(bind_result)) {
      // The block is bound.
      DCHECK_EQ(assembler.current_block(), this->super::block());
      // Now we build a jump from this block to the loop header.
      // Remove the "bound"-flag from the beginning of the tuple.
      auto values = base::tuple_drop<1>(bind_result);
      assembler.Goto(loop_header_data_.block);
      // Finalize Phis in the loop header.
      FixLoopPhis(assembler, values);
    }
    assembler.FinalizeLoop(loop_header_data_.block);
  }

 private:
  template <typename A>
  static values_t MaterializeLoopPhis(A& assembler, BlockData& data) {
    return MaterializeLoopPhisImpl(assembler, data,
                                   std::make_index_sequence<super::size>());
  }

  template <typename A, size_t... indices>
  static values_t MaterializeLoopPhisImpl(A& assembler, BlockData& data,
                                          std::index_sequence<indices...>) {
    size_t predecessor_count = data.block->PredecessorCount();
    USE(predecessor_count);
    DCHECK_EQ(data.predecessors.size(), predecessor_count);
    // If this label has no values, we don't need any Phis.
    if constexpr (super::size == 0) return typename super::values_t{};

    DCHECK_EQ(predecessor_count, 1);
    auto phis = typename super::values_t{assembler.PendingLoopPhi(
        std::get<indices>(data.recorded_values)[0])...};
    return phis;
  }

  template <typename A>
  void FixLoopPhis(A& assembler, const typename super::values_t& values) {
    DCHECK(loop_header_data_.block->IsBound());
    DCHECK(loop_header_data_.block->IsLoop());
    DCHECK_LE(1, loop_header_data_.predecessors.size());
    DCHECK_LE(loop_header_data_.predecessors.size(), 2);
    FixLoopPhi<0>(assembler, values);
  }

  template <size_t I, typename A>
  void FixLoopPhi(A& assembler, const typename super::values_t& values) {
    if constexpr (I < std::tuple_size_v<typename super::values_t>) {
      OpIndex phi_index = std::get<I>(*pending_loop_phis_);
      PendingLoopPhiOp& pending_loop_phi =
          assembler.output_graph()
              .Get(phi_index)
              .template Cast<PendingLoopPhiOp>();
      DCHECK_EQ(pending_loop_phi.first(),
                std::get<I>(loop_header_data_.recorded_values)[0]);
      assembler.output_graph().template Replace<PhiOp>(
          phi_index,
          base::VectorOf<OpIndex>(
              {pending_loop_phi.first(), std::get<I>(values)}),
          pending_loop_phi.rep);
      FixLoopPhi<I + 1>(assembler, values);
    }
  }

  BlockData loop_header_data_;
  std::optional<values_t> pending_loop_phis_;
};

namespace detail {
template <typename T>
struct LoopLabelForHelper;
template <typename T>
struct LoopLabelForHelper<V<T>> {
  using type = LoopLabel<T>;
};
template <typename... Ts>
struct LoopLabelForHelper<std::tuple<V<Ts>...>> {
  using type = LoopLabel<Ts...>;
};
}  // namespace detail

template <typename T>
using LoopLabelFor = typename detail::LoopLabelForHelper<T>::type;

Handle<Code> BuiltinCodeHandle(Builtin builtin, Isolate* isolate);

template <typename Next>
class TurboshaftAssemblerOpInterface;

template <typename T>
class Uninitialized {
  static_assert(is_subtype_v<T, HeapObject>);

 public:
  explicit Uninitialized(V<T> object) : object_(object) {}

 private:
  template <typename Next>
  friend class TurboshaftAssemblerOpInterface;

  V<T> object() const {
    DCHECK(object_.has_value());
    return *object_;
  }

  V<T> ReleaseObject() {
    DCHECK(object_.has_value());
    auto temp = *object_;
    object_.reset();
    return temp;
  }

  std::optional<V<T>> object_;
};

// Forward declarations
template <class Assembler>
class GraphVisitor;
template <class Next>
class ValueNumberingReducer;
template <class Next>
class EmitProjectionReducer;

template <class Assembler, bool has_gvn, template <class> class... Reducers>
class ReducerStack {};

// The following overloads of ReducerStack build the reducer stack, with 2
// subtleties:
//  - Inserting an EmitProjectionReducer in the right place: right before the
//    ValueNumberingReducer if the stack has a ValueNumberingReducer, and right
//    before the last reducer of the stack otherwise.
//  - Inserting a GenericReducerBase right before the last reducer of the stack.
//    This last reducer should have a kIsBottomOfStack member defined, and
//    should be an IR-specific reducer (like TSReducerBase).

// Insert the GenericReducerBase before the bottom-most reducer of the stack.
template <class Assembler, template <class> class LastReducer>
class ReducerStack<Assembler, true, LastReducer>
    : public GenericReducerBase<LastReducer<ReducerStack<Assembler, true>>> {
  static_assert(LastReducer<ReducerStack<Assembler, false>>::kIsBottomOfStack);

 public:
  using GenericReducerBase<
      LastReducer<ReducerStack<Assembler, true>>>::GenericReducerBase;
};

// The stack has no ValueNumberingReducer, so we insert the
// EmitProjectionReducer right before the GenericReducerBase (which we insert
// before the bottom-most reducer).
template <class Assembler, template <class> class LastReducer>
class ReducerStack<Assembler, false, LastReducer>
    : public EmitProjectionReducer<
          GenericReducerBase<LastReducer<ReducerStack<Assembler, false>>>> {
  static_assert(LastReducer<ReducerStack<Assembler, false>>::kIsBottomOfStack);

 public:
  using EmitProjectionReducer<GenericReducerBase<
      LastReducer<ReducerStack<Assembler, false>>>>::EmitProjectionReducer;
};

// We insert an EmitProjectionReducer right before the ValueNumberingReducer
template <class Assembler, template <class> class... Reducers>
class ReducerStack<Assembler, true, ValueNumberingReducer, Reducers...>
    : public EmitProjectionReducer<
          ValueNumberingReducer<ReducerStack<Assembler, true, Reducers...>>> {
 public:
  using EmitProjectionReducer<ValueNumberingReducer<
      ReducerStack<Assembler, true, Reducers...>>>::EmitProjectionReducer;
};

template <class Assembler, bool has_gvn, template <class> class FirstReducer,
          template <class> class... Reducers>
class ReducerStack<Assembler, has_gvn, FirstReducer, Reducers...>
    : public FirstReducer<ReducerStack<Assembler, has_gvn, Reducers...>> {
 public:
  using FirstReducer<
      ReducerStack<Assembler, has_gvn, Reducers...>>::FirstReducer;
};

template <class Reducers, bool has_gvn>
class ReducerStack<Assembler<Reducers>, has_gvn> {
 public:
  using AssemblerType = Assembler<Reducers>;
  using ReducerList = Reducers;
  Assembler<ReducerList>& Asm() {
    return *static_cast<Assembler<ReducerList>*>(this);
  }
};

template <class Reducers>
struct reducer_stack_type {};

template <template <class> class... Reducers>
struct reducer_stack_type<reducer_list<Reducers...>> {
  using type =
      ReducerStack<Assembler<reducer_list<Reducers...>>,
                   (is_same_reducer<ValueNumberingReducer, Reducers>::value ||
                    ...),
                   Reducers...>;
};

template <typename Next>
class GenericReducerBase;

// TURBOSHAFT_REDUCER_GENERIC_BOILERPLATE should almost never be needed: it
// should only be used by the IR-specific base class, while other reducers
// should simply use `TURBOSHAFT_REDUCER_BOILERPLATE`.
#define TURBOSHAFT_REDUCER_GENERIC_BOILERPLATE(Name)                      \
  using ReducerList = typename Next::ReducerList;                         \
  using assembler_t = compiler::turboshaft::Assembler<ReducerList>;       \
  assembler_t& Asm() { return *static_cast<assembler_t*>(this); }         \
  template <class T>                                                      \
  using ScopedVar = compiler::turboshaft::ScopedVariable<T, assembler_t>; \
  using CatchScope = compiler::turboshaft::CatchScopeImpl<assembler_t>;   \
  static constexpr auto& ReducerName() { return #Name; }

// Defines a few helpers to use the Assembler and its stack in Reducers.
#define TURBOSHAFT_REDUCER_BOILERPLATE(Name)   \
  TURBOSHAFT_REDUCER_GENERIC_BOILERPLATE(Name) \
  using node_t = typename Next::node_t;        \
  using block_t = typename Next::block_t;

template <class T, class Assembler>
class ScopedVariable : Variable {
  using value_type = maybe_const_or_v_t<T>;

 public:
  template <typename Reducer>
  explicit ScopedVariable(Reducer* reducer)
      : Variable(reducer->Asm().NewVariable(
            static_cast<const RegisterRepresentation&>(V<T>::rep))),
        assembler_(reducer->Asm()) {}
  template <typename Reducer>
  ScopedVariable(Reducer* reducer, value_type initial_value)
      : ScopedVariable(reducer) {
    assembler_.SetVariable(*this, assembler_.resolve(initial_value));
  }

  ScopedVariable(const ScopedVariable&) = delete;
  ScopedVariable(ScopedVariable&&) = delete;
  ScopedVariable& operator=(const ScopedVariable) = delete;
  ScopedVariable& operator=(ScopedVariable&&) = delete;
  ~ScopedVariable() {
    // Explicitly mark the variable as invalid to avoid the creation of
    // unnecessary loop phis.
    assembler_.SetVariable(*this, OpIndex::Invalid());
  }

  void Set(value_type new_value) {
    assembler_.SetVariable(*this, assembler_.resolve(new_value));
  }
  V<T> Get() const { return assembler_.GetVariable(*this); }

  void operator=(value_type new_value) { Set(new_value); }
  template <typename U,
            typename = std::enable_if_t<
                v_traits<U>::template implicitly_constructible_from<T>::value>>
  operator V<U>() const {
    return Get();
  }
  template <typename U,
            typename = std::enable_if_t<
                v_traits<U>::template implicitly_constructible_from<T>::value>>
  operator OptionalV<U>() const {
    return Get();
  }
  template <typename U,
            typename = std::enable_if_t<
                const_or_v_exists_v<U> &&
                v_traits<U>::template implicitly_constructible_from<T>::value>>
  operator ConstOrV<U>() const {
    return Get();
  }
  operator OpIndex() const { return Get(); }
  operator OptionalOpIndex() const { return Get(); }

 private:
  Assembler& assembler_;
};

// LABEL_BLOCK is used in Reducers to have a single call forwarding to the next
// reducer without change. A typical use would be:
//
//     OpIndex ReduceFoo(OpIndex arg) {
//       LABEL_BLOCK(no_change) return Next::ReduceFoo(arg);
//       ...
//       if (...) goto no_change;
//       ...
//       if (...) goto no_change;
//       ...
//     }
#define LABEL_BLOCK(label)     \
  for (; false; UNREACHABLE()) \
  label:

// EmitProjectionReducer ensures that projections are always emitted right after
// their input operation. To do so, when an operation with multiple outputs is
// emitted, it always emit its projections, and returns a Tuple of the
// projections.
// It should "towards" the bottom of the stack (so that calling Next::ReduceXXX
// just emits XXX without emitting any operation afterwards, and so that
// Next::ReduceXXX does indeed emit XXX rather than lower/optimize it to some
// other subgraph), but it should be before GlobalValueNumbering, so that
// operations with multiple outputs can still be GVNed.
template <class Next>
class EmitProjectionReducer
    : public UniformReducerAdapter<EmitProjectionReducer, Next> {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(EmitProjection)

  V<Object> ReduceCatchBlockBegin() {
    // CatchBlockBegin have a single output, so they never have projections,
    // but additionally split-edge can transform CatchBlockBeginOp into PhiOp,
    // which means that there is no guarantee here that Next::CatchBlockBegin is
    // indeed a CatchBlockBegin (which means that the .Cast<> of the generic
    // ReduceOperation could fail on CatchBlockBegin).
    return Next::ReduceCatchBlockBegin();
  }

  template <Opcode opcode, typename Continuation, typename... Args>
  OpIndex ReduceOperation(Args... args) {
    OpIndex new_idx = Continuation{this}.Reduce(args...);
    const Operation& op = Asm().output_graph().Get(new_idx);
    if constexpr (MayThrow(opcode)) {
      // Operations that can throw are lowered to a Op+DidntThrow, and what we
      // get from Next::Reduce is the DidntThrow.
      return WrapInTupleIfNeeded(op.Cast<DidntThrowOp>(), new_idx);
    }
    return WrapInTupleIfNeeded(op.Cast<typename Continuation::Op>(), new_idx);
  }

 private:
  template <class Op>
  V<Any> WrapInTupleIfNeeded(const Op& op, V<Any> idx) {
    if (op.outputs_rep().size() > 1) {
      base::SmallVector<V<Any>, 8> projections;
      auto reps = op.outputs_rep();
      for (int i = 0; i < static_cast<int>(reps.size()); i++) {
        projections.push_back(Asm().Projection(idx, i, reps[i]));
      }
      return Asm().Tuple(base::VectorOf(projections));
    }
    return idx;
  }
};

// This reducer takes care of emitting Turboshaft operations. Ideally, the rest
// of the Assembler stack would be generic, and only TSReducerBase (and
// TurboshaftAssemblerOpInterface) would be Turboshaft-specific.
// TODO(dmercadier): this is currently not quite at the very bottom of the stack
// but actually before ReducerBase and ReducerBaseForwarder. This doesn't
// matter, because Emit should be unique on the reducer stack, but still, it
// would be nice to have the TSReducerBase at the very bottom of the stack.
template <class Next>
class TSReducerBase : public Next {
 public:
  static constexpr bool kIsBottomOfStack = true;
  TURBOSHAFT_REDUCER_GENERIC_BOILERPLATE(TSReducerBase)
  using node_t = OpIndex;
  using block_t = Block;

  template <class Op, class... Args>
  OpIndex Emit(Args... args) {
    static_assert((std::is_base_of<Operation, Op>::value));
    static_assert(!(std::is_same<Op, Operation>::value));
    DCHECK_NOT_NULL(Asm().current_block());
    OpIndex result = Asm().output_graph().next_operation_index();
    Op& op = Asm().output_graph().template Add<Op>(args...);
    Asm().output_graph().operation_origins()[result] =
        Asm().current_operation_origin();
#ifdef DEBUG
    if (v8_flags.turboshaft_trace_intermediate_reductions) {
      std::cout << std::setw(Asm().intermediate_tracing_depth()) << ' ' << "["
                << ReducerName() << "]: emitted " << op << "\n";
    }
    op_to_block_[result] = Asm().current_block();
    DCHECK(ValidInputs(result));
#endif  // DEBUG
    if (op.IsBlockTerminator()) Asm().FinalizeBlock();
    return result;
  }

 private:
#ifdef DEBUG
  GrowingOpIndexSidetable<Block*> op_to_block_{Asm().phase_zone(),
                                               &Asm().output_graph()};

  bool ValidInputs(OpIndex op_idx) {
    const Operation& op = Asm().output_graph().Get(op_idx);
    if (auto* phi = op.TryCast<PhiOp>()) {
      auto pred_blocks = Asm().current_block()->Predecessors();
      for (size_t i = 0; i < phi->input_count; ++i) {
        Block* input_block = op_to_block_[phi->input(i)];
        Block* pred_block = pred_blocks[i];
        if (input_block->GetCommonDominator(pred_block) != input_block) {
          std::cerr << "Input #" << phi->input(i).id()
                    << " does not dominate predecessor B"
                    << pred_block->index().id() << ".\n";
          std::cerr << op_idx.id() << ": " << op << "\n";
          return false;
        }
      }
    } else {
      for (OpIndex input : op.inputs()) {
        Block* input_block = op_to_block_[input];
        if (input_block->GetCommonDominator(Asm().current_block()) !=
            input_block) {
          std::cerr << "Input #" << input.id()
                    << " does not dominate its use.\n";
          std::cerr << op_idx.id() << ": " << op << "\n";
          return false;
        }
      }
    }
    return true;
  }
#endif  // DEBUG
};

namespace detail {
template <typename T>
inline T&& MakeShadowy(T&& value) {
  static_assert(!std::is_same_v<std::remove_reference_t<T>, OpIndex>);
  return std::forward<T>(value);
}
inline ShadowyOpIndex MakeShadowy(OpIndex value) {
  return ShadowyOpIndex{value};
}
template <typename T>
inline ShadowyOpIndex MakeShadowy(V<T> value) {
  return ShadowyOpIndex{value};
}
inline ShadowyOpIndexVectorWrapper MakeShadowy(
    base::Vector<const OpIndex> value) {
  return ShadowyOpIndexVectorWrapper{value};
}
template <typename T>
inline ShadowyOpIndexVectorWrapper MakeShadowy(base::Vector<const V<T>> value) {
  return ShadowyOpIndexVectorWrapper{value};
}
}  // namespace detail

// This empty base-class is used to provide default-implementations of plain
// methods emitting operations.
template <class Next>
class ReducerBaseForwarder : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(ReducerBaseForwarder)

#define EMIT_OP(Name)                                                         \
  OpIndex ReduceInputGraph##Name(OpIndex ig_index, const Name##Op& op) {      \
    return this->Asm().AssembleOutputGraph##Name(op);                         \
  }                                                                           \
  template <class... Args>                                                    \
  OpIndex Reduce##Name(Args... args) {                                        \
    return this->Asm().template Emit<Name##Op>(detail::MakeShadowy(args)...); \
  }
  TURBOSHAFT_OPERATION_LIST(EMIT_OP)
#undef EMIT_OP
};

// GenericReducerBase provides default implementations of Branch-related
// Operations (Goto, Branch, Switch, CheckException), and takes care of updating
// Block predecessors (and calls the Assembler to maintain split-edge form).
// ReducerBase is always added by Assembler at the bottom of the reducer stack.
template <class Next>
class GenericReducerBase : public ReducerBaseForwarder<Next> {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(GenericReducerBase)

  using Base = ReducerBaseForwarder<Next>;

  void Bind(Block* block) {}

  // CanAutoInlineBlocksWithSinglePredecessor is used to control whether the
  // CopyingPhase is allowed to automatically inline blocks with a single
  // predecessor or not.
  bool CanAutoInlineBlocksWithSinglePredecessor() const { return true; }

  void Analyze() {}

#ifdef DEBUG
  void Verify(OpIndex old_index, OpIndex new_index) {}
#endif  // DEBUG

  void RemoveLast(OpIndex index_of_last_operation) {
    Asm().output_graph().RemoveLast();
  }

  void FixLoopPhi(const PhiOp& input_phi, OpIndex output_index,
                  Block* output_graph_loop) {
    if (!Asm()
             .output_graph()
             .Get(output_index)
             .template Is<PendingLoopPhiOp>()) {
      return;
    }
    DCHECK(output_graph_loop->Contains(output_index));
    auto& pending_phi = Asm()
                            .output_graph()
                            .Get(output_index)
                            .template Cast<PendingLoopPhiOp>();
#ifdef DEBUG
    DCHECK_EQ(pending_phi.rep, input_phi.rep);
    // The 1st input of the PendingLoopPhi should be the same as the original
    // Phi, except for peeled loops (where it's the same as the 2nd input when
    // computed with the VariableReducer Snapshot right before the loop was
    // emitted).
    DCHECK_IMPLIES(
        pending_phi.first() != Asm().MapToNewGraph(input_phi.input(0)),
        output_graph_loop->has_peeled_iteration());
#endif
    Asm().output_graph().template Replace<PhiOp>(
        output_index,
        base::VectorOf<OpIndex>(
            {pending_phi.first(), Asm().MapToNewGraph(input_phi.input(1))}),
        input_phi.rep);
  }

  OpIndex REDUCE(Phi)(base::Vector<const OpIndex> inputs,
                      RegisterRepresentation rep) {
    DCHECK(Asm().current_block()->IsMerge() &&
           inputs.size() == Asm().current_block()->Predecessors().size());
    return Base::ReducePhi(inputs, rep);
  }

  OpIndex REDUCE(PendingLoopPhi)(OpIndex first, RegisterRepresentation rep) {
    DCHECK(Asm().current_block()->IsLoop());
    return Base::ReducePendingLoopPhi(first, rep);
  }

  V<None> REDUCE(Goto)(Block* destination, bool is_backedge) {
    // Calling Base::Goto will call Emit<Goto>, which will call FinalizeBlock,
    // which will reset {current_block_}. We thus save {current_block_} before
    // calling Base::Goto, as we'll need it for AddPredecessor. Note also that
    // AddPredecessor might introduce some new blocks/operations if it needs to
    // split an edge, which means that it has to run after Base::Goto
    // (otherwise, the current Goto could be inserted in the wrong block).
    Block* saved_current_block = Asm().current_block();
    V<None> new_opindex = Base::ReduceGoto(destination, is_backedge);
    Asm().AddPredecessor(saved_current_block, destination, false);
    return new_opindex;
  }

  OpIndex REDUCE(Branch)(OpIndex condition, Block* if_true, Block* if_false,
                         BranchHint hint) {
    // There should never be a good reason to generate a Branch where both the
    // {if_true} and {if_false} are the same Block. If we ever decide to lift
    // this condition, then AddPredecessor and SplitEdge should be updated
    // accordingly.
    DCHECK_NE(if_true, if_false);
    Block* saved_current_block = Asm().current_block();
    OpIndex new_opindex =
        Base::ReduceBranch(condition, if_true, if_false, hint);
    Asm().AddPredecessor(saved_current_block, if_true, true);
    Asm().AddPredecessor(saved_current_block, if_false, true);
    return new_opindex;
  }

  V<Object> REDUCE(CatchBlockBegin)() {
    Block* current_block = Asm().current_block();
    if (current_block->IsBranchTarget()) {
      DCHECK_EQ(current_block->PredecessorCount(), 1);
      DCHECK_EQ(current_block->LastPredecessor()
                    ->LastOperation(Asm().output_graph())
                    .template Cast<CheckExceptionOp>()
                    .catch_block,
                current_block);
      return Base::ReduceCatchBlockBegin();
    }
    // We are trying to emit a CatchBlockBegin into a block that used to be the
    // catch_block successor but got edge-splitted into a merge. Therefore, we
    // need to emit a phi now and can rely on the predecessors all having a
    // ReduceCatchBlockBegin and nothing else.
    DCHECK(current_block->IsMerge());
    base::SmallVector<OpIndex, 8> phi_inputs;
    for (Block* predecessor : current_block->Predecessors()) {
      V<Object> catch_begin = predecessor->begin();
      DCHECK(Asm().Get(catch_begin).template Is<CatchBlockBeginOp>());
      phi_inputs.push_back(catch_begin);
    }
    return Asm().Phi(base::VectorOf(phi_inputs),
                     RegisterRepresentation::Tagged());
  }

  V<None> REDUCE(Switch)(V<Word32> input, base::Vector<SwitchOp::Case> cases,
                         Block* default_case, BranchHint default_hint) {
#ifdef DEBUG
    // Making sure that all cases and {default_case} are different. If we ever
    // decide to lift this condition, then AddPredecessor and SplitEdge should
    // be updated accordingly.
    std::unordered_set<Block*> seen;
    seen.insert(default_case);
    for (auto switch_case : cases) {
      DCHECK_EQ(seen.count(switch_case.destination), 0);
      seen.insert(switch_case.destination);
    }
#endif
    Block* saved_current_block = Asm().current_block();
    V<None> new_opindex =
        Base::ReduceSwitch(input, cases, default_case, default_hint);
    for (SwitchOp::Case c : cases) {
      Asm().AddPredecessor(saved_current_block, c.destination, true);
    }
    Asm().AddPredecessor(saved_current_block, default_case, true);
    return new_opindex;
  }

  V<Any> REDUCE(Call)(V<CallTarget> callee,
                      OptionalV<turboshaft::FrameState> frame_state,
                      base::Vector<const OpIndex> arguments,
                      const TSCallDescriptor* descriptor, OpEffects effects) {
    V<Any> raw_call =
        Base::ReduceCall(callee, frame_state, arguments, descriptor, effects);
    bool has_catch_block = false;
    if (descriptor->can_throw == CanThrow::kYes) {
      // TODO(nicohartmann@): Unfortunately, we have many descriptors where
      // effects are not set consistently with {can_throw}. We should fix those
      // and reenable this DCHECK.
      // DCHECK(effects.is_required_when_unused());
      effects = effects.RequiredWhenUnused();
      has_catch_block = CatchIfInCatchScope(raw_call);
    }
    return ReduceDidntThrow(raw_call, has_catch_block, &descriptor->out_reps,
                            effects);
  }

#define REDUCE_THROWING_OP(Name)                                             \
  template <typename... Args>                                                \
  V<Any> Reduce##Name(Args... args) {                                        \
    OpIndex raw_op_index = Base::Reduce##Name(args...);                      \
    bool has_catch_block = CatchIfInCatchScope(raw_op_index);                \
    const Name##Op& raw_op =                                                 \
        Asm().output_graph().Get(raw_op_index).template Cast<Name##Op>();    \
    return ReduceDidntThrow(raw_op_index, has_catch_block, &raw_op.kOutReps, \
                            raw_op.Effects());                               \
  }
  TURBOSHAFT_THROWING_STATIC_OUTPUTS_OPERATIONS_LIST(REDUCE_THROWING_OP)
#undef REDUCE_THROWING_OP

 private:
  // These reduce functions are private, as they should only be emitted
  // automatically by `CatchIfInCatchScope` and `DoNotCatch` defined below and
  // never explicitly.
  using Base::ReduceDidntThrow;
  V<None> REDUCE(CheckException)(V<Any> throwing_operation, Block* successor,
                                 Block* catch_block) {
    // {successor} and {catch_block} should never be the same.  AddPredecessor
    // and SplitEdge rely on this.
    DCHECK_NE(successor, catch_block);
    Block* saved_current_block = Asm().current_block();
    V<None> new_opindex =
        Base::ReduceCheckException(throwing_operation, successor, catch_block);
    Asm().AddPredecessor(saved_current_block, successor, true);
    Asm().AddPredecessor(saved_current_block, catch_block, true);
    return new_opindex;
  }

  bool CatchIfInCatchScope(OpIndex throwing_operation) {
    if (Asm().current_catch_block()) {
      Block* successor = Asm().NewBlock();
      ReduceCheckException(throwing_operation, successor,
                           Asm().current_catch_block());
      Asm().BindReachable(successor);
      return true;
    }
    return false;
  }
};

namespace detail {

template <typename LoopLabel, typename Iterable, typename Iterator,
          typename ValueTuple, size_t... Indices>
auto BuildResultTupleImpl(bool bound, Iterable&& iterable,
                          LoopLabel&& loop_header, Label<> loop_exit,
                          Iterator current_iterator, ValueTuple current_values,
                          std::index_sequence<Indices...>) {
  return std::make_tuple(bound, std::forward<Iterable>(iterable),
                         std::forward<LoopLabel>(loop_header),
                         std::move(loop_exit), current_iterator,
                         std::get<Indices>(current_values)...);
}

template <typename LoopLabel, typename Iterable, typename Iterator,
          typename Value>
auto BuildResultTuple(bool bound, Iterable&& iterable, LoopLabel&& loop_header,
                      Label<> loop_exit, Iterator current_iterator,
                      Value current_value) {
  return std::make_tuple(bound, std::forward<Iterable>(iterable),
                         std::forward<LoopLabel>(loop_header),
                         std::move(loop_exit), current_iterator, current_value);
}

template <typename LoopLabel, typename Iterable, typename Iterator,
          typename... Values>
auto BuildResultTuple(bool bound, Iterable&& iterable, LoopLabel&& loop_header,
                      Label<> loop_exit, Iterator current_iterator,
                      std::tuple<Values...> current_values) {
  static_assert(std::tuple_size_v<Iterator> == sizeof...(Values));
  return BuildResultTupleImpl(bound, std::forward<Iterable>(iterable),
                              std::forward<LoopLabel>(loop_header),
                              std::move(loop_exit), std::move(current_iterator),
                              std::move(current_values),
                              std::make_index_sequence<sizeof...(Values)>{});
}

}  // namespace detail

template <class Next>
class GenericAssemblerOpInterface : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(GenericAssemblerOpInterface)

  // These methods are used by the assembler macros (BIND, BIND_LOOP, GOTO,
  // GOTO_IF).
  template <typename L>
  auto ControlFlowHelper_Bind(L& label)
      -> base::prepend_tuple_type<bool, typename L::values_t> {
    // LoopLabels need to be bound with `BIND_LOOP` instead of `BIND`.
    static_assert(!L::is_loop);
    return label.Bind(Asm());
  }

  template <typename L>
  auto ControlFlowHelper_BindLoop(L& label)
      -> base::prepend_tuple_type<bool, typename L::values_t> {
    // Only LoopLabels can be bound with `BIND_LOOP`. Otherwise use `BIND`.
    static_assert(L::is_loop);
    return label.BindLoop(Asm());
  }

  template <typename L>
  void ControlFlowHelper_EndLoop(L& label) {
    static_assert(L::is_loop);
    label.EndLoop(Asm());
  }

  template <CONCEPT(ForeachIterable<assembler_t>) It>
  auto ControlFlowHelper_Foreach(It iterable) {
    // We need to take ownership over the `iterable` instance as we need to make
    // sure that the `ControlFlowHelper_Foreach` and
    // `ControlFlowHelper_EndForeachLoop` functions operate on the same object.
    // This can potentially involve copying the `iterable` if it is not moved to
    // the `FOREACH` macro. `ForeachIterable`s should be cheap to copy and they
    // MUST NOT emit any code in their constructors/destructors.
#ifdef DEBUG
    OpIndex next_index = Asm().output_graph().next_operation_index();
    {
      It temp_copy = iterable;
      USE(temp_copy);
    }
    // Make sure we have not emitted any operations.
    DCHECK_EQ(next_index, Asm().output_graph().next_operation_index());
#endif

    LoopLabelFor<typename It::iterator_type> loop_header(this);
    Label<> loop_exit(this);

    typename It::iterator_type begin = iterable.Begin(Asm());

    ControlFlowHelper_Goto(loop_header, {begin});

    auto bound_and_current_iterator = loop_header.BindLoop(Asm());
    auto [bound] = base::tuple_head<1>(bound_and_current_iterator);
    auto current_iterator = detail::unwrap_unary_tuple(
        base::tuple_drop<1>(bound_and_current_iterator));
    OptionalV<Word32> is_end = iterable.IsEnd(Asm(), current_iterator);
    if (is_end.has_value()) {
      ControlFlowHelper_GotoIf(is_end.value(), loop_exit, {});
    }

    typename It::value_type current_value =
        iterable.Dereference(Asm(), current_iterator);

    return detail::BuildResultTuple(
        bound, std::move(iterable), std::move(loop_header),
        std::move(loop_exit), current_iterator, current_value);
  }

  template <CONCEPT(ForeachIterable<assembler_t>) It>
  void ControlFlowHelper_EndForeachLoop(
      It iterable, LoopLabelFor<typename It::iterator_type>& header_label,
      Label<>& exit_label, typename It::iterator_type current_iterator) {
    typename It::iterator_type next_iterator =
        iterable.Advance(Asm(), current_iterator);
    ControlFlowHelper_Goto(header_label, {next_iterator});
    ControlFlowHelper_EndLoop(header_label);
    ControlFlowHelper_Bind(exit_label);
  }

  std::tuple<bool, LoopLabel<>, Label<>> ControlFlowHelper_While(
      std::function<V<Word32>()> cond_builder) {
    LoopLabel<> loop_header(this);
    Label<> loop_exit(this);

    ControlFlowHelper_Goto(loop_header, {});

    auto [bound] = loop_header.BindLoop(Asm());
    V<Word32> cond = cond_builder();
    ControlFlowHelper_GotoIfNot(cond, loop_exit, {});

    return std::make_tuple(bound, std::move(loop_header), std::move(loop_exit));
  }

  template <typename L1, typename L2>
  void ControlFlowHelper_EndWhileLoop(L1& header_label, L2& exit_label) {
    static_assert(L1::is_loop);
    static_assert(!L2::is_loop);
    ControlFlowHelper_Goto(header_label, {});
    ControlFlowHelper_EndLoop(header_label);
    ControlFlowHelper_Bind(exit_label);
  }

  template <typename L>
  void ControlFlowHelper_Goto(L& label,
                              const typename L::const_or_values_t& values) {
    auto resolved_values = detail::ResolveAll(Asm(), values);
    label.Goto(Asm(), resolved_values);
  }

  template <typename L>
  void ControlFlowHelper_GotoIf(ConditionWithHint condition, L& label,
                                const typename L::const_or_values_t& values) {
    auto resolved_values = detail::ResolveAll(Asm(), values);
    label.GotoIf(Asm(), condition.condition(), condition.hint(),
                 resolved_values);
  }

  template <typename L>
  void ControlFlowHelper_GotoIfNot(
      ConditionWithHint condition, L& label,
      const typename L::const_or_values_t& values) {
    auto resolved_values = detail::ResolveAll(Asm(), values);
    label.GotoIfNot(Asm(), condition.condition(), condition.hint(),
                    resolved_values);
  }

  struct ControlFlowHelper_IfState {
    block_t* else_block;
    block_t* end_block;
  };

  bool ControlFlowHelper_BindIf(ConditionWithHint condition,
                                ControlFlowHelper_IfState* state) {
    block_t* then_block = Asm().NewBlock();
    state->else_block = Asm().NewBlock();
    state->end_block = Asm().NewBlock();
    Asm().Branch(condition, then_block, state->else_block);
    return Asm().Bind(then_block);
  }

  bool ControlFlowHelper_BindIfNot(ConditionWithHint condition,
                                   ControlFlowHelper_IfState* state) {
    block_t* then_block = Asm().NewBlock();
    state->else_block = Asm().NewBlock();
    state->end_block = Asm().NewBlock();
    Asm().Branch(condition, state->else_block, then_block);
    return Asm().Bind(then_block);
  }

  bool ControlFlowHelper_BindElse(ControlFlowHelper_IfState* state) {
    block_t* else_block = state->else_block;
    state->else_block = nullptr;
    return Asm().Bind(else_block);
  }

  void ControlFlowHelper_FinishIfBlock(ControlFlowHelper_IfState* state) {
    if (Asm().current_block() == nullptr) return;
    Asm().Goto(state->end_block);
  }

  void ControlFlowHelper_EndIf(ControlFlowHelper_IfState* state) {
    if (state->else_block) {
      if (Asm().Bind(state->else_block)) {
        Asm().Goto(state->end_block);
      }
    }
    Asm().Bind(state->end_block);
  }
};

template <class Next>
class TurboshaftAssemblerOpInterface
    : public GenericAssemblerOpInterface<Next> {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(TurboshaftAssemblerOpInterface)

  template <typename... Args>
  explicit TurboshaftAssemblerOpInterface(Args... args)
      : GenericAssemblerOpInterface<Next>(args...),
        matcher_(Asm().output_graph()) {}

  const OperationMatcher& matcher() const { return matcher_; }

  // ReduceProjection eliminates projections to tuples and returns instead the
  // corresponding tuple input. We do this at the top of the stack to avoid
  // passing this Projection around needlessly. This is in particular important
  // to ValueNumberingReducer, which assumes that it's at the bottom of the
  // stack, and that the BaseReducer will actually emit an Operation. If we put
  // this projection-to-tuple-simplification in the BaseReducer, then this
  // assumption of the ValueNumberingReducer will break.
  V<Any> REDUCE(Projection)(V<Any> tuple, uint16_t index,
                            RegisterRepresentation rep) {
    if (auto* tuple_op = Asm().matcher().template TryCast<TupleOp>(tuple)) {
      return tuple_op->input(index);
    }
    return Next::ReduceProjection(tuple, index, rep);
  }

  // Methods to be used by the reducers to reducer operations with the whole
  // reducer stack.

  V<Object> GenericBinop(V<Object> left, V<Object> right,
                         V<turboshaft::FrameState> frame_state,
                         V<Context> context, GenericBinopOp::Kind kind,
                         LazyDeoptOnThrow lazy_deopt_on_throw) {
    return ReduceIfReachableGenericBinop(left, right, frame_state, context,
                                         kind, lazy_deopt_on_throw);
  }
#define DECL_GENERIC_BINOP(Name)                                              \
  V<Object> Generic##Name(                                                    \
      V<Object> left, V<Object> right, V<turboshaft::FrameState> frame_state, \
      V<Context> context, LazyDeoptOnThrow lazy_deopt_on_throw) {             \
    return GenericBinop(left, right, frame_state, context,                    \
                        GenericBinopOp::Kind::k##Name, lazy_deopt_on_throw);  \
  }
  GENERIC_BINOP_LIST(DECL_GENERIC_BINOP)
#undef DECL_GENERIC_BINOP

  V<Object> GenericUnop(V<Object> input, V<turboshaft::FrameState> frame_state,
                        V<Context> context, GenericUnopOp::Kind kind,
                        LazyDeoptOnThrow lazy_deopt_on_throw) {
    return ReduceIfReachableGenericUnop(input, frame_state, context, kind,
                                        lazy_deopt_on_throw);
  }
#define DECL_GENERIC_UNOP(Name)                                            \
  V<Object> Generic##Name(                                                 \
      V<Object> input, V<turboshaft::FrameState> frame_state,              \
      V<Context> context, LazyDeoptOnThrow lazy_deopt_on_throw) {          \
    return GenericUnop(input, frame_state, context,                        \
                       GenericUnopOp::Kind::k##Name, lazy_deopt_on_throw); \
  }
  GENERIC_UNOP_LIST(DECL_GENERIC_UNOP)
#undef DECL_GENERIC_UNOP

  V<Object> ToNumberOrNumeric(V<Object> input,
                              V<turboshaft::FrameState> frame_state,
                              V<Context> context, Object::Conversion kind,
                              LazyDeoptOnThrow lazy_deopt_on_throw) {
    return ReduceIfReachableToNumberOrNumeric(input, frame_state, context, kind,
                                              lazy_deopt_on_throw);
  }
  V<Object> ToNumber(V<Object> input, V<turboshaft::FrameState> frame_state,
                     V<Context> context, LazyDeoptOnThrow lazy_deopt_on_throw) {
    return ToNumberOrNumeric(input, frame_state, context,
                             Object::Conversion::kToNumber,
                             lazy_deopt_on_throw);
  }
  V<Object> ToNumeric(V<Object> input, V<turboshaft::FrameState> frame_state,
                      V<Context> context,
                      LazyDeoptOnThrow lazy_deopt_on_throw) {
    return ToNumberOrNumeric(input, frame_state, context,
                             Object::Conversion::kToNumeric,
                             lazy_deopt_on_throw);
  }

#define DECL_MULTI_REP_BINOP(name, operation, rep_type, kind)               \
  OpIndex name(OpIndex left, OpIndex right, rep_type rep) {                 \
    return ReduceIfReachable##operation(left, right,                        \
                                        operation##Op::Kind::k##kind, rep); \
  }

#define DECL_MULTI_REP_BINOP_V(name, operation, kind, tag)                  \
  V<tag> name(V<tag> left, V<tag> right, v_traits<tag>::rep_type rep) {     \
    return ReduceIfReachable##operation(left, right,                        \
                                        operation##Op::Kind::k##kind, rep); \
  }

#define DECL_SINGLE_REP_BINOP_V(name, operation, kind, tag)            \
  V<tag> name(ConstOrV<tag> left, ConstOrV<tag> right) {               \
    return ReduceIfReachable##operation(resolve(left), resolve(right), \
                                        operation##Op::Kind::k##kind,  \
                                        V<tag>::rep);                  \
  }
  DECL_MULTI_REP_BINOP_V(WordAdd, WordBinop, Add, Word)
  DECL_SINGLE_REP_BINOP_V(Word32Add, WordBinop, Add, Word32)
  DECL_SINGLE_REP_BINOP_V(Word64Add, WordBinop, Add, Word64)
  DECL_SINGLE_REP_BINOP_V(WordPtrAdd, WordBinop, Add, WordPtr)

  DECL_MULTI_REP_BINOP_V(WordMul, WordBinop, Mul, Word)
  DECL_SINGLE_REP_BINOP_V(Word32Mul, WordBinop, Mul, Word32)
  DECL_SINGLE_REP_BINOP_V(Word64Mul, WordBinop, Mul, Word64)
  DECL_SINGLE_REP_BINOP_V(WordPtrMul, WordBinop, Mul, WordPtr)

  DECL_MULTI_REP_BINOP_V(WordBitwiseAnd, WordBinop, BitwiseAnd, Word)
  DECL_SINGLE_REP_BINOP_V(Word32BitwiseAnd, WordBinop, BitwiseAnd, Word32)
  DECL_SINGLE_REP_BINOP_V(Word64BitwiseAnd, WordBinop, BitwiseAnd, Word64)
  DECL_SINGLE_REP_BINOP_V(WordPtrBitwiseAnd, WordBinop, BitwiseAnd, WordPtr)

  DECL_MULTI_REP_BINOP_V(WordBitwiseOr, WordBinop, BitwiseOr, Word)
  DECL_SINGLE_REP_BINOP_V(Word32BitwiseOr, WordBinop, BitwiseOr, Word32)
  DECL_SINGLE_REP_BINOP_V(Word64BitwiseOr, WordBinop, BitwiseOr, Word64)
  DECL_SINGLE_REP_BINOP_V(WordPtrBitwiseOr, WordBinop, BitwiseOr, WordPtr)

  DECL_MULTI_REP_BINOP_V(WordBitwiseXor, WordBinop, BitwiseXor, Word)
  DECL_SINGLE_REP_BINOP_V(Word32BitwiseXor, WordBinop, BitwiseXor, Word32)
  DECL_SINGLE_REP_BINOP_V(Word64BitwiseXor, WordBinop, BitwiseXor, Word64)

  DECL_MULTI_REP_BINOP_V(WordSub, WordBinop, Sub, Word)
  DECL_SINGLE_REP_BINOP_V(Word32Sub, WordBinop, Sub, Word32)
  DECL_SINGLE_REP_BINOP_V(Word64Sub, WordBinop, Sub, Word64)
  DECL_SINGLE_REP_BINOP_V(WordPtrSub, WordBinop, Sub, WordPtr)

  DECL_MULTI_REP_BINOP_V(IntDiv, WordBinop, SignedDiv, Word)
  DECL_SINGLE_REP_BINOP_V(Int32Div, WordBinop, SignedDiv, Word32)
  DECL_SINGLE_REP_BINOP_V(Int64Div, WordBinop, SignedDiv, Word64)
  DECL_MULTI_REP_BINOP_V(UintDiv, WordBinop, UnsignedDiv, Word)
  DECL_SINGLE_REP_BINOP_V(Uint32Div, WordBinop, UnsignedDiv, Word32)
  DECL_SINGLE_REP_BINOP_V(Uint64Div, WordBinop, UnsignedDiv, Word64)
  DECL_MULTI_REP_BINOP_V(IntMod, WordBinop, SignedMod, Word)
  DECL_SINGLE_REP_BINOP_V(Int32Mod, WordBinop, SignedMod, Word32)
  DECL_SINGLE_REP_BINOP_V(Int64Mod, WordBinop, SignedMod, Word64)
  DECL_MULTI_REP_BINOP_V(UintMod, WordBinop, UnsignedMod, Word)
  DECL_SINGLE_REP_BINOP_V(Uint32Mod, WordBinop, UnsignedMod, Word32)
  DECL_SINGLE_REP_BINOP_V(Uint64Mod, WordBinop, UnsignedMod, Word64)
  DECL_MULTI_REP_BINOP_V(IntMulOverflownBits, WordBinop, SignedMulOverflownBits,
                         Word)
  DECL_SINGLE_REP_BINOP_V(Int32MulOverflownBits, WordBinop,
                          SignedMulOverflownBits, Word32)
  DECL_SINGLE_REP_BINOP_V(Int64MulOverflownBits, WordBinop,
                          SignedMulOverflownBits, Word64)
  DECL_MULTI_REP_BINOP_V(UintMulOverflownBits, WordBinop,
                         UnsignedMulOverflownBits, Word)
  DECL_SINGLE_REP_BINOP_V(Uint32MulOverflownBits, WordBinop,
                          UnsignedMulOverflownBits, Word32)
  DECL_SINGLE_REP_BINOP_V(Uint64MulOverflownBits, WordBinop,
                          UnsignedMulOverflownBits, Word64)

  V<Word> WordBinop(V<Word> left, V<Word> right, WordBinopOp::Kind kind,
                    WordRepresentation rep) {
    return ReduceIfReachableWordBinop(left, right, kind, rep);
  }
  V<turboshaft::Tuple<Word, Word32>> OverflowCheckedBinop(
      V<Word> left, V<Word> right, OverflowCheckedBinopOp::Kind kind,
      WordRepresentation rep) {
    return ReduceIfReachableOverflowCheckedBinop(left, right, kind, rep);
  }

#define DECL_MULTI_REP_CHECK_BINOP_V(name, operation, kind, tag)            \
  V<turboshaft::Tuple<tag, Word32>> name(V<tag> left, V<tag> right,         \
                                         v_traits<tag>::rep_type rep) {     \
    return ReduceIfReachable##operation(left, right,                        \
                                        operation##Op::Kind::k##kind, rep); \
  }
#define DECL_SINGLE_REP_CHECK_BINOP_V(name, operation, kind, tag)      \
  V<turboshaft::Tuple<tag, Word32>> name(ConstOrV<tag> left,           \
                                         ConstOrV<tag> right) {        \
    return ReduceIfReachable##operation(resolve(left), resolve(right), \
                                        operation##Op::Kind::k##kind,  \
                                        V<tag>::rep);                  \
  }
  DECL_MULTI_REP_CHECK_BINOP_V(IntAddCheckOverflow, OverflowCheckedBinop,
                               SignedAdd, Word)
  DECL_SINGLE_REP_CHECK_BINOP_V(Int32AddCheckOverflow, OverflowCheckedBinop,
                                SignedAdd, Word32)
  DECL_SINGLE_REP_CHECK_BINOP_V(Int64AddCheckOverflow, OverflowCheckedBinop,
                                SignedAdd, Word64)
  DECL_MULTI_REP_CHECK_BINOP_V(IntSubCheckOverflow, OverflowCheckedBinop,
                               SignedSub, Word)
  DECL_SINGLE_REP_CHECK_BINOP_V(Int32SubCheckOverflow, OverflowCheckedBinop,
                                SignedSub, Word32)
  DECL_SINGLE_REP_CHECK_BINOP_V(Int64SubCheckOverflow, OverflowCheckedBinop,
                                SignedSub, Word64)
  DECL_MULTI_REP_CHECK_BINOP_V(IntMulCheckOverflow, OverflowCheckedBinop,
                               SignedMul, Word)
  DECL_SINGLE_REP_CHECK_BINOP_V(Int32MulCheckOverflow, OverflowCheckedBinop,
                                SignedMul, Word32)
  DECL_SINGLE_REP_CHECK_BINOP_V(Int64MulCheckOverflow, OverflowCheckedBinop,
                                SignedMul, Word64)
#undef DECL_MULTI_REP_CHECK_BINOP_V
#undef DECL_SINGLE_REP_CHECK_BINOP_V

  DECL_MULTI_REP_BINOP_V(FloatAdd, FloatBinop, Add, Float)
  DECL_SINGLE_REP_BINOP_V(Float32Add, FloatBinop, Add, Float32)
  DECL_SINGLE_REP_BINOP_V(Float64Add, FloatBinop, Add, Float64)
  DECL_MULTI_REP_BINOP_V(FloatMul, FloatBinop, Mul, Float)
  DECL_SINGLE_REP_BINOP_V(Float32Mul, FloatBinop, Mul, Float32)
  DECL_SINGLE_REP_BINOP_V(Float64Mul, FloatBinop, Mul, Float64)
  DECL_MULTI_REP_BINOP_V(FloatSub, FloatBinop, Sub, Float)
  DECL_SINGLE_REP_BINOP_V(Float32Sub, FloatBinop, Sub, Float32)
  DECL_SINGLE_REP_BINOP_V(Float64Sub, FloatBinop, Sub, Float64)
  DECL_MULTI_REP_BINOP_V(FloatDiv, FloatBinop, Div, Float)
  DECL_SINGLE_REP_BINOP_V(Float32Div, FloatBinop, Div, Float32)
  DECL_SINGLE_REP_BINOP_V(Float64Div, FloatBinop, Div, Float64)
  DECL_MULTI_REP_BINOP_V(FloatMin, FloatBinop, Min, Float)
  DECL_SINGLE_REP_BINOP_V(Float32Min, FloatBinop, Min, Float32)
  DECL_SINGLE_REP_BINOP_V(Float64Min, FloatBinop, Min, Float64)
  DECL_MULTI_REP_BINOP_V(FloatMax, FloatBinop, Max, Float)
  DECL_SINGLE_REP_BINOP_V(Float32Max, FloatBinop, Max, Float32)
  DECL_SINGLE_REP_BINOP_V(Float64Max, FloatBinop, Max, Float64)
  DECL_SINGLE_REP_BINOP_V(Float64Mod, FloatBinop, Mod, Float64)
  DECL_SINGLE_REP_BINOP_V(Float64Power, FloatBinop, Power, Float64)
  DECL_SINGLE_REP_BINOP_V(Float64Atan2, FloatBinop, Atan2, Float64)

  OpIndex Shift(OpIndex left, OpIndex right, ShiftOp::Kind kind,
                WordRepresentation rep) {
    return ReduceIfReachableShift(left, right, kind, rep);
  }

#define DECL_SINGLE_REP_SHIFT_V(name, kind, tag)                        \
  V<tag> name(ConstOrV<tag> left, ConstOrV<Word32> right) {             \
    return ReduceIfReachableShift(resolve(left), resolve(right),        \
                                  ShiftOp::Kind::k##kind, V<tag>::rep); \
  }

  DECL_MULTI_REP_BINOP(ShiftRightArithmeticShiftOutZeros, Shift,
                       WordRepresentation, ShiftRightArithmeticShiftOutZeros)
  DECL_SINGLE_REP_SHIFT_V(Word32ShiftRightArithmeticShiftOutZeros,
                          ShiftRightArithmeticShiftOutZeros, Word32)
  DECL_SINGLE_REP_SHIFT_V(Word64ShiftRightArithmeticShiftOutZeros,
                          ShiftRightArithmeticShiftOutZeros, Word64)
  DECL_SINGLE_REP_SHIFT_V(WordPtrShiftRightArithmeticShiftOutZeros,
                          ShiftRightArithmeticShiftOutZeros, WordPtr)
  DECL_MULTI_REP_BINOP(ShiftRightArithmetic, Shift, WordRepresentation,
                       ShiftRightArithmetic)
  DECL_SINGLE_REP_SHIFT_V(Word32ShiftRightArithmetic, ShiftRightArithmetic,
                          Word32)
  DECL_SINGLE_REP_SHIFT_V(Word64ShiftRightArithmetic, ShiftRightArithmetic,
                          Word64)
  DECL_SINGLE_REP_SHIFT_V(WordPtrShiftRightArithmetic, ShiftRightArithmetic,
                          WordPtr)
  DECL_MULTI_REP_BINOP(ShiftRightLogical, Shift, WordRepresentation,
                       ShiftRightLogical)
  DECL_SINGLE_REP_SHIFT_V(Word32ShiftRightLogical, ShiftRightLogical, Word32)
  DECL_SINGLE_REP_SHIFT_V(Word64ShiftRightLogical, ShiftRightLogical, Word64)
  DECL_SINGLE_REP_SHIFT_V(WordPtrShiftRightLogical, ShiftRightLogical, WordPtr)
  DECL_MULTI_REP_BINOP(ShiftLeft, Shift, WordRepresentation, ShiftLeft)
  DECL_SINGLE_REP_SHIFT_V(Word32ShiftLeft, ShiftLeft, Word32)
  DECL_SINGLE_REP_SHIFT_V(Word64ShiftLeft, ShiftLeft, Word64)
  DECL_SINGLE_REP_SHIFT_V(WordPtrShiftLeft, ShiftLeft, WordPtr)
  DECL_MULTI_REP_BINOP(RotateRight, Shift, WordRepresentation, RotateRight)
  DECL_SINGLE_REP_SHIFT_V(Word32RotateRight, RotateRight, Word32)
  DECL_SINGLE_REP_SHIFT_V(Word64RotateRight, RotateRight, Word64)
  DECL_MULTI_REP_BINOP(RotateLeft, Shift, WordRepresentation, RotateLeft)
  DECL_SINGLE_REP_SHIFT_V(Word32RotateLeft, RotateLeft, Word32)
  DECL_SINGLE_REP_SHIFT_V(Word64RotateLeft, RotateLeft, Word64)

  OpIndex ShiftRightLogical(OpIndex left, uint32_t right,
                            WordRepresentation rep) {
    DCHECK_GE(right, 0);
    DCHECK_LT(right, rep.bit_width());
    return ShiftRightLogical(left, this->Word32Constant(right), rep);
  }
  OpIndex ShiftRightArithmetic(OpIndex left, uint32_t right,
                               WordRepresentation rep) {
    DCHECK_GE(right, 0);
    DCHECK_LT(right, rep.bit_width());
    return ShiftRightArithmetic(left, this->Word32Constant(right), rep);
  }
  OpIndex ShiftLeft(OpIndex left, uint32_t right, WordRepresentation rep) {
    DCHECK_LT(right, rep.bit_width());
    return ShiftLeft(left, this->Word32Constant(right), rep);
  }

  V<Word32> Equal(V<Any> left, V<Any> right, RegisterRepresentation rep) {
    return Comparison(left, right, ComparisonOp::Kind::kEqual, rep);
  }

  V<Word32> TaggedEqual(V<Object> left, V<Object> right) {
    return Equal(left, right, RegisterRepresentation::Tagged());
  }

  V<Word32> RootEqual(V<Object> input, RootIndex root, Isolate* isolate) {
    return __ TaggedEqual(
        input, __ HeapConstant(Cast<HeapObject>(isolate->root_handle(root))));
  }

#define DECL_SINGLE_REP_EQUAL_V(name, tag)                            \
  V<Word32> name(ConstOrV<tag> left, ConstOrV<tag> right) {           \
    return ReduceIfReachableComparison(resolve(left), resolve(right), \
                                       ComparisonOp::Kind::kEqual,    \
                                       V<tag>::rep);                  \
  }
  DECL_SINGLE_REP_EQUAL_V(Word32Equal, Word32)
  DECL_SINGLE_REP_EQUAL_V(Word64Equal, Word64)
  DECL_SINGLE_REP_EQUAL_V(WordPtrEqual, WordPtr)
  DECL_SINGLE_REP_EQUAL_V(Float32Equal, Float32)
  DECL_SINGLE_REP_EQUAL_V(Float64Equal, Float64)
#undef DECL_SINGLE_REP_EQUAL_V

#define DECL_SINGLE_REP_COMPARISON_V(name, kind, tag)                 \
  V<Word32> name(ConstOrV<tag> left, ConstOrV<tag> right) {           \
    return ReduceIfReachableComparison(resolve(left), resolve(right), \
                                       ComparisonOp::Kind::k##kind,   \
                                       V<tag>::rep);                  \
  }

  DECL_MULTI_REP_BINOP(IntLessThan, Comparison, RegisterRepresentation,
                       SignedLessThan)
  DECL_SINGLE_REP_COMPARISON_V(Int32LessThan, SignedLessThan, Word32)
  DECL_SINGLE_REP_COMPARISON_V(Int64LessThan, SignedLessThan, Word64)
  DECL_SINGLE_REP_COMPARISON_V(IntPtrLessThan, SignedLessThan, WordPtr)

  DECL_MULTI_REP_BINOP(UintLessThan, Comparison, RegisterRepresentation,
                       UnsignedLessThan)
  DECL_SINGLE_REP_COMPARISON_V(Uint32LessThan, UnsignedLessThan, Word32)
  DECL_SINGLE_REP_COMPARISON_V(Uint64LessThan, UnsignedLessThan, Word64)
  DECL_SINGLE_REP_COMPARISON_V(UintPtrLessThan, UnsignedLessThan, WordPtr)
  DECL_MULTI_REP_BINOP(FloatLessThan, Comparison, RegisterRepresentation,
                       SignedLessThan)
  DECL_SINGLE_REP_COMPARISON_V(Float32LessThan, SignedLessThan, Float32)
  DECL_SINGLE_REP_COMPARISON_V(Float64LessThan, SignedLessThan, Float64)

  DECL_MULTI_REP_BINOP(IntLessThanOrEqual, Comparison, RegisterRepresentation,
                       SignedLessThanOrEqual)
  DECL_SINGLE_REP_COMPARISON_V(Int32LessThanOrEqual, SignedLessThanOrEqual,
                               Word32)
  DECL_SINGLE_REP_COMPARISON_V(Int64LessThanOrEqual, SignedLessThanOrEqual,
                               Word64)
  DECL_MULTI_REP_BINOP(UintLessThanOrEqual, Comparison, RegisterRepresentation,
                       UnsignedLessThanOrEqual)
  DECL_SINGLE_REP_COMPARISON_V(Uint32LessThanOrEqual, UnsignedLessThanOrEqual,
                               Word32)
  DECL_SINGLE_REP_COMPARISON_V(Uint64LessThanOrEqual, UnsignedLessThanOrEqual,
                               Word64)
  DECL_SINGLE_REP_COMPARISON_V(UintPtrLessThanOrEqual, UnsignedLessThanOrEqual,
                               WordPtr)
  DECL_MULTI_REP_BINOP(FloatLessThanOrEqual, Comparison, RegisterRepresentation,
                       SignedLessThanOrEqual)
  DECL_SINGLE_REP_COMPARISON_V(Float32LessThanOrEqual, SignedLessThanOrEqual,
                               Float32)
  DECL_SINGLE_REP_COMPARISON_V(Float64LessThanOrEqual, SignedLessThanOrEqual,
                               Float64)
#undef DECL_SINGLE_REP_COMPARISON_V

  OpIndex Comparison(OpIndex left, OpIndex right, ComparisonOp::Kind kind,
                     RegisterRepresentation rep) {
    return ReduceIfReachableComparison(left, right, kind, rep);
  }

#undef DECL_SINGLE_REP_BINOP_V
#undef DECL_MULTI_REP_BINOP

  V<Float> FloatUnary(V<Float> input, FloatUnaryOp::Kind kind,
                      FloatRepresentation rep) {
    return ReduceIfReachableFloatUnary(input, kind, rep);
  }
  V<Float64> Float64Unary(V<Float64> input, FloatUnaryOp::Kind kind) {
    return ReduceIfReachableFloatUnary(input, kind,
                                       FloatRepresentation::Float64());
  }

#define DECL_MULTI_REP_UNARY(name, operation, rep_type, kind)                \
  OpIndex name(OpIndex input, rep_type rep) {                                \
    return ReduceIfReachable##operation(input, operation##Op::Kind::k##kind, \
                                        rep);                                \
  }
#define DECL_MULTI_REP_UNARY_V(name, operation, rep_type, kind, tag)         \
  V<tag> name(V<tag> input, rep_type rep) {                                  \
    return ReduceIfReachable##operation(input, operation##Op::Kind::k##kind, \
                                        rep);                                \
  }
#define DECL_SINGLE_REP_UNARY_V(name, operation, kind, tag)         \
  V<tag> name(ConstOrV<tag> input) {                                \
    return ReduceIfReachable##operation(                            \
        resolve(input), operation##Op::Kind::k##kind, V<tag>::rep); \
  }

  DECL_MULTI_REP_UNARY_V(FloatAbs, FloatUnary, FloatRepresentation, Abs, Float)
  DECL_SINGLE_REP_UNARY_V(Float32Abs, FloatUnary, Abs, Float32)
  DECL_SINGLE_REP_UNARY_V(Float64Abs, FloatUnary, Abs, Float64)
  DECL_MULTI_REP_UNARY_V(FloatNegate, FloatUnary, FloatRepresentation, Negate,
                         Float)
  DECL_SINGLE_REP_UNARY_V(Float32Negate, FloatUnary, Negate, Float32)
  DECL_SINGLE_REP_UNARY_V(Float64Negate, FloatUnary, Negate, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64SilenceNaN, FloatUnary, SilenceNaN, Float64)
  DECL_MULTI_REP_UNARY_V(FloatRoundDown, FloatUnary, FloatRepresentation,
                         RoundDown, Float)
  DECL_SINGLE_REP_UNARY_V(Float32RoundDown, FloatUnary, RoundDown, Float32)
  DECL_SINGLE_REP_UNARY_V(Float64RoundDown, FloatUnary, RoundDown, Float64)
  DECL_MULTI_REP_UNARY_V(FloatRoundUp, FloatUnary, FloatRepresentation, RoundUp,
                         Float)
  DECL_SINGLE_REP_UNARY_V(Float32RoundUp, FloatUnary, RoundUp, Float32)
  DECL_SINGLE_REP_UNARY_V(Float64RoundUp, FloatUnary, RoundUp, Float64)
  DECL_MULTI_REP_UNARY_V(FloatRoundToZero, FloatUnary, FloatRepresentation,
                         RoundToZero, Float)
  DECL_SINGLE_REP_UNARY_V(Float32RoundToZero, FloatUnary, RoundToZero, Float32)
  DECL_SINGLE_REP_UNARY_V(Float64RoundToZero, FloatUnary, RoundToZero, Float64)
  DECL_MULTI_REP_UNARY_V(FloatRoundTiesEven, FloatUnary, FloatRepresentation,
                         RoundTiesEven, Float)
  DECL_SINGLE_REP_UNARY_V(Float32RoundTiesEven, FloatUnary, RoundTiesEven,
                          Float32)
  DECL_SINGLE_REP_UNARY_V(Float64RoundTiesEven, FloatUnary, RoundTiesEven,
                          Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Log, FloatUnary, Log, Float64)
  DECL_MULTI_REP_UNARY_V(FloatSqrt, FloatUnary, FloatRepresentation, Sqrt,
                         Float)
  DECL_SINGLE_REP_UNARY_V(Float32Sqrt, FloatUnary, Sqrt, Float32)
  DECL_SINGLE_REP_UNARY_V(Float64Sqrt, FloatUnary, Sqrt, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Exp, FloatUnary, Exp, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Expm1, FloatUnary, Expm1, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Sin, FloatUnary, Sin, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Cos, FloatUnary, Cos, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Sinh, FloatUnary, Sinh, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Cosh, FloatUnary, Cosh, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Asin, FloatUnary, Asin, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Acos, FloatUnary, Acos, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Asinh, FloatUnary, Asinh, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Acosh, FloatUnary, Acosh, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Tan, FloatUnary, Tan, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Tanh, FloatUnary, Tanh, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Log2, FloatUnary, Log2, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Log10, FloatUnary, Log10, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Log1p, FloatUnary, Log1p, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Atan, FloatUnary, Atan, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Atanh, FloatUnary, Atanh, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Cbrt, FloatUnary, Cbrt, Float64)

  DECL_MULTI_REP_UNARY_V(WordReverseBytes, WordUnary, WordRepresentation,
                         ReverseBytes, Word)
  DECL_SINGLE_REP_UNARY_V(Word32ReverseBytes, WordUnary, ReverseBytes, Word32)
  DECL_SINGLE_REP_UNARY_V(Word64ReverseBytes, WordUnary, ReverseBytes, Word64)
  DECL_MULTI_REP_UNARY_V(WordCountLeadingZeros, WordUnary, WordRepresentation,
                         CountLeadingZeros, Word)
  DECL_SINGLE_REP_UNARY_V(Word32CountLeadingZeros, WordUnary, CountLeadingZeros,
                          Word32)
  DECL_SINGLE_REP_UNARY_V(Word64CountLeadingZeros, WordUnary, CountLeadingZeros,
                          Word64)
  DECL_MULTI_REP_UNARY_V(WordCountTrailingZeros, WordUnary, WordRepresentation,
                         CountTrailingZeros, Word)
  DECL_SINGLE_REP_UNARY_V(Word32CountTrailingZeros, WordUnary,
                          CountTrailingZeros, Word32)
  DECL_SINGLE_REP_UNARY_V(Word64CountTrailingZeros, WordUnary,
                          CountTrailingZeros, Word64)
  DECL_MULTI_REP_UNARY_V(WordPopCount, WordUnary, WordRepresentation, PopCount,
                         Word)
  DECL_SINGLE_REP_UNARY_V(Word32PopCount, WordUnary, PopCount, Word32)
  DECL_SINGLE_REP_UNARY_V(Word64PopCount, WordUnary, PopCount, Word64)
  DECL_MULTI_REP_UNARY_V(WordSignExtend8, WordUnary, WordRepresentation,
                         SignExtend8, Word)
  DECL_SINGLE_REP_UNARY_V(Word32SignExtend8, WordUnary, SignExtend8, Word32)
  DECL_SINGLE_REP_UNARY_V(Word64SignExtend8, WordUnary, SignExtend8, Word64)
  DECL_MULTI_REP_UNARY_V(WordSignExtend16, WordUnary, WordRepresentation,
                         SignExtend16, Word)
  DECL_SINGLE_REP_UNARY_V(Word32SignExtend16, WordUnary, SignExtend16, Word32)
  DECL_SINGLE_REP_UNARY_V(Word64SignExtend16, WordUnary, SignExtend16, Word64)

  V<turboshaft::Tuple<Word, Word32>> OverflowCheckedUnary(
      V<Word> input, OverflowCheckedUnaryOp::Kind kind,
      WordRepresentation rep) {
    return ReduceIfReachableOverflowCheckedUnary(input, kind, rep);
  }

  DECL_MULTI_REP_UNARY_V(IntAbsCheckOverflow, OverflowCheckedUnary,
                         WordRepresentation, Abs, Word)
  DECL_SINGLE_REP_UNARY_V(Int32AbsCheckOverflow, OverflowCheckedUnary, Abs,
                          Word32)
  DECL_SINGLE_REP_UNARY_V(Int64AbsCheckOverflow, OverflowCheckedUnary, Abs,
                          Word64)

#undef DECL_SINGLE_REP_UNARY_V
#undef DECL_MULTI_REP_UNARY
#undef DECL_MULTI_REP_UNARY_V

  V<Word> WordBinopDeoptOnOverflow(V<Word> left, V<Word> right,
                                   V<turboshaft::FrameState> frame_state,
                                   WordBinopDeoptOnOverflowOp::Kind kind,
                                   WordRepresentation rep,
                                   FeedbackSource feedback,
                                   CheckForMinusZeroMode mode) {
    return ReduceIfReachableWordBinopDeoptOnOverflow(left, right, frame_state,
                                                     kind, rep, feedback, mode);
  }
#define DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(operation, rep_type)     \
  OpIndex rep_type##operation##DeoptOnOverflow(                       \
      ConstOrV<rep_type> left, ConstOrV<rep_type> right,              \
      V<turboshaft::FrameState> frame_state, FeedbackSource feedback, \
      CheckForMinusZeroMode mode =                                    \
          CheckForMinusZeroMode::kDontCheckForMinusZero) {            \
    return WordBinopDeoptOnOverflow(                                  \
        resolve(left), resolve(right), frame_state,                   \
        WordBinopDeoptOnOverflowOp::Kind::k##operation,               \
        WordRepresentation::rep_type(), feedback, mode);              \
  }

  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedAdd, Word32)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedAdd, Word64)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedAdd, WordPtr)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedSub, Word32)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedSub, Word64)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedSub, WordPtr)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedMul, Word32)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedMul, Word64)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedMul, WordPtr)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedDiv, Word32)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedDiv, Word64)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedDiv, WordPtr)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedMod, Word32)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedMod, Word64)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedMod, WordPtr)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(UnsignedDiv, Word32)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(UnsignedMod, Word32)
#undef DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW

  V<Float64> BitcastWord32PairToFloat64(ConstOrV<Word32> high_word32,
                                        ConstOrV<Word32> low_word32) {
    return ReduceIfReachableBitcastWord32PairToFloat64(resolve(high_word32),
                                                       resolve(low_word32));
  }

  OpIndex TaggedBitcast(OpIndex input, RegisterRepresentation from,
                        RegisterRepresentation to, TaggedBitcastOp::Kind kind) {
    return ReduceIfReachableTaggedBitcast(input, from, to, kind);
  }

#define DECL_TAGGED_BITCAST(FromT, ToT, kind)               \
  V<ToT> Bitcast##FromT##To##ToT(V<FromT> input) {          \
    return TaggedBitcast(input, V<FromT>::rep, V<ToT>::rep, \
                         TaggedBitcastOp::Kind::kind);      \
  }
  DECL_TAGGED_BITCAST(Smi, Word32, kSmi)
  DECL_TAGGED_BITCAST(Word32, Smi, kSmi)
  DECL_TAGGED_BITCAST(Smi, WordPtr, kSmi)
  DECL_TAGGED_BITCAST(WordPtr, Smi, kSmi)
  DECL_TAGGED_BITCAST(WordPtr, HeapObject, kHeapObject)
  DECL_TAGGED_BITCAST(HeapObject, WordPtr, kHeapObject)
#undef DECL_TAGGED_BITCAST
  V<Object> BitcastWordPtrToTagged(V<WordPtr> input) {
    return TaggedBitcast(input, V<WordPtr>::rep, V<Object>::rep,
                         TaggedBitcastOp::Kind::kAny);
  }

  V<WordPtr> BitcastTaggedToWordPtr(V<Object> input) {
    return TaggedBitcast(input, V<Object>::rep, V<WordPtr>::rep,
                         TaggedBitcastOp::Kind::kAny);
  }

  V<WordPtr> BitcastTaggedToWordPtrForTagAndSmiBits(V<Object> input) {
    return TaggedBitcast(input, RegisterRepresentation::Tagged(),
                         RegisterRepresentation::WordPtr(),
                         TaggedBitcastOp::Kind::kTagAndSmiBits);
  }

  V<Word32> ObjectIs(V<Object> input, ObjectIsOp::Kind kind,
                     ObjectIsOp::InputAssumptions input_assumptions) {
    return ReduceIfReachableObjectIs(input, kind, input_assumptions);
  }
#define DECL_OBJECT_IS(kind)                              \
  V<Word32> ObjectIs##kind(V<Object> object) {            \
    return ObjectIs(object, ObjectIsOp::Kind::k##kind,    \
                    ObjectIsOp::InputAssumptions::kNone); \
  }

  DECL_OBJECT_IS(ArrayBufferView)
  DECL_OBJECT_IS(BigInt)
  DECL_OBJECT_IS(BigInt64)
  DECL_OBJECT_IS(Callable)
  DECL_OBJECT_IS(Constructor)
  DECL_OBJECT_IS(DetectableCallable)
  DECL_OBJECT_IS(InternalizedString)
  DECL_OBJECT_IS(NonCallable)
  DECL_OBJECT_IS(Number)
  DECL_OBJECT_IS(NumberOrBigInt)
  DECL_OBJECT_IS(Receiver)
  DECL_OBJECT_IS(ReceiverOrNullOrUndefined)
  DECL_OBJECT_IS(Smi)
  DECL_OBJECT_IS(String)
  DECL_OBJECT_IS(StringOrStringWrapper)
  DECL_OBJECT_IS(Symbol)
  DECL_OBJECT_IS(Undetectable)
#undef DECL_OBJECT_IS

  V<Word32> Float64Is(V<Float64> input, NumericKind kind) {
    return ReduceIfReachableFloat64Is(input, kind);
  }
  V<Word32> Float64IsNaN(V<Float64> input) {
    return Float64Is(input, NumericKind::kNaN);
  }
  V<Word32> Float64IsHole(V<Float64> input) {
    return Float64Is(input, NumericKind::kFloat64Hole);
  }
  // Float64IsSmi returns true if {input} is an integer in smi range.
  V<Word32> Float64IsSmi(V<Float64> input) {
    return Float64Is(input, NumericKind::kSmi);
  }

  V<Word32> ObjectIsNumericValue(V<Object> input, NumericKind kind,
                                 FloatRepresentation input_rep) {
    return ReduceIfReachableObjectIsNumericValue(input, kind, input_rep);
  }

  V<Object> Convert(V<Object> input, ConvertOp::Kind from, ConvertOp::Kind to) {
    return ReduceIfReachableConvert(input, from, to);
  }
  V<Number> ConvertPlainPrimitiveToNumber(V<PlainPrimitive> input) {
    return V<Number>::Cast(Convert(input, ConvertOp::Kind::kPlainPrimitive,
                                   ConvertOp::Kind::kNumber));
  }
  V<Boolean> ConvertToBoolean(V<Object> input) {
    return V<Boolean>::Cast(
        Convert(input, ConvertOp::Kind::kObject, ConvertOp::Kind::kBoolean));
  }
  V<String> ConvertNumberToString(V<Number> input) {
    return V<String>::Cast(
        Convert(input, ConvertOp::Kind::kNumber, ConvertOp::Kind::kString));
  }
  V<Number> ConvertStringToNumber(V<String> input) {
    return V<Number>::Cast(
        Convert(input, ConvertOp::Kind::kString, ConvertOp::Kind::kNumber));
  }

  V<JSPrimitive> ConvertUntaggedToJSPrimitive(
      V<Untagged> input, ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind kind,
      RegisterRepresentation input_rep,
      ConvertUntaggedToJSPrimitiveOp::InputInterpretation input_interpretation,
      CheckForMinusZeroMode minus_zero_mode) {
    return ReduceIfReachableConvertUntaggedToJSPrimitive(
        input, kind, input_rep, input_interpretation, minus_zero_mode);
  }
#define CONVERT_PRIMITIVE_TO_OBJECT(name, kind, input_rep,               \
                                    input_interpretation)                \
  V<kind> name(V<input_rep> input) {                                     \
    return V<kind>::Cast(ConvertUntaggedToJSPrimitive(                   \
        input, ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::k##kind, \
        RegisterRepresentation::input_rep(),                             \
        ConvertUntaggedToJSPrimitiveOp::InputInterpretation::            \
            k##input_interpretation,                                     \
        CheckForMinusZeroMode::kDontCheckForMinusZero));                 \
  }
  CONVERT_PRIMITIVE_TO_OBJECT(ConvertInt32ToNumber, Number, Word32, Signed)
  CONVERT_PRIMITIVE_TO_OBJECT(ConvertUint32ToNumber, Number, Word32, Unsigned)
  CONVERT_PRIMITIVE_TO_OBJECT(ConvertWord32ToBoolean, Boolean, Word32, Signed)
  CONVERT_PRIMITIVE_TO_OBJECT(ConvertCharCodeToString, String, Word32, CharCode)
#undef CONVERT_PRIMITIVE_TO_OBJECT
  V<Number> ConvertFloat64ToNumber(V<Float64> input,
                                   CheckForMinusZeroMode minus_zero_mode) {
    return V<Number>::Cast(ConvertUntaggedToJSPrimitive(
        input, ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kNumber,
        RegisterRepresentation::Float64(),
        ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned,
        minus_zero_mode));
  }

  V<JSPrimitive> ConvertUntaggedToJSPrimitiveOrDeopt(
      V<Untagged> input, V<turboshaft::FrameState> frame_state,
      ConvertUntaggedToJSPrimitiveOrDeoptOp::JSPrimitiveKind kind,
      RegisterRepresentation input_rep,
      ConvertUntaggedToJSPrimitiveOrDeoptOp::InputInterpretation
          input_interpretation,
      const FeedbackSource& feedback) {
    return ReduceIfReachableConvertUntaggedToJSPrimitiveOrDeopt(
        input, frame_state, kind, input_rep, input_interpretation, feedback);
  }

  V<Untagged> ConvertJSPrimitiveToUntagged(
      V<JSPrimitive> primitive,
      ConvertJSPrimitiveToUntaggedOp::UntaggedKind kind,
      ConvertJSPrimitiveToUntaggedOp::InputAssumptions input_assumptions) {
    return ReduceIfReachableConvertJSPrimitiveToUntagged(primitive, kind,
                                                         input_assumptions);
  }

  V<Untagged> ConvertJSPrimitiveToUntaggedOrDeopt(
      V<Object> object, V<turboshaft::FrameState> frame_state,
      ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind from_kind,
      ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind to_kind,
      CheckForMinusZeroMode minus_zero_mode, const FeedbackSource& feedback) {
    return ReduceIfReachableConvertJSPrimitiveToUntaggedOrDeopt(
        object, frame_state, from_kind, to_kind, minus_zero_mode, feedback);
  }
  V<Word32> CheckedSmiUntag(V<Object> object,
                            V<turboshaft::FrameState> frame_state,
                            const FeedbackSource& feedback) {
    return V<Word32>::Cast(ConvertJSPrimitiveToUntaggedOrDeopt(
        object, frame_state,
        ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::kSmi,
        ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kInt32,
        CheckForMinusZeroMode::kDontCheckForMinusZero, feedback));
  }

  V<Word> TruncateJSPrimitiveToUntagged(
      V<JSPrimitive> object, TruncateJSPrimitiveToUntaggedOp::UntaggedKind kind,
      TruncateJSPrimitiveToUntaggedOp::InputAssumptions input_assumptions) {
    return ReduceIfReachableTruncateJSPrimitiveToUntagged(object, kind,
                                                          input_assumptions);
  }

  V<Word32> TruncateNumberToInt32(V<Number> value) {
    return V<Word32>::Cast(TruncateJSPrimitiveToUntagged(
        value, TruncateJSPrimitiveToUntaggedOp::UntaggedKind::kInt32,
        TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kNumberOrOddball));
  }

  V<Word> TruncateJSPrimitiveToUntaggedOrDeopt(
      V<JSPrimitive> object, V<turboshaft::FrameState> frame_state,
      TruncateJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind kind,
      TruncateJSPrimitiveToUntaggedOrDeoptOp::InputRequirement
          input_requirement,
      const FeedbackSource& feedback) {
    return ReduceIfReachableTruncateJSPrimitiveToUntaggedOrDeopt(
        object, frame_state, kind, input_requirement, feedback);
  }

  V<Object> ConvertJSPrimitiveToObject(V<JSPrimitive> value,
                                       V<Context> native_context,
                                       V<JSGlobalProxy> global_proxy,
                                       ConvertReceiverMode mode) {
    return ReduceIfReachableConvertJSPrimitiveToObject(value, native_context,
                                                       global_proxy, mode);
  }

  V<Word32> Word32Constant(uint32_t value) {
    return ReduceIfReachableConstant(ConstantOp::Kind::kWord32,
                                     uint64_t{value});
  }
  V<Word32> Word32Constant(int32_t value) {
    return Word32Constant(static_cast<uint32_t>(value));
  }
  V<Word64> Word64Constant(uint64_t value) {
    return ReduceIfReachableConstant(ConstantOp::Kind::kWord64, value);
  }
  V<Word64> Word64Constant(int64_t value) {
    return Word64Constant(static_cast<uint64_t>(value));
  }
  V<WordPtr> WordPtrConstant(uintptr_t value) {
    return V<WordPtr>::Cast(WordConstant(value, WordRepresentation::WordPtr()));
  }
  V<Word> WordConstant(uint64_t value, WordRepresentation rep) {
    switch (rep.value()) {
      case WordRepresentation::Word32():
        return Word32Constant(static_cast<uint32_t>(value));
      case WordRepresentation::Word64():
        return Word64Constant(value);
    }
  }
  V<WordPtr> IntPtrConstant(intptr_t value) {
    return UintPtrConstant(static_cast<uintptr_t>(value));
  }
  V<WordPtr> UintPtrConstant(uintptr_t value) { return WordPtrConstant(value); }
  V<Smi> SmiConstant(intptr_t value) {
    return SmiConstant(i::Tagged<Smi>(value));
  }
  V<Smi> SmiConstant(i::Tagged<Smi> value) {
    return V<Smi>::Cast(
        ReduceIfReachableConstant(ConstantOp::Kind::kSmi, value));
  }
  V<Float32> Float32Constant(i::Float32 value) {
    return ReduceIfReachableConstant(ConstantOp::Kind::kFloat32, value);
  }
  V<Float32> Float32Constant(float value) {
    // Passing the NaN Hole as input is allowed, but there is no guarantee that
    // it will remain a hole (it will remain NaN though).
    if (std::isnan(value)) {
      return Float32Constant(
          i::Float32::FromBits(base::bit_cast<uint32_t>(value)));
    } else {
      return Float32Constant(i::Float32(value));
    }
  }
  V<Float64> Float64Constant(i::Float64 value) {
    return ReduceIfReachableConstant(ConstantOp::Kind::kFloat64, value);
  }
  V<Float64> Float64Constant(double value) {
    // Passing the NaN Hole as input is allowed, but there is no guarantee that
    // it will remain a hole (it will remain NaN though).
    if (std::isnan(value)) {
      return Float64Constant(
          i::Float64::FromBits(base::bit_cast<uint64_t>(value)));
    } else {
      return Float64Constant(i::Float64(value));
    }
  }
  OpIndex FloatConstant(double value, FloatRepresentation rep) {
    // Passing the NaN Hole as input is allowed, but there is no guarantee that
    // it will remain a hole (it will remain NaN though).
    switch (rep.value()) {
      case FloatRepresentation::Float32():
        return Float32Constant(static_cast<float>(value));
      case FloatRepresentation::Float64():
        return Float64Constant(value);
    }
  }
  OpIndex NumberConstant(i::Float64 value) {
    return ReduceIfReachableConstant(ConstantOp::Kind::kNumber, value);
  }
  OpIndex NumberConstant(double value) {
    // Passing the NaN Hole as input is allowed, but there is no guarantee that
    // it will remain a hole (it will remain NaN though).
    if (std::isnan(value)) {
      return NumberConstant(
          i::Float64::FromBits(base::bit_cast<uint64_t>(value)));
    } else {
      return NumberConstant(i::Float64(value));
    }
  }
  OpIndex TaggedIndexConstant(int32_t value) {
    return ReduceIfReachableConstant(ConstantOp::Kind::kTaggedIndex,
                                     uint64_t{static_cast<uint32_t>(value)});
  }
  // TODO(nicohartmann): Maybe we should replace all uses of `HeapConstant` with
  // `HeapConstant[No|Maybe]?Hole` version.
  template <typename T,
            typename = std::enable_if_t<is_subtype_v<T, HeapObject>>>
  V<T> HeapConstant(Handle<T> value) {
    return ReduceIfReachableConstant(ConstantOp::Kind::kHeapObject,
                                     ConstantOp::Storage{value});
  }
  template <typename T,
            typename = std::enable_if_t<is_subtype_v<T, HeapObject>>>
  V<T> HeapConstantMaybeHole(Handle<T> value) {
    return __ HeapConstant(value);
  }
  template <typename T,
            typename = std::enable_if_t<is_subtype_v<T, HeapObject>>>
  V<T> HeapConstantNoHole(Handle<T> value) {
    CHECK(!IsAnyHole(*value));
    return __ HeapConstant(value);
  }
  V<HeapObject> HeapConstantHole(Handle<HeapObject> value) {
    DCHECK(IsAnyHole(*value));
    return __ HeapConstant(value);
  }
  V<Code> BuiltinCode(Builtin builtin, Isolate* isolate) {
    return HeapConstant(BuiltinCodeHandle(builtin, isolate));
  }
  OpIndex CompressedHeapConstant(Handle<HeapObject> value) {
    return ReduceIfReachableConstant(ConstantOp::Kind::kHeapObject, value);
  }
  OpIndex TrustedHeapConstant(Handle<HeapObject> value) {
    DCHECK(IsTrustedObject(*value));
    return ReduceIfReachableConstant(ConstantOp::Kind::kTrustedHeapObject,
                                     value);
  }
  OpIndex ExternalConstant(ExternalReference value) {
    return ReduceIfReachableConstant(ConstantOp::Kind::kExternal, value);
  }
  OpIndex IsolateField(IsolateFieldId id) {
    return ExternalConstant(ExternalReference::Create(id));
  }
  V<WordPtr> RelocatableConstant(int64_t value, RelocInfo::Mode mode) {
    DCHECK_EQ(mode, any_of(RelocInfo::WASM_CALL, RelocInfo::WASM_STUB_CALL));
    return ReduceIfReachableConstant(
        mode == RelocInfo::WASM_CALL
            ? ConstantOp::Kind::kRelocatableWasmCall
            : ConstantOp::Kind::kRelocatableWasmStubCall,
        static_cast<uint64_t>(value));
  }

  V<WordPtr> RelocatableWasmBuiltinCallTarget(Builtin builtin) {
    return RelocatableConstant(static_cast<int64_t>(builtin),
                               RelocInfo::WASM_STUB_CALL);
  }

  V<Word32> RelocatableWasmCanonicalSignatureId(int32_t canonical_id) {
    DCHECK_LE(0, canonical_id);
    return ReduceIfReachableConstant(
        ConstantOp::Kind::kRelocatableWasmCanonicalSignatureId,
        static_cast<uint64_t>(canonical_id));
  }

  V<Context> NoContextConstant() {
    return V<Context>::Cast(TagSmi(Context::kNoContext));
  }

  // TODO(nicohartmann@): Might want to get rid of the isolate when supporting
  // Wasm.
  V<Code> CEntryStubConstant(Isolate* isolate, int result_size,
                             ArgvMode argv_mode = ArgvMode::kStack,
                             bool builtin_exit_frame = false) {
    if (argv_mode != ArgvMode::kStack) {
      return HeapConstant(CodeFactory::CEntry(isolate, result_size, argv_mode,
                                              builtin_exit_frame));
    }

    DCHECK(result_size >= 1 && result_size <= 3);
    DCHECK_IMPLIES(builtin_exit_frame, result_size == 1);
    const int index = builtin_exit_frame ? 0 : result_size;
    if (cached_centry_stub_constants_[index].is_null()) {
      cached_centry_stub_constants_[index] = CodeFactory::CEntry(
          isolate, result_size, argv_mode, builtin_exit_frame);
    }
    return HeapConstant(cached_centry_stub_constants_[index].ToHandleChecked());
  }

#define DECL_CHANGE_V(name, kind, assumption, from, to)                  \
  V<to> name(ConstOrV<from> input) {                                     \
    return ReduceIfReachableChange(resolve(input), ChangeOp::Kind::kind, \
                                   ChangeOp::Assumption::assumption,     \
                                   V<from>::rep, V<to>::rep);            \
  }
#define DECL_TRY_CHANGE_V(name, kind, from, to)                       \
  V<turboshaft::Tuple<to, Word32>> name(V<from> input) {              \
    return ReduceIfReachableTryChange(input, TryChangeOp::Kind::kind, \
                                      V<from>::rep, V<to>::rep);      \
  }

  DECL_CHANGE_V(BitcastWord32ToWord64, kBitcast, kNoAssumption, Word32, Word64)
  DECL_CHANGE_V(BitcastFloat32ToWord32, kBitcast, kNoAssumption, Float32,
                Word32)
  DECL_CHANGE_V(BitcastWord32ToFloat32, kBitcast, kNoAssumption, Word32,
                Float32)
  DECL_CHANGE_V(BitcastFloat64ToWord64, kBitcast, kNoAssumption, Float64,
                Word64)
  DECL_CHANGE_V(BitcastWord64ToFloat64, kBitcast, kNoAssumption, Word64,
                Float64)
  DECL_CHANGE_V(ChangeUint32ToUint64, kZeroExtend, kNoAssumption, Word32,
                Word64)
  DECL_CHANGE_V(ChangeInt32ToInt64, kSignExtend, kNoAssumption, Word32, Word64)
  DECL_CHANGE_V(ChangeInt32ToFloat64, kSignedToFloat, kNoAssumption, Word32,
                Float64)
  DECL_CHANGE_V(ChangeInt64ToFloat64, kSignedToFloat, kNoAssumption, Word64,
                Float64)
  DECL_CHANGE_V(ChangeInt32ToFloat32, kSignedToFloat, kNoAssumption, Word32,
                Float32)
  DECL_CHANGE_V(ChangeInt64ToFloat32, kSignedToFloat, kNoAssumption, Word64,
                Float32)
  DECL_CHANGE_V(ChangeUint32ToFloat32, kUnsignedToFloat, kNoAssumption, Word32,
                Float32)
  DECL_CHANGE_V(ChangeUint64ToFloat32, kUnsignedToFloat, kNoAssumption, Word64,
                Float32)
  DECL_CHANGE_V(ReversibleInt64ToFloat64, kSignedToFloat, kReversible, Word64,
                Float64)
  DECL_CHANGE_V(ChangeUint64ToFloat64, kUnsignedToFloat, kNoAssumption, Word64,
                Float64)
  DECL_CHANGE_V(ReversibleUint64ToFloat64, kUnsignedToFloat, kReversible,
                Word64, Float64)
  DECL_CHANGE_V(ChangeUint32ToFloat64, kUnsignedToFloat, kNoAssumption, Word32,
                Float64)
  DECL_CHANGE_V(TruncateFloat64ToFloat32, kFloatConversion, kNoAssumption,
                Float64, Float32)
  DECL_CHANGE_V(ChangeFloat32ToFloat64, kFloatConversion, kNoAssumption,
                Float32, Float64)
  DECL_CHANGE_V(JSTruncateFloat64ToWord32, kJSFloatTruncate, kNoAssumption,
                Float64, Word32)
  DECL_CHANGE_V(TruncateWord64ToWord32, kTruncate, kNoAssumption, Word64,
                Word32)
  V<Word> ZeroExtendWord32ToRep(V<Word32> value, WordRepresentation rep) {
    if (rep == WordRepresentation::Word32()) return value;
    DCHECK_EQ(rep, WordRepresentation::Word64());
    return ChangeUint32ToUint64(value);
  }
  V<Word32> TruncateWordPtrToWord32(ConstOrV<WordPtr> input) {
    if constexpr (Is64()) {
      return TruncateWord64ToWord32(input);
    } else {
      DCHECK_EQ(WordPtr::bits, Word32::bits);
      return V<Word32>::Cast(resolve(input));
    }
  }
  V<WordPtr> ChangeInt32ToIntPtr(V<Word32> input) {
    if constexpr (Is64()) {
      return ChangeInt32ToInt64(input);
    } else {
      DCHECK_EQ(WordPtr::bits, Word32::bits);
      return V<WordPtr>::Cast(input);
    }
  }
  V<WordPtr> ChangeUint32ToUintPtr(V<Word32> input) {
    if constexpr (Is64()) {
      return ChangeUint32ToUint64(input);
    } else {
      DCHECK_EQ(WordPtr::bits, Word32::bits);
      return V<WordPtr>::Cast(input);
    }
  }

  V<Word64> ChangeIntPtrToInt64(V<WordPtr> input) {
    if constexpr (Is64()) {
      DCHECK_EQ(WordPtr::bits, Word64::bits);
      return V<Word64>::Cast(input);
    } else {
      return ChangeInt32ToInt64(input);
    }
  }

  V<Word64> ChangeUintPtrToUint64(V<WordPtr> input) {
    if constexpr (Is64()) {
      DCHECK_EQ(WordPtr::bits, Word64::bits);
      return V<Word64>::Cast(input);
    } else {
      return ChangeUint32ToUint64(input);
    }
  }

  V<Word32> IsSmi(V<Object> object) {
    if constexpr (COMPRESS_POINTERS_BOOL) {
      return Word32Equal(Word32BitwiseAnd(V<Word32>::Cast(object), kSmiTagMask),
                         kSmiTag);
    } else {
      return WordPtrEqual(
          WordPtrBitwiseAnd(V<WordPtr>::Cast(object), kSmiTagMask), kSmiTag);
    }
  }

#define DECL_SIGNED_FLOAT_TRUNCATE(FloatBits, ResultBits)                    \
  DECL_CHANGE_V(                                                             \
      TruncateFloat##FloatBits##ToInt##ResultBits##OverflowUndefined,        \
      kSignedFloatTruncateOverflowToMin, kNoOverflow, Float##FloatBits,      \
      Word##ResultBits)                                                      \
  DECL_TRY_CHANGE_V(TryTruncateFloat##FloatBits##ToInt##ResultBits,          \
                    kSignedFloatTruncateOverflowUndefined, Float##FloatBits, \
                    Word##ResultBits)

  DECL_SIGNED_FLOAT_TRUNCATE(64, 64)
  DECL_SIGNED_FLOAT_TRUNCATE(64, 32)
  DECL_SIGNED_FLOAT_TRUNCATE(32, 64)
  DECL_SIGNED_FLOAT_TRUNCATE(32, 32)
#undef DECL_SIGNED_FLOAT_TRUNCATE
  DECL_CHANGE_V(TruncateFloat64ToInt64OverflowToMin,
                kSignedFloatTruncateOverflowToMin, kNoAssumption, Float64,
                Word64)
  DECL_CHANGE_V(TruncateFloat32ToInt32OverflowToMin,
                kSignedFloatTruncateOverflowToMin, kNoAssumption, Float32,
                Word32)

#define DECL_UNSIGNED_FLOAT_TRUNCATE(FloatBits, ResultBits)                    \
  DECL_CHANGE_V(                                                               \
      TruncateFloat##FloatBits##ToUint##ResultBits##OverflowUndefined,         \
      kUnsignedFloatTruncateOverflowToMin, kNoOverflow, Float##FloatBits,      \
      Word##ResultBits)                                                        \
  DECL_CHANGE_V(TruncateFloat##FloatBits##ToUint##ResultBits##OverflowToMin,   \
                kUnsignedFloatTruncateOverflowToMin, kNoAssumption,            \
                Float##FloatBits, Word##ResultBits)                            \
  DECL_TRY_CHANGE_V(TryTruncateFloat##FloatBits##ToUint##ResultBits,           \
                    kUnsignedFloatTruncateOverflowUndefined, Float##FloatBits, \
                    Word##ResultBits)

  DECL_UNSIGNED_FLOAT_TRUNCATE(64, 64)
  DECL_UNSIGNED_FLOAT_TRUNCATE(64, 32)
  DECL_UNSIGNED_FLOAT_TRUNCATE(32, 64)
  DECL_UNSIGNED_FLOAT_TRUNCATE(32, 32)
#undef DECL_UNSIGNED_FLOAT_TRUNCATE

  DECL_CHANGE_V(ReversibleFloat64ToInt32, kSignedFloatTruncateOverflowToMin,
                kReversible, Float64, Word32)
  DECL_CHANGE_V(ReversibleFloat64ToUint32, kUnsignedFloatTruncateOverflowToMin,
                kReversible, Float64, Word32)
  DECL_CHANGE_V(ReversibleFloat64ToInt64, kSignedFloatTruncateOverflowToMin,
                kReversible, Float64, Word64)
  DECL_CHANGE_V(ReversibleFloat64ToUint64, kUnsignedFloatTruncateOverflowToMin,
                kReversible, Float64, Word64)
  DECL_CHANGE_V(Float64ExtractLowWord32, kExtractLowHalf, kNoAssumption,
                Float64, Word32)
  DECL_CHANGE_V(Float64ExtractHighWord32, kExtractHighHalf, kNoAssumption,
                Float64, Word32)
#undef DECL_CHANGE_V
#undef DECL_TRY_CHANGE_V

  V<Untagged> ChangeOrDeopt(V<Untagged> input,
                            V<turboshaft::FrameState> frame_state,
                            ChangeOrDeoptOp::Kind kind,
                            CheckForMinusZeroMode minus_zero_mode,
                            const FeedbackSource& feedback) {
    return ReduceIfReachableChangeOrDeopt(input, frame_state, kind,
                                          minus_zero_mode, feedback);
  }

  V<Word32> ChangeFloat64ToInt32OrDeopt(V<Float64> input,
                                        V<turboshaft::FrameState> frame_state,
                                        CheckForMinusZeroMode minus_zero_mode,
                                        const FeedbackSource& feedback) {
    return V<Word32>::Cast(ChangeOrDeopt(input, frame_state,
                                         ChangeOrDeoptOp::Kind::kFloat64ToInt32,
                                         minus_zero_mode, feedback));
  }
  V<Word32> ChangeFloat64ToUint32OrDeopt(V<Float64> input,
                                         V<turboshaft::FrameState> frame_state,
                                         CheckForMinusZeroMode minus_zero_mode,
                                         const FeedbackSource& feedback) {
    return V<Word32>::Cast(ChangeOrDeopt(
        input, frame_state, ChangeOrDeoptOp::Kind::kFloat64ToUint32,
        minus_zero_mode, feedback));
  }
  V<Word64> ChangeFloat64ToInt64OrDeopt(V<Float64> input,
                                        V<turboshaft::FrameState> frame_state,
                                        CheckForMinusZeroMode minus_zero_mode,
                                        const FeedbackSource& feedback) {
    return V<Word64>::Cast(ChangeOrDeopt(input, frame_state,
                                         ChangeOrDeoptOp::Kind::kFloat64ToInt64,
                                         minus_zero_mode, feedback));
  }

  V<Smi> TagSmi(ConstOrV<Word32> input) {
    constexpr int kSmiShiftBits = kSmiShiftSize + kSmiTagSize;
    // Do shift on 32bit values if Smis are stored in the lower word.
    if constexpr (Is64() && SmiValuesAre31Bits()) {
      V<Word32> shifted = Word32ShiftLeft(resolve(input), kSmiShiftBits);
      // In pointer compression, we smi-corrupt. Then, the upper bits are not
      // important.
      return BitcastWord32ToSmi(shifted);
    } else {
      return BitcastWordPtrToSmi(
          WordPtrShiftLeft(ChangeInt32ToIntPtr(resolve(input)), kSmiShiftBits));
    }
  }

  V<Word32> UntagSmi(V<Smi> input) {
    constexpr int kSmiShiftBits = kSmiShiftSize + kSmiTagSize;
    if constexpr (Is64() && SmiValuesAre31Bits()) {
      return Word32ShiftRightArithmeticShiftOutZeros(BitcastSmiToWord32(input),
                                                     kSmiShiftBits);
    }
    return TruncateWordPtrToWord32(WordPtrShiftRightArithmeticShiftOutZeros(
        BitcastSmiToWordPtr(input), kSmiShiftBits));
  }

  OpIndex AtomicRMW(V<WordPtr> base, V<WordPtr> index, OpIndex value,
                    AtomicRMWOp::BinOp bin_op,
                    RegisterRepresentation in_out_rep,
                    MemoryRepresentation memory_rep,
                    MemoryAccessKind memory_access_kind) {
    DCHECK_NE(bin_op, AtomicRMWOp::BinOp::kCompareExchange);
    return ReduceIfReachableAtomicRMW(base, index, value, OpIndex::Invalid(),
                                      bin_op, in_out_rep, memory_rep,
                                      memory_access_kind);
  }

  OpIndex AtomicCompareExchange(V<WordPtr> base, V<WordPtr> index,
                                OpIndex expected, OpIndex new_value,
                                RegisterRepresentation result_rep,
                                MemoryRepresentation input_rep,
                                MemoryAccessKind memory_access_kind) {
    return ReduceIfReachableAtomicRMW(
        base, index, new_value, expected, AtomicRMWOp::BinOp::kCompareExchange,
        result_rep, input_rep, memory_access_kind);
  }

  OpIndex AtomicWord32Pair(V<WordPtr> base, OptionalV<WordPtr> index,
                           OptionalV<Word32> value_low,
                           OptionalV<Word32> value_high,
                           OptionalV<Word32> expected_low,
                           OptionalV<Word32> expected_high,
                           AtomicWord32PairOp::Kind op_kind, int32_t offset) {
    return ReduceIfReachableAtomicWord32Pair(base, index, value_low, value_high,
                                             expected_low, expected_high,
                                             op_kind, offset);
  }

  OpIndex AtomicWord32PairLoad(V<WordPtr> base, OptionalV<WordPtr> index,
                               int32_t offset) {
    return AtomicWord32Pair(base, index, {}, {}, {}, {},
                            AtomicWord32PairOp::Kind::kLoad, offset);
  }
  OpIndex AtomicWord32PairStore(V<WordPtr> base, OptionalV<WordPtr> index,
                                V<Word32> value_low, V<Word32> value_high,
                                int32_t offset) {
    return AtomicWord32Pair(base, index, value_low, value_high, {}, {},
                            AtomicWord32PairOp::Kind::kStore, offset);
  }
  OpIndex AtomicWord32PairCompareExchange(
      V<WordPtr> base, OptionalV<WordPtr> index, V<Word32> value_low,
      V<Word32> value_high, V<Word32> expected_low, V<Word32> expected_high,
      int32_t offset = 0) {
    return AtomicWord32Pair(base, index, value_low, value_high, expected_low,
                            expected_high,
                            AtomicWord32PairOp::Kind::kCompareExchange, offset);
  }
  OpIndex AtomicWord32PairBinop(V<WordPtr> base, OptionalV<WordPtr> index,
                                V<Word32> value_low, V<Word32> value_high,
                                AtomicRMWOp::BinOp bin_op, int32_t offset = 0) {
    return AtomicWord32Pair(base, index, value_low, value_high, {}, {},
                            AtomicWord32PairOp::KindFromBinOp(bin_op), offset);
  }

  OpIndex MemoryBarrier(AtomicMemoryOrder memory_order) {
    return ReduceIfReachableMemoryBarrier(memory_order);
  }

  OpIndex Load(OpIndex base, OptionalOpIndex index, LoadOp::Kind kind,
               MemoryRepresentation loaded_rep,
               RegisterRepresentation result_rep, int32_t offset = 0,
               uint8_t element_size_log2 = 0) {
    return ReduceIfReachableLoad(base, index, kind, loaded_rep, result_rep,
                                 offset, element_size_log2);
  }

  OpIndex Load(OpIndex base, OptionalOpIndex index, LoadOp::Kind kind,
               MemoryRepresentation loaded_rep, int32_t offset = 0,
               uint8_t element_size_log2 = 0) {
    return Load(base, index, kind, loaded_rep,
                loaded_rep.ToRegisterRepresentation(), offset,
                element_size_log2);
  }
  OpIndex Load(OpIndex base, LoadOp::Kind kind, MemoryRepresentation loaded_rep,
               int32_t offset = 0) {
    return Load(base, OpIndex::Invalid(), kind, loaded_rep, offset);
  }
  OpIndex LoadOffHeap(OpIndex address, MemoryRepresentation rep) {
    return LoadOffHeap(address, 0, rep);
  }
  OpIndex LoadOffHeap(OpIndex address, int32_t offset,
                      MemoryRepresentation rep) {
    return Load(address, LoadOp::Kind::RawAligned(), rep, offset);
  }
  OpIndex LoadOffHeap(OpIndex address, OptionalOpIndex index, int32_t offset,
                      MemoryRepresentation rep) {
    return Load(address, index, LoadOp::Kind::RawAligned(), rep, offset,
                rep.SizeInBytesLog2());
  }

  // Load a protected (trusted -> trusted) pointer field. The read value is
  // either a Smi or a TrustedObject.
  V<Object> LoadProtectedPointerField(
      V<Object> base, OptionalV<WordPtr> index,
      LoadOp::Kind kind = LoadOp::Kind::TaggedBase(), int offset = 0,
      int element_size_log2 = kTaggedSizeLog2) {
    return Load(base, index, kind,
                V8_ENABLE_SANDBOX_BOOL
                    ? MemoryRepresentation::ProtectedPointer()
                    : MemoryRepresentation::AnyTagged(),
                offset, index.valid() ? element_size_log2 : 0);
  }

  // Load a protected (trusted -> trusted) pointer field. The read value is
  // either a Smi or a TrustedObject.
  V<Object> LoadProtectedPointerField(V<Object> base, LoadOp::Kind kind,
                                      int32_t offset) {
    return LoadProtectedPointerField(base, OpIndex::Invalid(), kind, offset);
  }

  // Load a trusted (indirect) pointer. Returns Smi or ExposedTrustedObject.
  V<Object> LoadTrustedPointerField(V<HeapObject> base, OptionalV<Word32> index,
                                    LoadOp::Kind kind, IndirectPointerTag tag,
                                    int offset = 0) {
#if V8_ENABLE_SANDBOX
    static_assert(COMPRESS_POINTERS_BOOL);
    V<Word32> handle =
        Load(base, index, kind, MemoryRepresentation::Uint32(), offset);
    V<Word32> table_index =
        Word32ShiftRightLogical(handle, kTrustedPointerHandleShift);
    V<Word64> table_offset = __ ChangeUint32ToUint64(
        Word32ShiftLeft(table_index, kTrustedPointerTableEntrySizeLog2));
    V<WordPtr> table =
        Load(LoadRootRegister(), LoadOp::Kind::RawAligned().Immutable(),
             MemoryRepresentation::UintPtr(),
             IsolateData::trusted_pointer_table_offset() +
                 Internals::kTrustedPointerTableBasePointerOffset);
    V<WordPtr> decoded_ptr =
        Load(table, table_offset, LoadOp::Kind::RawAligned(),
             MemoryRepresentation::UintPtr());

    // Untag the pointer and remove the marking bit in one operation.
    decoded_ptr =
        __ Word64BitwiseAnd(decoded_ptr, ~(tag | kTrustedPointerTableMarkBit));

    // Bitcast to tagged to this gets scanned by the GC properly.
    return BitcastWordPtrToTagged(decoded_ptr);
#else
    return Load(base, index, kind, MemoryRepresentation::TaggedPointer(),
                offset);
#endif  // V8_ENABLE_SANDBOX
  }

  // Load a trusted (indirect) pointer. Returns Smi or ExposedTrustedObject.
  V<Object> LoadTrustedPointerField(V<HeapObject> base, LoadOp::Kind kind,
                                    IndirectPointerTag tag, int offset = 0) {
    return LoadTrustedPointerField(base, OpIndex::Invalid(), kind, tag, offset);
  }

  V<Object> LoadFixedArrayElement(V<FixedArray> array, int index) {
    return Load(array, LoadOp::Kind::TaggedBase(),
                MemoryRepresentation::AnyTagged(),
                FixedArray::OffsetOfElementAt(index));
  }
  V<Object> LoadFixedArrayElement(V<FixedArray> array, V<WordPtr> index) {
    return Load(array, index, LoadOp::Kind::TaggedBase(),
                MemoryRepresentation::AnyTagged(),
                FixedArray::OffsetOfElementAt(0), kTaggedSizeLog2);
  }

  V<Float64> LoadFixedDoubleArrayElement(V<FixedDoubleArray> array, int index) {
    return Load(array, LoadOp::Kind::TaggedBase(),
                MemoryRepresentation::Float64(),
                FixedDoubleArray::OffsetOfElementAt(index));
  }
  V<Float64> LoadFixedDoubleArrayElement(V<FixedDoubleArray> array,
                                         V<WordPtr> index) {
    static_assert(ElementsKindToShiftSize(PACKED_DOUBLE_ELEMENTS) ==
                  ElementsKindToShiftSize(HOLEY_DOUBLE_ELEMENTS));
    return Load(array, index, LoadOp::Kind::TaggedBase(),
                MemoryRepresentation::Float64(),
                FixedDoubleArray::OffsetOfElementAt(0),
                ElementsKindToShiftSize(PACKED_DOUBLE_ELEMENTS));
  }

  V<Object> LoadProtectedFixedArrayElement(V<ProtectedFixedArray> array,
                                           V<WordPtr> index) {
    return LoadProtectedPointerField(array, index, LoadOp::Kind::TaggedBase(),
                                     ProtectedFixedArray::OffsetOfElementAt(0));
  }

  V<Object> LoadProtectedFixedArrayElement(V<ProtectedFixedArray> array,
                                           int index) {
    return LoadProtectedPointerField(
        array, LoadOp::Kind::TaggedBase(),
        ProtectedFixedArray::OffsetOfElementAt(index));
  }

  void Store(
      OpIndex base, OptionalOpIndex index, OpIndex value, StoreOp::Kind kind,
      MemoryRepresentation stored_rep, WriteBarrierKind write_barrier,
      int32_t offset = 0, uint8_t element_size_log2 = 0,
      bool maybe_initializing_or_transitioning = false,
      IndirectPointerTag maybe_indirect_pointer_tag = kIndirectPointerNullTag) {
    ReduceIfReachableStore(base, index, value, kind, stored_rep, write_barrier,
                           offset, element_size_log2,
                           maybe_initializing_or_transitioning,
                           maybe_indirect_pointer_tag);
  }
  void Store(
      OpIndex base, OpIndex value, StoreOp::Kind kind,
      MemoryRepresentation stored_rep, WriteBarrierKind write_barrier,
      int32_t offset = 0, bool maybe_initializing_or_transitioning = false,
      IndirectPointerTag maybe_indirect_pointer_tag = kIndirectPointerNullTag) {
    Store(base, OpIndex::Invalid(), value, kind, stored_rep, write_barrier,
          offset, 0, maybe_initializing_or_transitioning,
          maybe_indirect_pointer_tag);
  }

  template <typename T>
  void Initialize(Uninitialized<T>& object, OpIndex value,
                  MemoryRepresentation stored_rep,
                  WriteBarrierKind write_barrier, int32_t offset = 0) {
    return Store(object.object(), value,
                 StoreOp::Kind::Aligned(BaseTaggedness::kTaggedBase),
                 stored_rep, write_barrier, offset, true);
  }

  void StoreOffHeap(OpIndex address, OpIndex value, MemoryRepresentation rep,
                    int32_t offset = 0) {
    Store(address, value, StoreOp::Kind::RawAligned(), rep,
          WriteBarrierKind::kNoWriteBarrier, offset);
  }
  void StoreOffHeap(OpIndex address, OptionalOpIndex index, OpIndex value,
                    MemoryRepresentation rep, int32_t offset) {
    Store(address, index, value, StoreOp::Kind::RawAligned(), rep,
          WriteBarrierKind::kNoWriteBarrier, offset, rep.SizeInBytesLog2());
  }

  template <typename Rep = Any>
  V<Rep> LoadField(V<Object> object, const compiler::FieldAccess& access) {
    DCHECK_EQ(access.base_is_tagged, BaseTaggedness::kTaggedBase);
    return LoadFieldImpl<Rep>(object, access);
  }

  template <typename Rep = Any>
  V<Rep> LoadField(V<WordPtr> raw_base, const compiler::FieldAccess& access) {
    DCHECK_EQ(access.base_is_tagged, BaseTaggedness::kUntaggedBase);
    return LoadFieldImpl<Rep>(raw_base, access);
  }

  template <typename Obj, typename Class, typename T,
            typename = std::enable_if_t<v_traits<
                Class>::template implicitly_constructible_from<Obj>::value>>
  V<T> LoadField(V<Obj> object, const FieldAccessTS<Class, T>& field) {
    return LoadFieldImpl<T>(object, field);
  }

  template <typename Rep>
  V<Rep> LoadFieldImpl(OpIndex object, const compiler::FieldAccess& access) {
    MachineType machine_type = access.machine_type;
    if (machine_type.IsMapWord()) {
      machine_type = MachineType::TaggedPointer();
#ifdef V8_MAP_PACKING
      UNIMPLEMENTED();
#endif
    }
    MemoryRepresentation rep =
        MemoryRepresentation::FromMachineType(machine_type);
#ifdef V8_ENABLE_SANDBOX
    bool is_sandboxed_external =
        access.type.Is(compiler::Type::ExternalPointer());
    if (is_sandboxed_external) {
      // Fields for sandboxed external pointer contain a 32-bit handle, not a
      // 64-bit raw pointer.
      rep = MemoryRepresentation::Uint32();
    }
#endif  // V8_ENABLE_SANDBOX
    LoadOp::Kind kind = LoadOp::Kind::Aligned(access.base_is_tagged);
    if (access.is_immutable) {
      kind = kind.Immutable();
    }
    V<Rep> value = Load(object, kind, rep, access.offset);
#ifdef V8_ENABLE_SANDBOX
    if (is_sandboxed_external) {
      value = DecodeExternalPointer(value, access.external_pointer_tag);
    }
    if (access.is_bounded_size_access) {
      DCHECK(!is_sandboxed_external);
      value = ShiftRightLogical(value, kBoundedSizeShift,
                                WordRepresentation::WordPtr());
    }
#endif  // V8_ENABLE_SANDBOX
    return value;
  }

  // Helpers to read the most common fields.
  // TODO(nicohartmann@): Strengthen this to `V<HeapObject>`.
  V<Map> LoadMapField(V<Object> object) {
    return LoadField<Map>(object, AccessBuilder::ForMap());
  }

  V<Word32> LoadInstanceTypeField(V<Map> map) {
    return LoadField<Word32>(map, AccessBuilder::ForMapInstanceType());
  }

  V<Word32> HasInstanceType(V<Object> object, InstanceType instance_type) {
    return Word32Equal(LoadInstanceTypeField(LoadMapField(object)),
                       Word32Constant(instance_type));
  }

  V<Float64> LoadHeapNumberValue(V<HeapNumber> heap_number) {
    return __ template LoadField<HeapNumber, HeapNumber, Float64>(
        heap_number, AccessBuilderTS::ForHeapNumberValue());
  }

  template <typename Type = Object,
            typename = std::enable_if_t<is_subtype_v<Type, Object>>>
  V<Type> LoadTaggedField(V<Object> object, int field_offset) {
    return Load(object, LoadOp::Kind::TaggedBase(),
                MemoryRepresentation::AnyTagged(), field_offset);
  }

  template <typename Base>
  void StoreField(V<Base> object, const FieldAccess& access, V<Any> value) {
    StoreFieldImpl(object, access, value,
                   access.maybe_initializing_or_transitioning_store);
  }

  template <typename Object, typename Class, typename T>
  void InitializeField(Uninitialized<Object>& object,
                       const FieldAccessTS<Class, T>& access,
                       maybe_const_or_v_t<T> value) {
    static_assert(is_subtype_v<Object, Class>);
    StoreFieldImpl(object.object(), access, resolve(value), true);
  }

  // TODO(nicohartmann): Remove `InitializeField` once fully transitioned to
  // `FieldAccess`.
  template <typename T>
  void InitializeField(Uninitialized<T>& object, const FieldAccess& access,
                       V<Any> value) {
    StoreFieldImpl(object.object(), access, value, true);
  }

  template <typename Base>
  void StoreFieldImpl(V<Base> object, const FieldAccess& access, V<Any> value,
                      bool maybe_initializing_or_transitioning) {
    if constexpr (is_taggable_v<Base>) {
      DCHECK_EQ(access.base_is_tagged, BaseTaggedness::kTaggedBase);
    } else {
      static_assert(std::is_same_v<Base, WordPtr>);
      DCHECK_EQ(access.base_is_tagged, BaseTaggedness::kUntaggedBase);
    }
    // External pointer must never be stored by optimized code.
    DCHECK(!access.type.Is(compiler::Type::ExternalPointer()) ||
           !V8_ENABLE_SANDBOX_BOOL);
    // SandboxedPointers are not currently stored by optimized code.
    DCHECK(!access.type.Is(compiler::Type::SandboxedPointer()));

#ifdef V8_ENABLE_SANDBOX
    if (access.is_bounded_size_access) {
      value =
          ShiftLeft(value, kBoundedSizeShift, WordRepresentation::WordPtr());
    }
#endif  // V8_ENABLE_SANDBOX

    StoreOp::Kind kind = StoreOp::Kind::Aligned(access.base_is_tagged);
    MachineType machine_type = access.machine_type;
    if (machine_type.IsMapWord()) {
      machine_type = MachineType::TaggedPointer();
#ifdef V8_MAP_PACKING
      UNIMPLEMENTED();
#endif
    }
    MemoryRepresentation rep =
        MemoryRepresentation::FromMachineType(machine_type);
    Store(object, value, kind, rep, access.write_barrier_kind, access.offset,
          maybe_initializing_or_transitioning);
  }

  void StoreFixedArrayElement(V<FixedArray> array, int index, V<Object> value,
                              compiler::WriteBarrierKind write_barrier) {
    Store(array, value, LoadOp::Kind::TaggedBase(),
          MemoryRepresentation::AnyTagged(), write_barrier,
          FixedArray::kHeaderSize + index * kTaggedSize);
  }

  void StoreFixedArrayElement(V<FixedArray> array, V<WordPtr> index,
                              V<Object> value,
                              compiler::WriteBarrierKind write_barrier) {
    Store(array, index, value, LoadOp::Kind::TaggedBase(),
          MemoryRepresentation::AnyTagged(), write_barrier,
          FixedArray::kHeaderSize, kTaggedSizeLog2);
  }
  void StoreFixedDoubleArrayElement(V<FixedDoubleArray> array, V<WordPtr> index,
                                    V<Float64> value) {
    static_assert(ElementsKindToShiftSize(PACKED_DOUBLE_ELEMENTS) ==
                  ElementsKindToShiftSize(HOLEY_DOUBLE_ELEMENTS));
    Store(array, index, value, LoadOp::Kind::TaggedBase(),
          MemoryRepresentation::Float64(), WriteBarrierKind::kNoWriteBarrier,
          FixedDoubleArray::kHeaderSize,
          ElementsKindToShiftSize(PACKED_DOUBLE_ELEMENTS));
  }

  template <typename Class, typename T>
  V<T> LoadElement(V<Class> object, const ElementAccessTS<Class, T>& access,
                   V<WordPtr> index) {
    return LoadElement<T>(object, access, index, access.is_array_buffer_load);
  }

  // TODO(nicohartmann): Remove `LoadArrayBufferElement` once fully transitioned
  // to `ElementAccess`.
  template <typename T = Any, typename Base>
  V<T> LoadArrayBufferElement(V<Base> object, const ElementAccess& access,
                              V<WordPtr> index) {
    return LoadElement<T>(object, access, index, true);
  }
  // TODO(nicohartmann): Remove `LoadNonArrayBufferElement` once fully
  // transitioned to `ElementAccess`.
  template <typename T = Any, typename Base>
  V<T> LoadNonArrayBufferElement(V<Base> object, const ElementAccess& access,
                                 V<WordPtr> index) {
    return LoadElement<T>(object, access, index, false);
  }
  template <typename Base>
  V<WordPtr> GetElementStartPointer(V<Base> object,
                                    const ElementAccess& access) {
    return WordPtrAdd(BitcastHeapObjectToWordPtr(object),
                      access.header_size - access.tag());
  }

  template <typename Base>
  void StoreArrayBufferElement(V<Base> object, const ElementAccess& access,
                               V<WordPtr> index, V<Any> value) {
    return StoreElement(object, access, index, value, true);
  }
  template <typename Base>
  void StoreNonArrayBufferElement(V<Base> object, const ElementAccess& access,
                                  V<WordPtr> index, V<Any> value) {
    return StoreElement(object, access, index, value, false);
  }

  template <typename Class, typename T>
  void StoreElement(V<Class> object, const ElementAccessTS<Class, T>& access,
                    ConstOrV<WordPtr> index, V<T> value) {
    StoreElement(object, access, index, value, access.is_array_buffer_load);
  }

  template <typename Class, typename T>
  void InitializeElement(Uninitialized<Class>& object,
                         const ElementAccessTS<Class, T>& access,
                         ConstOrV<WordPtr> index, V<T> value) {
    StoreElement(object.object(), access, index, value,
                 access.is_array_buffer_load);
  }

  // TODO(nicohartmann): Remove `InitializeArrayBufferElement` once fully
  // transitioned to `ElementAccess`.
  template <typename Base>
  void InitializeArrayBufferElement(Uninitialized<Base>& object,
                                    const ElementAccess& access,
                                    V<WordPtr> index, V<Any> value) {
    StoreArrayBufferElement(object.object(), access, index, value);
  }
  // TODO(nicohartmann): Remove `InitializeNoneArrayBufferElement` once fully
  // transitioned to `ElementAccess`.
  template <typename Base>
  void InitializeNonArrayBufferElement(Uninitialized<Base>& object,
                                       const ElementAccess& access,
                                       V<WordPtr> index, V<Any> value) {
    StoreNonArrayBufferElement(object.object(), access, index, value);
  }

  V<Word32> ArrayBufferIsDetached(V<JSArrayBufferView> object) {
    V<HeapObject> buffer = __ template LoadField<HeapObject>(
        object, compiler::AccessBuilder::ForJSArrayBufferViewBuffer());
    V<Word32> bitfield = __ template LoadField<Word32>(
        buffer, compiler::AccessBuilder::ForJSArrayBufferBitField());
    return __ Word32BitwiseAnd(bitfield, JSArrayBuffer::WasDetachedBit::kMask);
  }

  template <typename T = HeapObject>
  Uninitialized<T> Allocate(ConstOrV<WordPtr> size, AllocationType type) {
    static_assert(is_subtype_v<T, HeapObject>);
    DCHECK(!in_object_initialization_);
    in_object_initialization_ = true;
    return Uninitialized<T>{ReduceIfReachableAllocate(resolve(size), type)};
  }

  template <typename T>
  V<T> FinishInitialization(Uninitialized<T>&& uninitialized) {
    DCHECK(in_object_initialization_);
    in_object_initialization_ = false;
    return uninitialized.ReleaseObject();
  }

  V<HeapNumber> AllocateHeapNumberWithValue(V<Float64> value,
                                            Factory* factory) {
    auto result = __ template Allocate<HeapNumber>(
        __ IntPtrConstant(sizeof(HeapNumber)), AllocationType::kYoung);
    __ InitializeField(result, AccessBuilder::ForMap(),
                       __ HeapConstant(factory->heap_number_map()));
    __ InitializeField(result, AccessBuilder::ForHeapNumberValue(), value);
    return __ FinishInitialization(std::move(result));
  }

  OpIndex DecodeExternalPointer(OpIndex handle, ExternalPointerTag tag) {
    return ReduceIfReachableDecodeExternalPointer(handle, tag);
  }

#if V8_ENABLE_WEBASSEMBLY
  void WasmStackCheck(WasmStackCheckOp::Kind kind) {
    ReduceIfReachableWasmStackCheck(kind);
  }
#endif

  void JSStackCheck(V<Context> context,
                    OptionalV<turboshaft::FrameState> frame_state,
                    JSStackCheckOp::Kind kind) {
    ReduceIfReachableJSStackCheck(context, frame_state, kind);
  }

  void JSLoopStackCheck(V<Context> context,
                        V<turboshaft::FrameState> frame_state) {
    JSStackCheck(context, frame_state, JSStackCheckOp::Kind::kLoop);
  }
  void JSFunctionEntryStackCheck(V<Context> context,
                                 V<turboshaft::FrameState> frame_state) {
    JSStackCheck(context, frame_state, JSStackCheckOp::Kind::kFunctionEntry);
  }

  void Retain(V<Object> value) { ReduceIfReachableRetain(value); }

  V<Word32> StackPointerGreaterThan(V<WordPtr> limit, StackCheckKind kind) {
    return ReduceIfReachableStackPointerGreaterThan(limit, kind);
  }

  V<Smi> StackCheckOffset() {
    return ReduceIfReachableFrameConstant(
        FrameConstantOp::Kind::kStackCheckOffset);
  }
  V<WordPtr> FramePointer() {
    return ReduceIfReachableFrameConstant(FrameConstantOp::Kind::kFramePointer);
  }
  V<WordPtr> ParentFramePointer() {
    return ReduceIfReachableFrameConstant(
        FrameConstantOp::Kind::kParentFramePointer);
  }

  V<WordPtr> StackSlot(int size, int alignment, bool is_tagged = false) {
    return ReduceIfReachableStackSlot(size, alignment, is_tagged);
  }

  V<WordPtr> AdaptLocalArgument(V<Object> argument) {
#ifdef V8_ENABLE_DIRECT_HANDLE
    // With direct locals, the argument can be passed directly.
    return BitcastTaggedToWordPtr(argument);
#else
    // With indirect locals, the argument has to be stored on the stack and the
    // slot address is passed.
    V<WordPtr> stack_slot =
        StackSlot(sizeof(uintptr_t), alignof(uintptr_t), true);
    StoreOffHeap(stack_slot, __ BitcastTaggedToWordPtr(argument),
                 MemoryRepresentation::UintPtr());
    return stack_slot;
#endif
  }

  OpIndex LoadRootRegister() { return ReduceIfReachableLoadRootRegister(); }

  template <typename T = Any, typename U = T>
  V<std::common_type_t<T, U>> Select(ConstOrV<Word32> cond, V<T> vtrue,
                                     V<U> vfalse, RegisterRepresentation rep,
                                     BranchHint hint,
                                     SelectOp::Implementation implem) {
    return ReduceIfReachableSelect(resolve(cond), vtrue, vfalse, rep, hint,
                                   implem);
  }

  // TODO(chromium:331100916): remove this overload once Turboshaft has been
  // entirely V<>ified.
  OpIndex Select(ConstOrV<Word32> cond, OpIndex vtrue, OpIndex vfalse,
                 RegisterRepresentation rep, BranchHint hint,
                 SelectOp::Implementation implem) {
    return Select(cond, V<Any>::Cast(vtrue), V<Any>::Cast(vfalse), rep, hint,
                  implem);
  }

#define DEF_SELECT(Rep)                                                  \
  V<Rep> Rep##Select(ConstOrV<Word32> cond, ConstOrV<Rep> vtrue,         \
                     ConstOrV<Rep> vfalse) {                             \
    return Select<Rep>(resolve(cond), resolve(vtrue), resolve(vfalse),   \
                       RegisterRepresentation::Rep(), BranchHint::kNone, \
                       SelectOp::Implementation::kCMove);                \
  }
  DEF_SELECT(Word32)
  DEF_SELECT(Word64)
  DEF_SELECT(WordPtr)
  DEF_SELECT(Float32)
  DEF_SELECT(Float64)
#undef DEF_SELECT

  template <typename T, typename U>
  V<std::common_type_t<T, U>> Conditional(ConstOrV<Word32> cond, V<T> vtrue,
                                          V<U> vfalse,
                                          BranchHint hint = BranchHint::kNone) {
    return Select(resolve(cond), vtrue, vfalse,
                  V<std::common_type_t<T, U>>::rep, hint,
                  SelectOp::Implementation::kBranch);
  }
  void Switch(V<Word32> input, base::Vector<SwitchOp::Case> cases,
              Block* default_case,
              BranchHint default_hint = BranchHint::kNone) {
    ReduceIfReachableSwitch(input, cases, default_case, default_hint);
  }
  void Unreachable() { ReduceIfReachableUnreachable(); }

  OpIndex Parameter(int index, RegisterRepresentation rep,
                    const char* debug_name = nullptr) {
    // Parameter indices might be negative.
    int cache_location = index - kMinParameterIndex;
    DCHECK_GE(cache_location, 0);
    if (static_cast<size_t>(cache_location) >= cached_parameters_.size()) {
      cached_parameters_.resize_and_init(cache_location + 1);
    }
    OpIndex& cached_param = cached_parameters_[cache_location];
    if (!cached_param.valid()) {
      // Note: When in unreachable code, this will return OpIndex::Invalid, so
      // the cached state is unchanged.
      cached_param = ReduceIfReachableParameter(index, rep, debug_name);
    } else {
      DCHECK_EQ(Asm().output_graph().Get(cached_param).outputs_rep(),
                base::VectorOf({rep}));
    }
    return cached_param;
  }
  template <typename T>
  V<T> Parameter(int index, const char* debug_name = nullptr) {
    return Parameter(index, V<T>::rep, debug_name);
  }
  V<Object> OsrValue(int index) { return ReduceIfReachableOsrValue(index); }
  void Return(V<Word32> pop_count, base::Vector<const OpIndex> return_values) {
    ReduceIfReachableReturn(pop_count, return_values);
  }
  void Return(OpIndex result) {
    Return(Word32Constant(0), base::VectorOf({result}));
  }

  template <typename R = AnyOrNone>
  V<R> Call(V<CallTarget> callee, OptionalV<turboshaft::FrameState> frame_state,
            base::Vector<const OpIndex> arguments,
            const TSCallDescriptor* descriptor,
            OpEffects effects = OpEffects().CanCallAnything()) {
    return ReduceIfReachableCall(callee, frame_state, arguments, descriptor,
                                 effects);
  }
  template <typename R = AnyOrNone>
  V<R> Call(V<CallTarget> callee, std::initializer_list<OpIndex> arguments,
            const TSCallDescriptor* descriptor,
            OpEffects effects = OpEffects().CanCallAnything()) {
    return Call<R>(callee, OptionalV<turboshaft::FrameState>::Nullopt(),
                   base::VectorOf(arguments), descriptor, effects);
  }

  template <typename Descriptor>
  std::enable_if_t<Descriptor::kNeedsFrameState && Descriptor::kNeedsContext,
                   detail::index_type_for_t<typename Descriptor::results_t>>
  CallBuiltin(Isolate* isolate, V<turboshaft::FrameState> frame_state,
              V<Context> context, const typename Descriptor::arguments_t& args,
              LazyDeoptOnThrow lazy_deopt_on_throw = LazyDeoptOnThrow::kNo) {
    using result_t = detail::index_type_for_t<typename Descriptor::results_t>;
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return result_t::Invalid();
    }
    DCHECK(frame_state.valid());
    DCHECK(context.valid());
    auto arguments = std::apply(
        [context](auto&&... as) {
          return base::SmallVector<
              OpIndex, std::tuple_size_v<typename Descriptor::arguments_t> + 1>{
              std::forward<decltype(as)>(as)..., context};
        },
        args);
    return result_t::Cast(CallBuiltinImpl(
        isolate, Descriptor::kFunction, frame_state, base::VectorOf(arguments),
        Descriptor::Create(StubCallMode::kCallCodeObject,
                           Asm().output_graph().graph_zone(),
                           lazy_deopt_on_throw),
        Descriptor::kEffects));
  }

  template <typename Descriptor>
  std::enable_if_t<!Descriptor::kNeedsFrameState && Descriptor::kNeedsContext,
                   detail::index_type_for_t<typename Descriptor::results_t>>
  CallBuiltin(Isolate* isolate, V<Context> context,
              const typename Descriptor::arguments_t& args) {
    using result_t = detail::index_type_for_t<typename Descriptor::results_t>;
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return result_t::Invalid();
    }
    DCHECK(context.valid());
    auto arguments = std::apply(
        [context](auto&&... as) {
          return base::SmallVector<
              OpIndex, std::tuple_size_v<typename Descriptor::arguments_t> + 1>{
              std::forward<decltype(as)>(as)..., context};
        },
        args);
    return result_t::Cast(CallBuiltinImpl(
        isolate, Descriptor::kFunction,
        OptionalV<turboshaft::FrameState>::Nullopt(), base::VectorOf(arguments),
        Descriptor::Create(StubCallMode::kCallCodeObject,
                           Asm().output_graph().graph_zone()),
        Descriptor::kEffects));
  }
  template <typename Descriptor>
  std::enable_if_t<Descriptor::kNeedsFrameState && !Descriptor::kNeedsContext,
                   detail::index_type_for_t<typename Descriptor::results_t>>
  CallBuiltin(Isolate* isolate, V<turboshaft::FrameState> frame_state,
              const typename Descriptor::arguments_t& args,
              LazyDeoptOnThrow lazy_deopt_on_throw = LazyDeoptOnThrow::kNo) {
    using result_t = detail::index_type_for_t<typename Descriptor::results_t>;
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return result_t::Invalid();
    }
    DCHECK(frame_state.valid());
    auto arguments = std::apply(
        [](auto&&... as) {
          return base::SmallVector<OpIndex, std::tuple_size_v<decltype(args)>>{
              std::forward<decltype(as)>(as)...};
        },
        args);
    return result_t::Cast(CallBuiltinImpl(
        isolate, Descriptor::kFunction, frame_state, base::VectorOf(arguments),
        Descriptor::Create(StubCallMode::kCallCodeObject,
                           Asm().output_graph().graph_zone(),
                           lazy_deopt_on_throw),
        Descriptor::kEffects));
  }
  template <typename Descriptor>
  std::enable_if_t<!Descriptor::kNeedsFrameState && !Descriptor::kNeedsContext,
                   detail::index_type_for_t<typename Descriptor::results_t>>
  CallBuiltin(Isolate* isolate, const typename Descriptor::arguments_t& args) {
    using result_t = detail::index_type_for_t<typename Descriptor::results_t>;
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return result_t::Invalid();
    }
    auto arguments = std::apply(
        [](auto&&... as) {
          return base::SmallVector<
              OpIndex, std::tuple_size_v<typename Descriptor::arguments_t>>{
              std::forward<decltype(as)>(as)...};
        },
        args);
    return result_t::Cast(CallBuiltinImpl(
        isolate, Descriptor::kFunction,
        OptionalV<turboshaft::FrameState>::Nullopt(), base::VectorOf(arguments),
        Descriptor::Create(StubCallMode::kCallCodeObject,
                           Asm().output_graph().graph_zone()),
        Descriptor::kEffects));
  }

#if V8_ENABLE_WEBASSEMBLY

  template <typename Descriptor>
  std::enable_if_t<!Descriptor::kNeedsContext,
                   detail::index_type_for_t<typename Descriptor::results_t>>
  WasmCallBuiltinThroughJumptable(
      const typename Descriptor::arguments_t& args) {
    static_assert(!Descriptor::kNeedsFrameState);
    using result_t = detail::index_type_for_t<typename Descriptor::results_t>;
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return result_t::Invalid();
    }
    auto arguments = std::apply(
        [](auto&&... as) {
          return base::SmallVector<
              OpIndex, std::tuple_size_v<typename Descriptor::arguments_t>>{
              std::forward<decltype(as)>(as)...};
        },
        args);
    V<WordPtr> call_target =
        RelocatableWasmBuiltinCallTarget(Descriptor::kFunction);
    return result_t::Cast(
        Call(call_target, OptionalV<turboshaft::FrameState>::Nullopt(),
             base::VectorOf(arguments),
             Descriptor::Create(StubCallMode::kCallWasmRuntimeStub,
                                Asm().output_graph().graph_zone()),
             Descriptor::kEffects));
  }

  template <typename Descriptor>
  std::enable_if_t<Descriptor::kNeedsContext,
                   detail::index_type_for_t<typename Descriptor::results_t>>
  WasmCallBuiltinThroughJumptable(
      V<Context> context, const typename Descriptor::arguments_t& args) {
    static_assert(!Descriptor::kNeedsFrameState);
    using result_t = detail::index_type_for_t<typename Descriptor::results_t>;
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return result_t::Invalid();
    }
    DCHECK(context.valid());
    auto arguments = std::apply(
        [context](auto&&... as) {
          return base::SmallVector<
              OpIndex, std::tuple_size_v<typename Descriptor::arguments_t> + 1>{
              std::forward<decltype(as)>(as)..., context};
        },
        args);
    V<WordPtr> call_target =
        RelocatableWasmBuiltinCallTarget(Descriptor::kFunction);
    return result_t::Cast(
        Call(call_target, OptionalV<turboshaft::FrameState>::Nullopt(),
             base::VectorOf(arguments),
             Descriptor::Create(StubCallMode::kCallWasmRuntimeStub,
                                Asm().output_graph().graph_zone()),
             Descriptor::kEffects));
  }

#endif  // V8_ENABLE_WEBASSEMBLY

  V<Any> CallBuiltinImpl(Isolate* isolate, Builtin builtin,
                         OptionalV<turboshaft::FrameState> frame_state,
                         base::Vector<const OpIndex> arguments,
                         const TSCallDescriptor* desc, OpEffects effects) {
    Callable callable = Builtins::CallableFor(isolate, builtin);
    return Call(HeapConstant(callable.code()), frame_state, arguments, desc,
                effects);
  }

#define DECL_GENERIC_BINOP_BUILTIN_CALL(Name)                            \
  V<Object> CallBuiltin_##Name(                                          \
      Isolate* isolate, V<turboshaft::FrameState> frame_state,           \
      V<Context> context, V<Object> lhs, V<Object> rhs,                  \
      LazyDeoptOnThrow lazy_deopt_on_throw) {                            \
    return CallBuiltin<typename BuiltinCallDescriptor::Name>(            \
        isolate, frame_state, context, {lhs, rhs}, lazy_deopt_on_throw); \
  }
  GENERIC_BINOP_LIST(DECL_GENERIC_BINOP_BUILTIN_CALL)
#undef DECL_GENERIC_BINOP_BUILTIN_CALL

#define DECL_GENERIC_UNOP_BUILTIN_CALL(Name)                           \
  V<Object> CallBuiltin_##Name(Isolate* isolate,                       \
                               V<turboshaft::FrameState> frame_state,  \
                               V<Context> context, V<Object> input,    \
                               LazyDeoptOnThrow lazy_deopt_on_throw) { \
    return CallBuiltin<typename BuiltinCallDescriptor::Name>(          \
        isolate, frame_state, context, {input}, lazy_deopt_on_throw);  \
  }
  GENERIC_UNOP_LIST(DECL_GENERIC_UNOP_BUILTIN_CALL)
#undef DECL_GENERIC_UNOP_BUILTIN_CALL

  V<Number> CallBuiltin_ToNumber(Isolate* isolate,
                                 V<turboshaft::FrameState> frame_state,
                                 V<Context> context, V<Object> input,
                                 LazyDeoptOnThrow lazy_deopt_on_throw) {
    return CallBuiltin<typename BuiltinCallDescriptor::ToNumber>(
        isolate, frame_state, context, {input}, lazy_deopt_on_throw);
  }
  V<Numeric> CallBuiltin_ToNumeric(Isolate* isolate,
                                   V<turboshaft::FrameState> frame_state,
                                   V<Context> context, V<Object> input,
                                   LazyDeoptOnThrow lazy_deopt_on_throw) {
    return CallBuiltin<typename BuiltinCallDescriptor::ToNumeric>(
        isolate, frame_state, context, {input}, lazy_deopt_on_throw);
  }

  void CallBuiltin_CheckTurbofanType(Isolate* isolate, V<Context> context,
                                     V<Object> object,
                                     V<TurbofanType> allocated_type,
                                     V<Smi> node_id) {
    CallBuiltin<typename BuiltinCallDescriptor::CheckTurbofanType>(
        isolate, context, {object, allocated_type, node_id});
  }
  V<Object> CallBuiltin_CopyFastSmiOrObjectElements(Isolate* isolate,
                                                    V<Object> object) {
    return CallBuiltin<
        typename BuiltinCallDescriptor::CopyFastSmiOrObjectElements>(isolate,
                                                                     {object});
  }
  void CallBuiltin_DebugPrintFloat64(Isolate* isolate, V<Context> context,
                                     V<Float64> value) {
    CallBuiltin<typename BuiltinCallDescriptor::DebugPrintFloat64>(
        isolate, context, {value});
  }
  void CallBuiltin_DebugPrintWordPtr(Isolate* isolate, V<Context> context,
                                     V<WordPtr> value) {
    CallBuiltin<typename BuiltinCallDescriptor::DebugPrintWordPtr>(
        isolate, context, {value});
  }
  V<Smi> CallBuiltin_FindOrderedHashMapEntry(Isolate* isolate,
                                             V<Context> context,
                                             V<Object> table, V<Smi> key) {
    return CallBuiltin<typename BuiltinCallDescriptor::FindOrderedHashMapEntry>(
        isolate, context, {table, key});
  }
  V<Smi> CallBuiltin_FindOrderedHashSetEntry(Isolate* isolate,
                                             V<Context> context, V<Object> set,
                                             V<Smi> key) {
    return CallBuiltin<typename BuiltinCallDescriptor::FindOrderedHashSetEntry>(
        isolate, context, {set, key});
  }
  V<Object> CallBuiltin_GrowFastDoubleElements(Isolate* isolate,
                                               V<Object> object, V<Smi> size) {
    return CallBuiltin<typename BuiltinCallDescriptor::GrowFastDoubleElements>(
        isolate, {object, size});
  }
  V<Object> CallBuiltin_GrowFastSmiOrObjectElements(Isolate* isolate,
                                                    V<Object> object,
                                                    V<Smi> size) {
    return CallBuiltin<
        typename BuiltinCallDescriptor::GrowFastSmiOrObjectElements>(
        isolate, {object, size});
  }
  V<FixedArray> CallBuiltin_NewSloppyArgumentsElements(
      Isolate* isolate, V<WordPtr> frame, V<WordPtr> formal_parameter_count,
      V<Smi> arguments_count) {
    return CallBuiltin<
        typename BuiltinCallDescriptor::NewSloppyArgumentsElements>(
        isolate, {frame, formal_parameter_count, arguments_count});
  }
  V<FixedArray> CallBuiltin_NewStrictArgumentsElements(
      Isolate* isolate, V<WordPtr> frame, V<WordPtr> formal_parameter_count,
      V<Smi> arguments_count) {
    return CallBuiltin<
        typename BuiltinCallDescriptor::NewStrictArgumentsElements>(
        isolate, {frame, formal_parameter_count, arguments_count});
  }
  V<FixedArray> CallBuiltin_NewRestArgumentsElements(
      Isolate* isolate, V<WordPtr> frame, V<WordPtr> formal_parameter_count,
      V<Smi> arguments_count) {
    return CallBuiltin<
        typename BuiltinCallDescriptor::NewRestArgumentsElements>(
        isolate, {frame, formal_parameter_count, arguments_count});
  }
  V<String> CallBuiltin_NumberToString(Isolate* isolate, V<Number> input) {
    return CallBuiltin<typename BuiltinCallDescriptor::NumberToString>(isolate,
                                                                       {input});
  }
  V<String> CallBuiltin_ToString(Isolate* isolate,
                                 V<turboshaft::FrameState> frame_state,
                                 V<Context> context, V<Object> input,
                                 LazyDeoptOnThrow lazy_deopt_on_throw) {
    return CallBuiltin<typename BuiltinCallDescriptor::ToString>(
        isolate, frame_state, context, {input}, lazy_deopt_on_throw);
  }
  V<Number> CallBuiltin_PlainPrimitiveToNumber(Isolate* isolate,
                                               V<PlainPrimitive> input) {
    return CallBuiltin<typename BuiltinCallDescriptor::PlainPrimitiveToNumber>(
        isolate, {input});
  }
  V<Boolean> CallBuiltin_SameValue(Isolate* isolate, V<Object> left,
                                   V<Object> right) {
    return CallBuiltin<typename BuiltinCallDescriptor::SameValue>(
        isolate, {left, right});
  }
  V<Boolean> CallBuiltin_SameValueNumbersOnly(Isolate* isolate, V<Object> left,
                                              V<Object> right) {
    return CallBuiltin<typename BuiltinCallDescriptor::SameValueNumbersOnly>(
        isolate, {left, right});
  }
  V<String> CallBuiltin_StringAdd_CheckNone(Isolate* isolate,
                                            V<Context> context, V<String> left,
                                            V<String> right) {
    return CallBuiltin<typename BuiltinCallDescriptor::StringAdd_CheckNone>(
        isolate, context, {left, right});
  }
  V<Boolean> CallBuiltin_StringEqual(Isolate* isolate, V<String> left,
                                     V<String> right, V<WordPtr> length) {
    return CallBuiltin<typename BuiltinCallDescriptor::StringEqual>(
        isolate, {left, right, length});
  }
  V<Boolean> CallBuiltin_StringLessThan(Isolate* isolate, V<String> left,
                                        V<String> right) {
    return CallBuiltin<typename BuiltinCallDescriptor::StringLessThan>(
        isolate, {left, right});
  }
  V<Boolean> CallBuiltin_StringLessThanOrEqual(Isolate* isolate, V<String> left,
                                               V<String> right) {
    return CallBuiltin<typename BuiltinCallDescriptor::StringLessThanOrEqual>(
        isolate, {left, right});
  }
  V<Smi> CallBuiltin_StringIndexOf(Isolate* isolate, V<String> string,
                                   V<String> search, V<Smi> position) {
    return CallBuiltin<typename BuiltinCallDescriptor::StringIndexOf>(
        isolate, {string, search, position});
  }
  V<String> CallBuiltin_StringFromCodePointAt(Isolate* isolate,
                                              V<String> string,
                                              V<WordPtr> index) {
    return CallBuiltin<typename BuiltinCallDescriptor::StringFromCodePointAt>(
        isolate, {string, index});
  }
#ifdef V8_INTL_SUPPORT
  V<String> CallBuiltin_StringToLowerCaseIntl(Isolate* isolate,
                                              V<Context> context,
                                              V<String> string) {
    return CallBuiltin<typename BuiltinCallDescriptor::StringToLowerCaseIntl>(
        isolate, context, {string});
  }
#endif  // V8_INTL_SUPPORT
  V<Number> CallBuiltin_StringToNumber(Isolate* isolate, V<String> input) {
    return CallBuiltin<typename BuiltinCallDescriptor::StringToNumber>(isolate,
                                                                       {input});
  }
  V<String> CallBuiltin_StringSubstring(Isolate* isolate, V<String> string,
                                        V<WordPtr> start, V<WordPtr> end) {
    return CallBuiltin<typename BuiltinCallDescriptor::StringSubstring>(
        isolate, {string, start, end});
  }
  V<Boolean> CallBuiltin_ToBoolean(Isolate* isolate, V<Object> object) {
    return CallBuiltin<typename BuiltinCallDescriptor::ToBoolean>(isolate,
                                                                  {object});
  }
  V<JSReceiver> CallBuiltin_ToObject(Isolate* isolate, V<Context> context,
                                     V<JSPrimitive> object) {
    return CallBuiltin<typename BuiltinCallDescriptor::ToObject>(
        isolate, context, {object});
  }
  V<Context> CallBuiltin_FastNewFunctionContextFunction(
      Isolate* isolate, OpIndex frame_state, V<Context> context,
      V<ScopeInfo> scope_info, ConstOrV<Word32> slot_count,
      LazyDeoptOnThrow lazy_deopt_on_throw) {
    return CallBuiltin<
        typename BuiltinCallDescriptor::FastNewFunctionContextFunction>(
        isolate, frame_state, context, {scope_info, resolve(slot_count)},
        lazy_deopt_on_throw);
  }
  V<Context> CallBuiltin_FastNewFunctionContextEval(
      Isolate* isolate, OpIndex frame_state, V<Context> context,
      V<ScopeInfo> scope_info, ConstOrV<Word32> slot_count,
      LazyDeoptOnThrow lazy_deopt_on_throw) {
    return CallBuiltin<
        typename BuiltinCallDescriptor::FastNewFunctionContextEval>(
        isolate, frame_state, context, {scope_info, resolve(slot_count)},
        lazy_deopt_on_throw);
  }
  V<JSFunction> CallBuiltin_FastNewClosure(
      Isolate* isolate, V<turboshaft::FrameState> frame_state,
      V<Context> context, V<SharedFunctionInfo> shared_function_info,
      V<FeedbackCell> feedback_cell) {
    return CallBuiltin<typename BuiltinCallDescriptor::FastNewClosure>(
        isolate, frame_state, context, {shared_function_info, feedback_cell});
  }
  V<String> CallBuiltin_Typeof(Isolate* isolate, V<Object> object) {
    return CallBuiltin<typename BuiltinCallDescriptor::Typeof>(isolate,
                                                               {object});
  }

  V<Object> CallBuiltinWithVarStackArgs(Isolate* isolate, Zone* graph_zone,
                                        Builtin builtin,
                                        V<turboshaft::FrameState> frame_state,
                                        int num_stack_args,
                                        base::Vector<OpIndex> arguments,
                                        LazyDeoptOnThrow lazy_deopt_on_throw) {
    Callable callable = Builtins::CallableFor(isolate, builtin);
    const CallInterfaceDescriptor& descriptor = callable.descriptor();
    CallDescriptor* call_descriptor =
        Linkage::GetStubCallDescriptor(graph_zone, descriptor, num_stack_args,
                                       CallDescriptor::kNeedsFrameState);
    V<Code> stub_code = __ HeapConstant(callable.code());

    return Call<Object>(
        stub_code, frame_state, arguments,
        TSCallDescriptor::Create(call_descriptor, CanThrow::kYes,
                                 lazy_deopt_on_throw, graph_zone));
  }

  V<Object> CallBuiltin_CallWithSpread(Isolate* isolate, Zone* graph_zone,
                                       V<turboshaft::FrameState> frame_state,
                                       V<Context> context, V<Object> function,
                                       int num_args_no_spread, V<Object> spread,
                                       base::Vector<V<Object>> args_no_spread,
                                       LazyDeoptOnThrow lazy_deopt_on_throw) {
    base::SmallVector<OpIndex, 16> arguments;
    arguments.push_back(function);
    arguments.push_back(Word32Constant(num_args_no_spread));
    arguments.push_back(spread);
    arguments.insert(arguments.end(), args_no_spread.begin(),
                     args_no_spread.end());

    arguments.push_back(context);

    return CallBuiltinWithVarStackArgs(
        isolate, graph_zone, Builtin::kCallWithSpread, frame_state,
        num_args_no_spread, base::VectorOf(arguments), lazy_deopt_on_throw);
  }
  V<Object> CallBuiltin_CallWithArrayLike(
      Isolate* isolate, Zone* graph_zone, V<turboshaft::FrameState> frame_state,
      V<Context> context, V<Object> receiver, V<Object> function,
      V<Object> arguments_list, LazyDeoptOnThrow lazy_deopt_on_throw) {
    // CallWithArrayLike is a weird builtin that expects a receiver as top of
    // the stack, but doesn't explicitly list it as an extra argument. We thus
    // manually create the call descriptor with 1 stack argument.
    constexpr int kNumberOfStackArguments = 1;

    OpIndex arguments[] = {function, arguments_list, receiver, context};

    return CallBuiltinWithVarStackArgs(
        isolate, graph_zone, Builtin::kCallWithArrayLike, frame_state,
        kNumberOfStackArguments, base::VectorOf(arguments),
        lazy_deopt_on_throw);
  }
  V<Object> CallBuiltin_CallForwardVarargs(
      Isolate* isolate, Zone* graph_zone, Builtin builtin,
      V<turboshaft::FrameState> frame_state, V<Context> context,
      V<JSFunction> function, int num_args, int start_index,
      base::Vector<V<Object>> args, LazyDeoptOnThrow lazy_deopt_on_throw) {
    DCHECK(builtin == Builtin::kCallFunctionForwardVarargs ||
           builtin == Builtin::kCallForwardVarargs);
    base::SmallVector<OpIndex, 16> arguments;
    arguments.push_back(function);
    arguments.push_back(__ Word32Constant(num_args));
    arguments.push_back(__ Word32Constant(start_index));
    arguments.insert(arguments.end(), args.begin(), args.end());
    arguments.push_back(context);

    return CallBuiltinWithVarStackArgs(
        isolate, graph_zone, builtin, frame_state, num_args,
        base::VectorOf(arguments), lazy_deopt_on_throw);
  }

  template <typename Descriptor>
  std::enable_if_t<Descriptor::kNeedsFrameState, typename Descriptor::result_t>
  CallRuntime(Isolate* isolate, V<turboshaft::FrameState> frame_state,
              V<Context> context, LazyDeoptOnThrow lazy_deopt_on_throw,
              const typename Descriptor::arguments_t& args) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return OpIndex::Invalid();
    }
    DCHECK(frame_state.valid());
    DCHECK(context.valid());
    return CallRuntimeImpl<typename Descriptor::result_t>(
        isolate, Descriptor::kFunction,
        Descriptor::Create(Asm().output_graph().graph_zone(),
                           lazy_deopt_on_throw),
        frame_state, context, args);
  }
  template <typename Descriptor>
  std::enable_if_t<!Descriptor::kNeedsFrameState, typename Descriptor::result_t>
  CallRuntime(Isolate* isolate, V<Context> context,
              const typename Descriptor::arguments_t& args) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return OpIndex::Invalid();
    }
    DCHECK(context.valid());
    return CallRuntimeImpl<typename Descriptor::result_t>(
        isolate, Descriptor::kFunction,
        Descriptor::Create(Asm().output_graph().graph_zone(),
                           LazyDeoptOnThrow::kNo),
        {}, context, args);
  }

  template <typename Ret, typename Args>
  Ret CallRuntimeImpl(Isolate* isolate, Runtime::FunctionId function,
                      const TSCallDescriptor* desc,
                      V<turboshaft::FrameState> frame_state, V<Context> context,
                      const Args& args) {
    const int result_size = Runtime::FunctionForId(function)->result_size;
    constexpr size_t kMaxNumArgs = 6;
    const size_t argc = std::tuple_size_v<Args>;
    static_assert(kMaxNumArgs >= argc);
    // Convert arguments from `args` tuple into a `SmallVector<OpIndex>`.
    using vector_t = base::SmallVector<OpIndex, argc + 4>;
    auto inputs = std::apply(
        [](auto&&... as) {
          return vector_t{std::forward<decltype(as)>(as)...};
        },
        args);
    DCHECK(context.valid());
    inputs.push_back(ExternalConstant(ExternalReference::Create(function)));
    inputs.push_back(Word32Constant(static_cast<int>(argc)));
    inputs.push_back(context);

    if constexpr (std::is_same_v<Ret, void>) {
      Call(CEntryStubConstant(isolate, result_size), frame_state,
           base::VectorOf(inputs), desc);
    } else {
      return Ret::Cast(Call(CEntryStubConstant(isolate, result_size),
                            frame_state, base::VectorOf(inputs), desc));
    }
  }

  void CallRuntime_Abort(Isolate* isolate, V<Context> context, V<Smi> reason) {
    CallRuntime<typename RuntimeCallDescriptor::Abort>(isolate, context,
                                                       {reason});
  }
  V<Number> CallRuntime_DateCurrentTime(Isolate* isolate, V<Context> context) {
    return CallRuntime<typename RuntimeCallDescriptor::DateCurrentTime>(
        isolate, context, {});
  }
  void CallRuntime_DebugPrint(Isolate* isolate, V<Object> object) {
    CallRuntime<typename RuntimeCallDescriptor::DebugPrint>(
        isolate, NoContextConstant(), {object});
  }
  V<Object> CallRuntime_HandleNoHeapWritesInterrupts(
      Isolate* isolate, V<turboshaft::FrameState> frame_state,
      V<Context> context) {
    return CallRuntime<
        typename RuntimeCallDescriptor::HandleNoHeapWritesInterrupts>(
        isolate, frame_state, context, LazyDeoptOnThrow::kNo, {});
  }
  V<Object> CallRuntime_StackGuard(Isolate* isolate, V<Context> context) {
    return CallRuntime<typename RuntimeCallDescriptor::StackGuard>(isolate,
                                                                   context, {});
  }
  V<Object> CallRuntime_StackGuardWithGap(Isolate* isolate,
                                          V<turboshaft::FrameState> frame_state,
                                          V<Context> context, V<Smi> gap) {
    return CallRuntime<typename RuntimeCallDescriptor::StackGuardWithGap>(
        isolate, frame_state, context, LazyDeoptOnThrow::kNo, {gap});
  }
  V<Object> CallRuntime_StringCharCodeAt(Isolate* isolate, V<Context> context,
                                         V<String> string, V<Number> index) {
    return CallRuntime<typename RuntimeCallDescriptor::StringCharCodeAt>(
        isolate, context, {string, index});
  }
#ifdef V8_INTL_SUPPORT
  V<String> CallRuntime_StringToUpperCaseIntl(Isolate* isolate,
                                              V<Context> context,
                                              V<String> string) {
    return CallRuntime<typename RuntimeCallDescriptor::StringToUpperCaseIntl>(
        isolate, context, {string});
  }
#endif  // V8_INTL_SUPPORT
  V<String> CallRuntime_SymbolDescriptiveString(
      Isolate* isolate, V<turboshaft::FrameState> frame_state,
      V<Context> context, V<Symbol> symbol,
      LazyDeoptOnThrow lazy_deopt_on_throw) {
    return CallRuntime<typename RuntimeCallDescriptor::SymbolDescriptiveString>(
        isolate, frame_state, context, lazy_deopt_on_throw, {symbol});
  }
  V<Object> CallRuntime_TerminateExecution(
      Isolate* isolate, V<turboshaft::FrameState> frame_state,
      V<Context> context) {
    return CallRuntime<typename RuntimeCallDescriptor::TerminateExecution>(
        isolate, frame_state, context, LazyDeoptOnThrow::kNo, {});
  }
  V<Object> CallRuntime_TransitionElementsKind(Isolate* isolate,
                                               V<Context> context,
                                               V<HeapObject> object,
                                               V<Map> target_map) {
    return CallRuntime<typename RuntimeCallDescriptor::TransitionElementsKind>(
        isolate, context, {object, target_map});
  }
  V<Object> CallRuntime_TryMigrateInstance(Isolate* isolate, V<Context> context,
                                           V<HeapObject> heap_object) {
    return CallRuntime<typename RuntimeCallDescriptor::TryMigrateInstance>(
        isolate, context, {heap_object});
  }
  void CallRuntime_ThrowAccessedUninitializedVariable(
      Isolate* isolate, V<turboshaft::FrameState> frame_state,
      V<Context> context, LazyDeoptOnThrow lazy_deopt_on_throw,
      V<Object> object) {
    CallRuntime<
        typename RuntimeCallDescriptor::ThrowAccessedUninitializedVariable>(
        isolate, frame_state, context, lazy_deopt_on_throw, {object});
  }
  void CallRuntime_ThrowConstructorReturnedNonObject(
      Isolate* isolate, V<turboshaft::FrameState> frame_state,
      V<Context> context, LazyDeoptOnThrow lazy_deopt_on_throw) {
    CallRuntime<
        typename RuntimeCallDescriptor::ThrowConstructorReturnedNonObject>(
        isolate, frame_state, context, lazy_deopt_on_throw, {});
  }
  void CallRuntime_ThrowNotSuperConstructor(
      Isolate* isolate, V<turboshaft::FrameState> frame_state,
      V<Context> context, LazyDeoptOnThrow lazy_deopt_on_throw,
      V<Object> constructor, V<Object> function) {
    CallRuntime<typename RuntimeCallDescriptor::ThrowNotSuperConstructor>(
        isolate, frame_state, context, lazy_deopt_on_throw,
        {constructor, function});
  }
  void CallRuntime_ThrowSuperAlreadyCalledError(
      Isolate* isolate, V<turboshaft::FrameState> frame_state,
      V<Context> context, LazyDeoptOnThrow lazy_deopt_on_throw) {
    CallRuntime<typename RuntimeCallDescriptor::ThrowSuperAlreadyCalledError>(
        isolate, frame_state, context, lazy_deopt_on_throw, {});
  }
  void CallRuntime_ThrowSuperNotCalled(Isolate* isolate,
                                       V<turboshaft::FrameState> frame_state,
                                       V<Context> context,
                                       LazyDeoptOnThrow lazy_deopt_on_throw) {
    CallRuntime<typename RuntimeCallDescriptor::ThrowSuperNotCalled>(
        isolate, frame_state, context, lazy_deopt_on_throw, {});
  }
  void CallRuntime_ThrowCalledNonCallable(Isolate* isolate,
                                          V<turboshaft::FrameState> frame_state,
                                          V<Context> context,
                                          LazyDeoptOnThrow lazy_deopt_on_throw,
                                          V<Object> value) {
    CallRuntime<typename RuntimeCallDescriptor::ThrowCalledNonCallable>(
        isolate, frame_state, context, lazy_deopt_on_throw, {value});
  }
  void CallRuntime_ThrowInvalidStringLength(
      Isolate* isolate, V<turboshaft::FrameState> frame_state,
      V<Context> context, LazyDeoptOnThrow lazy_deopt_on_throw) {
    CallRuntime<typename RuntimeCallDescriptor::ThrowInvalidStringLength>(
        isolate, frame_state, context, lazy_deopt_on_throw, {});
  }
  V<JSFunction> CallRuntime_NewClosure(
      Isolate* isolate, V<Context> context,
      V<SharedFunctionInfo> shared_function_info,
      V<FeedbackCell> feedback_cell) {
    return CallRuntime<typename RuntimeCallDescriptor::NewClosure>(
        isolate, context, {shared_function_info, feedback_cell});
  }
  V<JSFunction> CallRuntime_NewClosure_Tenured(
      Isolate* isolate, V<Context> context,
      V<SharedFunctionInfo> shared_function_info,
      V<FeedbackCell> feedback_cell) {
    return CallRuntime<typename RuntimeCallDescriptor::NewClosure_Tenured>(
        isolate, context, {shared_function_info, feedback_cell});
  }
  V<Boolean> CallRuntime_HasInPrototypeChain(
      Isolate* isolate, V<turboshaft::FrameState> frame_state,
      V<Context> context, LazyDeoptOnThrow lazy_deopt_on_throw,
      V<Object> object, V<HeapObject> prototype) {
    return CallRuntime<typename RuntimeCallDescriptor::HasInPrototypeChain>(
        isolate, frame_state, context, lazy_deopt_on_throw,
        {object, prototype});
  }

  void TailCall(OpIndex callee, base::Vector<const OpIndex> arguments,
                const TSCallDescriptor* descriptor) {
    ReduceIfReachableTailCall(callee, arguments, descriptor);
  }

  V<turboshaft::FrameState> FrameState(base::Vector<const OpIndex> inputs,
                                       bool inlined,
                                       const FrameStateData* data) {
    return ReduceIfReachableFrameState(inputs, inlined, data);
  }
  void DeoptimizeIf(V<Word32> condition, V<turboshaft::FrameState> frame_state,
                    const DeoptimizeParameters* parameters) {
    ReduceIfReachableDeoptimizeIf(condition, frame_state, false, parameters);
  }
  void DeoptimizeIfNot(V<Word32> condition,
                       V<turboshaft::FrameState> frame_state,
                       const DeoptimizeParameters* parameters) {
    ReduceIfReachableDeoptimizeIf(condition, frame_state, true, parameters);
  }
  void DeoptimizeIf(V<Word32> condition, V<turboshaft::FrameState> frame_state,
                    DeoptimizeReason reason, const FeedbackSource& feedback) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return;
    }
    Zone* zone = Asm().output_graph().graph_zone();
    const DeoptimizeParameters* params =
        zone->New<DeoptimizeParameters>(reason, feedback);
    DeoptimizeIf(condition, frame_state, params);
  }
  void DeoptimizeIfNot(V<Word32> condition,
                       V<turboshaft::FrameState> frame_state,
                       DeoptimizeReason reason,
                       const FeedbackSource& feedback) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return;
    }
    Zone* zone = Asm().output_graph().graph_zone();
    const DeoptimizeParameters* params =
        zone->New<DeoptimizeParameters>(reason, feedback);
    DeoptimizeIfNot(condition, frame_state, params);
  }
  void Deoptimize(V<turboshaft::FrameState> frame_state,
                  const DeoptimizeParameters* parameters) {
    ReduceIfReachableDeoptimize(frame_state, parameters);
  }
  void Deoptimize(V<turboshaft::FrameState> frame_state,
                  DeoptimizeReason reason, const FeedbackSource& feedback) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return;
    }
    Zone* zone = Asm().output_graph().graph_zone();
    const DeoptimizeParameters* params =
        zone->New<DeoptimizeParameters>(reason, feedback);
    Deoptimize(frame_state, params);
  }

#if V8_ENABLE_WEBASSEMBLY
  // TrapIf and TrapIfNot in Wasm code do not pass a frame state.
  void TrapIf(V<Word32> condition, TrapId trap_id) {
    ReduceIfReachableTrapIf(condition, OptionalV<turboshaft::FrameState>{},
                            false, trap_id);
  }
  void TrapIfNot(V<Word32> condition, TrapId trap_id) {
    ReduceIfReachableTrapIf(condition, OptionalV<turboshaft::FrameState>{},
                            true, trap_id);
  }

  // TrapIf and TrapIfNot from Wasm inlined into JS pass a frame state.
  void TrapIf(V<Word32> condition,
              OptionalV<turboshaft::FrameState> frame_state, TrapId trap_id) {
    ReduceIfReachableTrapIf(condition, frame_state, false, trap_id);
  }
  void TrapIfNot(V<Word32> condition,
                 OptionalV<turboshaft::FrameState> frame_state,
                 TrapId trap_id) {
    ReduceIfReachableTrapIf(condition, frame_state, true, trap_id);
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  void StaticAssert(V<Word32> condition, const char* source) {
    ReduceIfReachableStaticAssert(condition, source);
  }

  OpIndex Phi(base::Vector<const OpIndex> inputs, RegisterRepresentation rep) {
    return ReduceIfReachablePhi(inputs, rep);
  }
  OpIndex Phi(std::initializer_list<OpIndex> inputs,
              RegisterRepresentation rep) {
    return Phi(base::VectorOf(inputs), rep);
  }
  template <typename T>
  V<T> Phi(const base::Vector<V<T>>& inputs) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return OpIndex::Invalid();
    }
    // Downcast from typed `V<T>` wrapper to `OpIndex`.
    OpIndex* inputs_begin = inputs.data();
    static_assert(sizeof(OpIndex) == sizeof(V<T>));
    return Phi(base::VectorOf(inputs_begin, inputs.length()), V<T>::rep);
  }
  OpIndex PendingLoopPhi(OpIndex first, RegisterRepresentation rep) {
    return ReduceIfReachablePendingLoopPhi(first, rep);
  }
  template <typename T>
  V<T> PendingLoopPhi(V<T> first) {
    return PendingLoopPhi(first, V<T>::rep);
  }

  V<Any> Tuple(base::Vector<const V<Any>> indices) {
    return ReduceIfReachableTuple(indices);
  }
  V<Any> Tuple(std::initializer_list<V<Any>> indices) {
    return ReduceIfReachableTuple(base::VectorOf(indices));
  }
  template <typename... Ts>
  V<turboshaft::Tuple<Ts...>> Tuple(V<Ts>... indices) {
    std::initializer_list<V<Any>> inputs{V<Any>::Cast(indices)...};
    return V<turboshaft::Tuple<Ts...>>::Cast(Tuple(base::VectorOf(inputs)));
  }
  // TODO(chromium:331100916): Remove this overload once everything is properly
  // V<>ified.
  V<turboshaft::Tuple<Any, Any>> Tuple(OpIndex left, OpIndex right) {
    return V<turboshaft::Tuple<Any, Any>>::Cast(
        Tuple(base::VectorOf({V<Any>::Cast(left), V<Any>::Cast(right)})));
  }

  V<Any> Projection(V<Any> tuple, uint16_t index, RegisterRepresentation rep) {
    return ReduceIfReachableProjection(tuple, index, rep);
  }
  template <uint16_t Index, typename... Ts>
  auto Projection(V<turboshaft::Tuple<Ts...>> tuple) {
    using element_t = base::nth_type_t<Index, Ts...>;
    static_assert(v_traits<element_t>::rep != nullrep,
                  "Representation for Projection cannot be inferred. Use "
                  "overload with explicit Representation argument.");
    return V<element_t>::Cast(Projection(tuple, Index, V<element_t>::rep));
  }
  template <uint16_t Index, typename... Ts>
  auto Projection(V<turboshaft::Tuple<Ts...>> tuple,
                  RegisterRepresentation rep) {
    using element_t = base::nth_type_t<Index, Ts...>;
    DCHECK(V<element_t>::allows_representation(rep));
    return V<element_t>::Cast(Projection(tuple, Index, rep));
  }
  OpIndex CheckTurboshaftTypeOf(OpIndex input, RegisterRepresentation rep,
                                Type expected_type, bool successful) {
    CHECK(v8_flags.turboshaft_enable_debug_features);
    return ReduceIfReachableCheckTurboshaftTypeOf(input, rep, expected_type,
                                                  successful);
  }

  // This is currently only usable during graph building on the main thread.
  void Dcheck(V<Word32> condition, const char* message, const char* file,
              int line, const SourceLocation& loc = SourceLocation::Current()) {
    Isolate* isolate = Asm().data()->isolate();
    USE(isolate);
    DCHECK_NOT_NULL(isolate);
    DCHECK_EQ(ThreadId::Current(), isolate->thread_id());
#ifdef DEBUG
    if (v8_flags.debug_code) {
      Check(condition, message, file, line, loc);
    }
#endif
  }

  // This is currently only usable during graph building on the main thread.
  void Check(V<Word32> condition, const char* message, const char* file,
             int line, const SourceLocation& loc = SourceLocation::Current()) {
    Isolate* isolate = Asm().data()->isolate();
    USE(isolate);
    DCHECK_NOT_NULL(isolate);
    DCHECK_EQ(ThreadId::Current(), isolate->thread_id());

    if (message != nullptr) {
      CodeComment({"[ Assert: ", loc}, message);
    } else {
      CodeComment({"[ Assert: ", loc});
    }

    IF_NOT (LIKELY(condition)) {
      std::vector<FileAndLine> file_and_line;
      if (file != nullptr) {
        file_and_line.push_back({file, line});
      }
      FailAssert(message, file_and_line, loc);
    }
    CodeComment({"] Assert", SourceLocation()});
  }

  void FailAssert(const char* message,
                  const std::vector<FileAndLine>& files_and_lines,
                  const SourceLocation& loc) {
    std::stringstream stream;
    if (message) stream << message;
    for (auto it = files_and_lines.rbegin(); it != files_and_lines.rend();
         ++it) {
      if (it->first != nullptr) {
        stream << " [" << it->first << ":" << it->second << "]";
#ifndef DEBUG
        // To limit the size of these strings in release builds, we include only
        // the innermost macro's file name and line number.
        break;
#endif
      }
    }

    Isolate* isolate = Asm().data()->isolate();
    DCHECK_NOT_NULL(isolate);
    DCHECK_EQ(ThreadId::Current(), isolate->thread_id());
    V<String> string_constant =
        __ HeapConstantNoHole(isolate->factory()->NewStringFromAsciiChecked(
            stream.str().c_str(), AllocationType::kOld));

    AbortCSADcheck(string_constant);
    Unreachable();
  }

  void AbortCSADcheck(V<String> message) {
    ReduceIfReachableAbortCSADcheck(message);
  }

  // CatchBlockBegin should always be the 1st operation of a catch handler, and
  // returns the value of the exception that was caught. Because of split-edge
  // form, catch handlers cannot have multiple predecessors (since their
  // predecessors always end with CheckException, which has 2 successors). As
  // such, when multiple CheckException go to the same catch handler,
  // Assembler::AddPredecessor and Assembler::SplitEdge take care of introducing
  // additional intermediate catch handlers, which are then wired to the
  // original catch handler. When calling `__ CatchBlockBegin` at the begining
  // of the original catch handler, a Phi of the CatchBlockBegin of the
  // predecessors is emitted instead. Here is an example:
  //
  // Initial graph:
  //
  //                   + B1 ----------------+
  //                   | ...                |
  //                   | 1: CallOp(...)     |
  //                   | 2: CheckException  |
  //                   +--------------------+
  //                     /              \
  //                    /                \
  //                   /                  \
  //     + B2 ----------------+        + B3 ----------------+
  //     | 3: DidntThrow(1)   |        | 4: CatchBlockBegin |
  //     |  ...               |        | 5: SomeOp(4)       |
  //     |  ...               |        | ...                |
  //     +--------------------+        +--------------------+
  //                   \                  /
  //                    \                /
  //                     \              /
  //                   + B4 ----------------+
  //                   | 6: Phi(3, 4)       |
  //                   |  ...               |
  //                   +--------------------+
  //
  //
  // Let's say that we lower the CallOp to 2 throwing calls. We'll thus get:
  //
  //
  //                             + B1 ----------------+
  //                             | ...                |
  //                             | 1: CallOp(...)     |
  //                             | 2: CheckException  |
  //                             +--------------------+
  //                               /              \
  //                              /                \
  //                             /                  \
  //               + B2 ----------------+        + B4 ----------------+
  //               | 3: DidntThrow(1)   |        | 7: CatchBlockBegin |
  //               | 4: CallOp(...)     |        | 8: Goto(B6)        |
  //               | 5: CheckException  |        +--------------------+
  //               +--------------------+                        \
  //                   /              \                           \
  //                  /                \                           \
  //                 /                  \                           \
  //     + B3 ----------------+        + B5 ----------------+       |
  //     | 6: DidntThrow(4)   |        | 9: CatchBlockBegin |       |
  //     |  ...               |        | 10: Goto(B6)       |       |
  //     |  ...               |        +--------------------+       |
  //     +--------------------+                   \                 |
  //                    \                          \                |
  //                     \                          \               |
  //                      \                      + B6 ----------------+
  //                       \                     | 11: Phi(7, 9)      |
  //                        \                    | 12: SomeOp(11)     |
  //                         \                   | ...                |
  //                          \                  +--------------------+
  //                           \                     /
  //                            \                   /
  //                             \                 /
  //                           + B7 ----------------+
  //                           | 6: Phi(6, 11)      |
  //                           |  ...               |
  //                           +--------------------+
  //
  // Note B6 in the output graph corresponds to B3 in the input graph and that
  // `11: Phi(7, 9)` was emitted when calling `CatchBlockBegin` in order to map
  // `4: CatchBlockBegin` from the input graph.
  //
  // Besides AddPredecessor and SplitEdge in Assembler, most of the machinery to
  // make this work is in GenericReducerBase (in particular,
  // `REDUCE(CatchBlockBegin)`, `REDUCE(Call)`, `REDUCE(CheckException)` and
  // `CatchIfInCatchScope`).
  V<Object> CatchBlockBegin() { return ReduceIfReachableCatchBlockBegin(); }

  void Goto(Block* destination) {
    bool is_backedge = destination->IsBound();
    Goto(destination, is_backedge);
  }
  void Goto(Block* destination, bool is_backedge) {
    ReduceIfReachableGoto(destination, is_backedge);
  }
  void Branch(V<Word32> condition, Block* if_true, Block* if_false,
              BranchHint hint = BranchHint::kNone) {
    ReduceIfReachableBranch(condition, if_true, if_false, hint);
  }
  void Branch(ConditionWithHint condition, Block* if_true, Block* if_false) {
    return Branch(condition.condition(), if_true, if_false, condition.hint());
  }

  // Return `true` if the control flow after the conditional jump is reachable.
  ConditionalGotoStatus GotoIf(V<Word32> condition, Block* if_true,
                               BranchHint hint = BranchHint::kNone) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      // What we return here should not matter.
      return ConditionalGotoStatus::kBranch;
    }
    Block* if_false = Asm().NewBlock();
    return BranchAndBind(condition, if_true, if_false, hint, if_false);
  }
  ConditionalGotoStatus GotoIf(ConditionWithHint condition, Block* if_true) {
    return GotoIf(condition.condition(), if_true, condition.hint());
  }
  // Return `true` if the control flow after the conditional jump is reachable.
  ConditionalGotoStatus GotoIfNot(V<Word32> condition, Block* if_false,
                                  BranchHint hint = BranchHint::kNone) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      // What we return here should not matter.
      return ConditionalGotoStatus::kBranch;
    }
    Block* if_true = Asm().NewBlock();
    return BranchAndBind(condition, if_true, if_false, hint, if_true);
  }

  ConditionalGotoStatus GotoIfNot(ConditionWithHint condition,
                                  Block* if_false) {
    return GotoIfNot(condition.condition(), if_false, condition.hint());
  }

  OpIndex CallBuiltin(Builtin builtin, V<turboshaft::FrameState> frame_state,
                      base::Vector<OpIndex> arguments, CanThrow can_throw,
                      Isolate* isolate) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return OpIndex::Invalid();
    }
    Callable const callable = Builtins::CallableFor(isolate, builtin);
    Zone* graph_zone = Asm().output_graph().graph_zone();

    const CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
        graph_zone, callable.descriptor(),
        callable.descriptor().GetStackParameterCount(),
        CallDescriptor::kNoFlags, Operator::kNoThrow | Operator::kNoDeopt);
    DCHECK_EQ(call_descriptor->NeedsFrameState(), frame_state.valid());

    const TSCallDescriptor* ts_call_descriptor = TSCallDescriptor::Create(
        call_descriptor, can_throw, LazyDeoptOnThrow::kNo, graph_zone);

    OpIndex callee = Asm().HeapConstant(callable.code());

    return Asm().Call(callee, frame_state, arguments, ts_call_descriptor);
  }

  V<ConsString> NewConsString(V<Word32> length, V<String> first,
                              V<String> second) {
    return ReduceIfReachableNewConsString(length, first, second);
  }
  V<Object> NewArray(V<WordPtr> length, NewArrayOp::Kind kind,
                     AllocationType allocation_type) {
    return ReduceIfReachableNewArray(length, kind, allocation_type);
  }
  V<Object> NewDoubleArray(V<WordPtr> length, AllocationType allocation_type) {
    return NewArray(length, NewArrayOp::Kind::kDouble, allocation_type);
  }

  V<Object> DoubleArrayMinMax(V<Object> array, DoubleArrayMinMaxOp::Kind kind) {
    return ReduceIfReachableDoubleArrayMinMax(array, kind);
  }
  V<Object> DoubleArrayMin(V<Object> array) {
    return DoubleArrayMinMax(array, DoubleArrayMinMaxOp::Kind::kMin);
  }
  V<Object> DoubleArrayMax(V<Object> array) {
    return DoubleArrayMinMax(array, DoubleArrayMinMaxOp::Kind::kMax);
  }

  V<Any> LoadFieldByIndex(V<Object> object, V<Word32> index) {
    return ReduceIfReachableLoadFieldByIndex(object, index);
  }

  void DebugBreak() { ReduceIfReachableDebugBreak(); }

  // TODO(nicohartmann): Maybe this can be unified with Dcheck?
  void AssertImpl(V<Word32> condition, const char* condition_string,
                  const char* file, int line) {
#ifdef DEBUG
    // We use 256 characters as a buffer size. This can be increased if
    // necessary.
    static constexpr size_t kMaxAssertCommentLength = 256;
    base::Vector<char> buffer =
        Asm().data()->compilation_zone()->template AllocateVector<char>(
            kMaxAssertCommentLength);
    int result = base::SNPrintF(buffer, "Assert: %s    [%s:%d]",
                                condition_string, file, line);
    DCHECK_LT(0, result);
    Comment(buffer.data());
    IF_NOT (LIKELY(condition)) {
      Comment(buffer.data());
      Comment("ASSERT FAILED");
      DebugBreak();
    }

#endif
  }

  void DebugPrint(OpIndex input, RegisterRepresentation rep) {
    CHECK(v8_flags.turboshaft_enable_debug_features);
    ReduceIfReachableDebugPrint(input, rep);
  }
  void DebugPrint(V<Object> input) {
    DebugPrint(input, RegisterRepresentation::Tagged());
  }
  void DebugPrint(V<WordPtr> input) {
    DebugPrint(input, RegisterRepresentation::WordPtr());
  }
  void DebugPrint(V<Float64> input) {
    DebugPrint(input, RegisterRepresentation::Float64());
  }

  void Comment(const char* message) { ReduceIfReachableComment(message); }
  void Comment(const std::string& message) {
    size_t length = message.length() + 1;
    char* zone_buffer =
        Asm().data()->compilation_zone()->template AllocateArray<char>(length);
    MemCopy(zone_buffer, message.c_str(), length);
    Comment(zone_buffer);
  }
  using MessageWithSourceLocation = CodeAssembler::MessageWithSourceLocation;
  template <typename... Args>
  void CodeComment(MessageWithSourceLocation message, Args&&... args) {
    if (!v8_flags.code_comments) return;
    std::ostringstream s;
    USE(s << message.message, (s << std::forward<Args>(args))...);
    if (message.loc.FileName()) {
      s << " - " << message.loc.ToString();
    }
    Comment(std::move(s).str());
  }

  V<BigInt> BigIntBinop(V<BigInt> left, V<BigInt> right,
                        V<turboshaft::FrameState> frame_state,
                        BigIntBinopOp::Kind kind) {
    return ReduceIfReachableBigIntBinop(left, right, frame_state, kind);
  }
#define BIGINT_BINOP(kind)                                        \
  V<BigInt> BigInt##kind(V<BigInt> left, V<BigInt> right,         \
                         V<turboshaft::FrameState> frame_state) { \
    return BigIntBinop(left, right, frame_state,                  \
                       BigIntBinopOp::Kind::k##kind);             \
  }
  BIGINT_BINOP(Add)
  BIGINT_BINOP(Sub)
  BIGINT_BINOP(Mul)
  BIGINT_BINOP(Div)
  BIGINT_BINOP(Mod)
  BIGINT_BINOP(BitwiseAnd)
  BIGINT_BINOP(BitwiseOr)
  BIGINT_BINOP(BitwiseXor)
  BIGINT_BINOP(ShiftLeft)
  BIGINT_BINOP(ShiftRightArithmetic)
#undef BIGINT_BINOP

  V<Boolean> BigIntComparison(V<BigInt> left, V<BigInt> right,
                              BigIntComparisonOp::Kind kind) {
    return ReduceIfReachableBigIntComparison(left, right, kind);
  }
#define BIGINT_COMPARE(kind)                                                 \
  V<Boolean> BigInt##kind(V<BigInt> left, V<BigInt> right) {                 \
    return BigIntComparison(left, right, BigIntComparisonOp::Kind::k##kind); \
  }
  BIGINT_COMPARE(Equal)
  BIGINT_COMPARE(LessThan)
  BIGINT_COMPARE(LessThanOrEqual)
#undef BIGINT_COMPARE

  V<BigInt> BigIntUnary(V<BigInt> input, BigIntUnaryOp::Kind kind) {
    return ReduceIfReachableBigIntUnary(input, kind);
  }
  V<BigInt> BigIntNegate(V<BigInt> input) {
    return BigIntUnary(input, BigIntUnaryOp::Kind::kNegate);
  }

  OpIndex Word32PairBinop(V<Word32> left_low, V<Word32> left_high,
                          V<Word32> right_low, V<Word32> right_high,
                          Word32PairBinopOp::Kind kind) {
    return ReduceIfReachableWord32PairBinop(left_low, left_high, right_low,
                                            right_high, kind);
  }

  V<Word32> StringAt(V<String> string, V<WordPtr> position,
                     StringAtOp::Kind kind) {
    return ReduceIfReachableStringAt(string, position, kind);
  }
  V<Word32> StringCharCodeAt(V<String> string, V<WordPtr> position) {
    return StringAt(string, position, StringAtOp::Kind::kCharCode);
  }
  V<Word32> StringCodePointAt(V<String> string, V<WordPtr> position) {
    return StringAt(string, position, StringAtOp::Kind::kCodePoint);
  }

#ifdef V8_INTL_SUPPORT
  V<String> StringToCaseIntl(V<String> string, StringToCaseIntlOp::Kind kind) {
    return ReduceIfReachableStringToCaseIntl(string, kind);
  }
  V<String> StringToLowerCaseIntl(V<String> string) {
    return StringToCaseIntl(string, StringToCaseIntlOp::Kind::kLower);
  }
  V<String> StringToUpperCaseIntl(V<String> string) {
    return StringToCaseIntl(string, StringToCaseIntlOp::Kind::kUpper);
  }
#endif  // V8_INTL_SUPPORT

  V<Word32> StringLength(V<String> string) {
    return ReduceIfReachableStringLength(string);
  }

  V<Smi> StringIndexOf(V<String> string, V<String> search, V<Smi> position) {
    return ReduceIfReachableStringIndexOf(string, search, position);
  }

  V<String> StringFromCodePointAt(V<String> string, V<WordPtr> index) {
    return ReduceIfReachableStringFromCodePointAt(string, index);
  }

  V<String> StringSubstring(V<String> string, V<Word32> start, V<Word32> end) {
    return ReduceIfReachableStringSubstring(string, start, end);
  }

  V<String> StringConcat(V<String> left, V<String> right) {
    return ReduceIfReachableStringConcat(left, right);
  }

  V<Boolean> StringComparison(V<String> left, V<String> right,
                              StringComparisonOp::Kind kind) {
    return ReduceIfReachableStringComparison(left, right, kind);
  }
  V<Boolean> StringEqual(V<String> left, V<String> right) {
    return StringComparison(left, right, StringComparisonOp::Kind::kEqual);
  }
  V<Boolean> StringLessThan(V<String> left, V<String> right) {
    return StringComparison(left, right, StringComparisonOp::Kind::kLessThan);
  }
  V<Boolean> StringLessThanOrEqual(V<String> left, V<String> right) {
    return StringComparison(left, right,
                            StringComparisonOp::Kind::kLessThanOrEqual);
  }

  V<Smi> ArgumentsLength() {
    return ReduceIfReachableArgumentsLength(ArgumentsLengthOp::Kind::kArguments,
                                            0);
  }
  V<Smi> RestLength(int formal_parameter_count) {
    DCHECK_LE(0, formal_parameter_count);
    return ReduceIfReachableArgumentsLength(ArgumentsLengthOp::Kind::kRest,
                                            formal_parameter_count);
  }

  V<FixedArray> NewArgumentsElements(V<Smi> arguments_count,
                                     CreateArgumentsType type,
                                     int formal_parameter_count) {
    DCHECK_LE(0, formal_parameter_count);
    return ReduceIfReachableNewArgumentsElements(arguments_count, type,
                                                 formal_parameter_count);
  }

  OpIndex LoadTypedElement(OpIndex buffer, V<Object> base, V<WordPtr> external,
                           V<WordPtr> index, ExternalArrayType array_type) {
    return ReduceIfReachableLoadTypedElement(buffer, base, external, index,
                                             array_type);
  }

  OpIndex LoadDataViewElement(V<Object> object, V<WordPtr> storage,
                              V<WordPtr> index, V<Word32> is_little_endian,
                              ExternalArrayType element_type) {
    return ReduceIfReachableLoadDataViewElement(object, storage, index,
                                                is_little_endian, element_type);
  }

  V<Object> LoadStackArgument(V<Object> base, V<WordPtr> index) {
    return ReduceIfReachableLoadStackArgument(base, index);
  }

  void StoreTypedElement(OpIndex buffer, V<Object> base, V<WordPtr> external,
                         V<WordPtr> index, OpIndex value,
                         ExternalArrayType array_type) {
    ReduceIfReachableStoreTypedElement(buffer, base, external, index, value,
                                       array_type);
  }

  void StoreDataViewElement(V<Object> object, V<WordPtr> storage,
                            V<WordPtr> index, OpIndex value,
                            ConstOrV<Word32> is_little_endian,
                            ExternalArrayType element_type) {
    ReduceIfReachableStoreDataViewElement(
        object, storage, index, value, resolve(is_little_endian), element_type);
  }

  void TransitionAndStoreArrayElement(
      V<Object> array, V<WordPtr> index, OpIndex value,
      TransitionAndStoreArrayElementOp::Kind kind, MaybeHandle<Map> fast_map,
      MaybeHandle<Map> double_map) {
    ReduceIfReachableTransitionAndStoreArrayElement(array, index, value, kind,
                                                    fast_map, double_map);
  }

  void StoreSignedSmallElement(V<Object> array, V<WordPtr> index,
                               V<Word32> value) {
    TransitionAndStoreArrayElement(
        array, index, value,
        TransitionAndStoreArrayElementOp::Kind::kSignedSmallElement, {}, {});
  }

  V<Word32> CompareMaps(V<HeapObject> heap_object,
                        const ZoneRefSet<Map>& maps) {
    return ReduceIfReachableCompareMaps(heap_object, maps);
  }

  void CheckMaps(V<HeapObject> heap_object,
                 V<turboshaft::FrameState> frame_state,
                 const ZoneRefSet<Map>& maps, CheckMapsFlags flags,
                 const FeedbackSource& feedback) {
    ReduceIfReachableCheckMaps(heap_object, frame_state, maps, flags, feedback);
  }

  void AssumeMap(V<HeapObject> heap_object, const ZoneRefSet<Map>& maps) {
    ReduceIfReachableAssumeMap(heap_object, maps);
  }

  V<Object> CheckedClosure(V<Object> input,
                           V<turboshaft::FrameState> frame_state,
                           Handle<FeedbackCell> feedback_cell) {
    return ReduceIfReachableCheckedClosure(input, frame_state, feedback_cell);
  }

  void CheckEqualsInternalizedString(V<Object> expected, V<Object> value,
                                     V<turboshaft::FrameState> frame_state) {
    ReduceIfReachableCheckEqualsInternalizedString(expected, value,
                                                   frame_state);
  }

  V<Object> LoadMessage(V<WordPtr> offset) {
    return ReduceIfReachableLoadMessage(offset);
  }

  void StoreMessage(V<WordPtr> offset, V<Object> object) {
    ReduceIfReachableStoreMessage(offset, object);
  }

  V<Boolean> SameValue(V<Object> left, V<Object> right,
                       SameValueOp::Mode mode) {
    return ReduceIfReachableSameValue(left, right, mode);
  }

  V<Word32> Float64SameValue(V<Float64> left, V<Float64> right) {
    return ReduceIfReachableFloat64SameValue(left, right);
  }

  OpIndex FastApiCall(V<turboshaft::FrameState> frame_state,
                      V<Object> data_argument, V<Context> context,
                      base::Vector<const OpIndex> arguments,
                      const FastApiCallParameters* parameters) {
    return ReduceIfReachableFastApiCall(frame_state, data_argument, context,
                                        arguments, parameters);
  }

  void RuntimeAbort(AbortReason reason) {
    ReduceIfReachableRuntimeAbort(reason);
  }

  V<Object> EnsureWritableFastElements(V<Object> object, V<Object> elements) {
    return ReduceIfReachableEnsureWritableFastElements(object, elements);
  }

  V<Object> MaybeGrowFastElements(V<Object> object, V<Object> elements,
                                  V<Word32> index, V<Word32> elements_length,
                                  V<turboshaft::FrameState> frame_state,
                                  GrowFastElementsMode mode,
                                  const FeedbackSource& feedback) {
    return ReduceIfReachableMaybeGrowFastElements(
        object, elements, index, elements_length, frame_state, mode, feedback);
  }

  void TransitionElementsKind(V<HeapObject> object,
                              const ElementsTransition& transition) {
    ReduceIfReachableTransitionElementsKind(object, transition);
  }

  OpIndex FindOrderedHashEntry(V<Object> data_structure, OpIndex key,
                               FindOrderedHashEntryOp::Kind kind) {
    return ReduceIfReachableFindOrderedHashEntry(data_structure, key, kind);
  }
  V<Smi> FindOrderedHashMapEntry(V<Object> table, V<Smi> key) {
    return FindOrderedHashEntry(
        table, key, FindOrderedHashEntryOp::Kind::kFindOrderedHashMapEntry);
  }
  V<Smi> FindOrderedHashSetEntry(V<Object> table, V<Smi> key) {
    return FindOrderedHashEntry(
        table, key, FindOrderedHashEntryOp::Kind::kFindOrderedHashSetEntry);
  }
  V<WordPtr> FindOrderedHashMapEntryForInt32Key(V<Object> table,
                                                V<Word32> key) {
    return FindOrderedHashEntry(
        table, key,
        FindOrderedHashEntryOp::Kind::kFindOrderedHashMapEntryForInt32Key);
  }
  V<Object> SpeculativeNumberBinop(V<Object> left, V<Object> right,
                                   V<turboshaft::FrameState> frame_state,
                                   SpeculativeNumberBinopOp::Kind kind) {
    return ReduceIfReachableSpeculativeNumberBinop(left, right, frame_state,
                                                   kind);
  }

  V<Object> LoadRoot(RootIndex root_index) {
    Isolate* isolate = __ data() -> isolate();
    DCHECK_NOT_NULL(isolate);
    if (RootsTable::IsImmortalImmovable(root_index)) {
      Handle<Object> root = isolate->root_handle(root_index);
      if (i::IsSmi(*root)) {
        return __ SmiConstant(Cast<Smi>(*root));
      } else {
        return HeapConstantMaybeHole(i::Cast<HeapObject>(root));
      }
    }

    // TODO(jgruber): In theory we could generate better code for this by
    // letting the macro assembler decide how to load from the roots list. In
    // most cases, it would boil down to loading from a fixed kRootRegister
    // offset.
    OpIndex isolate_root =
        __ ExternalConstant(ExternalReference::isolate_root(isolate));
    int offset = IsolateData::root_slot_offset(root_index);
    return __ LoadOffHeap(isolate_root, offset,
                          MemoryRepresentation::AnyTagged());
  }

#define HEAP_CONSTANT_ACCESSOR(rootIndexName, rootAccessorName, name)          \
  V<RemoveTagged<                                                              \
      decltype(std::declval<ReadOnlyRoots>().rootAccessorName())>::type>       \
      name##Constant() {                                                       \
    const TurboshaftPipelineKind kind = __ data() -> pipeline_kind();          \
    if (V8_UNLIKELY(kind == TurboshaftPipelineKind::kCSA ||                    \
                    kind == TurboshaftPipelineKind::kTSABuiltin)) {            \
      DCHECK(RootsTable::IsImmortalImmovable(RootIndex::k##rootIndexName));    \
      return V<RemoveTagged<                                                   \
          decltype(std::declval<ReadOnlyRoots>().rootAccessorName())>::type>:: \
          Cast(__ LoadRoot(RootIndex::k##rootIndexName));                      \
    } else {                                                                   \
      Isolate* isolate = __ data() -> isolate();                               \
      DCHECK_NOT_NULL(isolate);                                                \
      Factory* factory = isolate->factory();                                   \
      DCHECK_NOT_NULL(factory);                                                \
      return __ HeapConstant(factory->rootAccessorName());                     \
    }                                                                          \
  }
  HEAP_IMMUTABLE_IMMOVABLE_OBJECT_LIST(HEAP_CONSTANT_ACCESSOR)
#undef HEAP_CONSTANT_ACCESSOR

#define HEAP_CONSTANT_ACCESSOR(rootIndexName, rootAccessorName, name)       \
  V<RemoveTagged<decltype(std::declval<Heap>().rootAccessorName())>::type>  \
      name##Constant() {                                                    \
    const TurboshaftPipelineKind kind = __ data() -> pipeline_kind();       \
    if (V8_UNLIKELY(kind == TurboshaftPipelineKind::kCSA ||                 \
                    kind == TurboshaftPipelineKind::kTSABuiltin)) {         \
      DCHECK(RootsTable::IsImmortalImmovable(RootIndex::k##rootIndexName)); \
      return V<                                                             \
          RemoveTagged<decltype(std::declval<Heap>().rootAccessorName())>:: \
              type>::Cast(__ LoadRoot(RootIndex::k##rootIndexName));        \
    } else {                                                                \
      Isolate* isolate = __ data() -> isolate();                            \
      DCHECK_NOT_NULL(isolate);                                             \
      Factory* factory = isolate->factory();                                \
      DCHECK_NOT_NULL(factory);                                             \
      return __ HeapConstant(factory->rootAccessorName());                  \
    }                                                                       \
  }
  HEAP_MUTABLE_IMMOVABLE_OBJECT_LIST(HEAP_CONSTANT_ACCESSOR)
#undef HEAP_CONSTANT_ACCESSOR

#define HEAP_CONSTANT_TEST(rootIndexName, rootAccessorName, name) \
  V<Word32> Is##name(V<Object> value) {                           \
    return TaggedEqual(value, name##Constant());                  \
  }                                                               \
  V<Word32> IsNot##name(V<Object> value) {                        \
    return TaggedNotEqual(value, name##Constant());               \
  }
  HEAP_IMMOVABLE_OBJECT_LIST(HEAP_CONSTANT_TEST)
#undef HEAP_CONSTANT_TEST

#ifdef V8_ENABLE_WEBASSEMBLY
  V<Any> GlobalGet(V<WasmTrustedInstanceData> trusted_instance_data,
                   const wasm::WasmGlobal* global) {
    return ReduceIfReachableGlobalGet(trusted_instance_data, global);
  }

  OpIndex GlobalSet(V<WasmTrustedInstanceData> trusted_instance_data,
                    V<Any> value, const wasm::WasmGlobal* global) {
    return ReduceIfReachableGlobalSet(trusted_instance_data, value, global);
  }

  V<HeapObject> Null(wasm::ValueType type) {
    return ReduceIfReachableNull(type);
  }

  V<Word32> IsNull(V<Object> input, wasm::ValueType type) {
    return ReduceIfReachableIsNull(input, type);
  }

  V<Object> AssertNotNull(V<Object> object, wasm::ValueType type,
                          TrapId trap_id) {
    return ReduceIfReachableAssertNotNull(object, type, trap_id);
  }

  V<Map> RttCanon(V<FixedArray> rtts, uint32_t type_index) {
    return ReduceIfReachableRttCanon(rtts, type_index);
  }

  V<Word32> WasmTypeCheck(V<Object> object, OptionalV<Map> rtt,
                          WasmTypeCheckConfig config) {
    return ReduceIfReachableWasmTypeCheck(object, rtt, config);
  }

  V<Object> WasmTypeCast(V<Object> object, OptionalV<Map> rtt,
                         WasmTypeCheckConfig config) {
    return ReduceIfReachableWasmTypeCast(object, rtt, config);
  }

  V<Object> AnyConvertExtern(V<Object> input) {
    return ReduceIfReachableAnyConvertExtern(input);
  }

  V<Object> ExternConvertAny(V<Object> input) {
    return ReduceIfReachableExternConvertAny(input);
  }

  template <typename T>
  V<T> AnnotateWasmType(V<T> value, const wasm::ValueType type) {
    return ReduceIfReachableWasmTypeAnnotation(value, type);
  }

  V<Any> StructGet(V<WasmStructNullable> object, const wasm::StructType* type,
                   uint32_t type_index, int field_index, bool is_signed,
                   CheckForNull null_check) {
    return ReduceIfReachableStructGet(object, type, type_index, field_index,
                                      is_signed, null_check);
  }

  void StructSet(V<WasmStructNullable> object, V<Any> value,
                 const wasm::StructType* type, uint32_t type_index,
                 int field_index, CheckForNull null_check) {
    ReduceIfReachableStructSet(object, value, type, type_index, field_index,
                               null_check);
  }

  V<Any> ArrayGet(V<WasmArrayNullable> array, V<Word32> index,
                  const wasm::ArrayType* array_type, bool is_signed) {
    return ReduceIfReachableArrayGet(array, index, array_type, is_signed);
  }

  void ArraySet(V<WasmArrayNullable> array, V<Word32> index, V<Any> value,
                wasm::ValueType element_type) {
    ReduceIfReachableArraySet(array, index, value, element_type);
  }

  V<Word32> ArrayLength(V<WasmArrayNullable> array, CheckForNull null_check) {
    return ReduceIfReachableArrayLength(array, null_check);
  }

  V<WasmArray> WasmAllocateArray(V<Map> rtt, ConstOrV<Word32> length,
                                 const wasm::ArrayType* array_type) {
    return ReduceIfReachableWasmAllocateArray(rtt, resolve(length), array_type);
  }

  V<WasmStruct> WasmAllocateStruct(V<Map> rtt,
                                   const wasm::StructType* struct_type) {
    return ReduceIfReachableWasmAllocateStruct(rtt, struct_type);
  }

  V<WasmFuncRef> WasmRefFunc(V<Object> wasm_instance, uint32_t function_index) {
    return ReduceIfReachableWasmRefFunc(wasm_instance, function_index);
  }

  V<String> StringAsWtf16(V<String> string) {
    return ReduceIfReachableStringAsWtf16(string);
  }

  V<turboshaft::Tuple<Object, WordPtr, Word32>> StringPrepareForGetCodeUnit(
      V<Object> string) {
    return ReduceIfReachableStringPrepareForGetCodeUnit(string);
  }

  V<Simd128> Simd128Constant(const uint8_t value[kSimd128Size]) {
    return ReduceIfReachableSimd128Constant(value);
  }

  V<Simd128> Simd128Binop(V<Simd128> left, V<Simd128> right,
                          Simd128BinopOp::Kind kind) {
    return ReduceIfReachableSimd128Binop(left, right, kind);
  }

  V<Simd128> Simd128Unary(V<Simd128> input, Simd128UnaryOp::Kind kind) {
    return ReduceIfReachableSimd128Unary(input, kind);
  }

  V<Simd128> Simd128ReverseBytes(V<Simd128> input) {
    return Simd128Unary(input, Simd128UnaryOp::Kind::kSimd128ReverseBytes);
  }

  V<Simd128> Simd128Shift(V<Simd128> input, V<Word32> shift,
                          Simd128ShiftOp::Kind kind) {
    return ReduceIfReachableSimd128Shift(input, shift, kind);
  }

  V<Word32> Simd128Test(V<Simd128> input, Simd128TestOp::Kind kind) {
    return ReduceIfReachableSimd128Test(input, kind);
  }

  V<Simd128> Simd128Splat(V<Any> input, Simd128SplatOp::Kind kind) {
    return ReduceIfReachableSimd128Splat(input, kind);
  }

  V<Simd128> Simd128Ternary(V<Simd128> first, V<Simd128> second,
                            V<Simd128> third, Simd128TernaryOp::Kind kind) {
    return ReduceIfReachableSimd128Ternary(first, second, third, kind);
  }

  V<Any> Simd128ExtractLane(V<Simd128> input, Simd128ExtractLaneOp::Kind kind,
                            uint8_t lane) {
    return ReduceIfReachableSimd128ExtractLane(input, kind, lane);
  }

  V<Simd128> Simd128Reduce(V<Simd128> input, Simd128ReduceOp::Kind kind) {
    return ReduceIfReachableSimd128Reduce(input, kind);
  }

  V<Simd128> Simd128ReplaceLane(V<Simd128> into, V<Any> new_lane,
                                Simd128ReplaceLaneOp::Kind kind, uint8_t lane) {
    return ReduceIfReachableSimd128ReplaceLane(into, new_lane, kind, lane);
  }

  OpIndex Simd128LaneMemory(V<WordPtr> base, V<WordPtr> index, V<WordPtr> value,
                            Simd128LaneMemoryOp::Mode mode,
                            Simd128LaneMemoryOp::Kind kind,
                            Simd128LaneMemoryOp::LaneKind lane_kind,
                            uint8_t lane, int offset) {
    return ReduceIfReachableSimd128LaneMemory(base, index, value, mode, kind,
                                              lane_kind, lane, offset);
  }

  V<Simd128> Simd128LoadTransform(
      V<WordPtr> base, V<WordPtr> index,
      Simd128LoadTransformOp::LoadKind load_kind,
      Simd128LoadTransformOp::TransformKind transform_kind, int offset) {
    return ReduceIfReachableSimd128LoadTransform(base, index, load_kind,
                                                 transform_kind, offset);
  }

  V<Simd128> Simd128Shuffle(V<Simd128> left, V<Simd128> right,
                            const uint8_t shuffle[kSimd128Size]) {
    return ReduceIfReachableSimd128Shuffle(left, right, shuffle);
  }

  // SIMD256
#if V8_ENABLE_WASM_SIMD256_REVEC
  V<Simd256> Simd256Constant(const uint8_t value[kSimd256Size]) {
    return ReduceIfReachableSimd256Constant(value);
  }

  OpIndex Simd256Extract128Lane(V<Simd256> source, uint8_t lane) {
    return ReduceIfReachableSimd256Extract128Lane(source, lane);
  }

  V<Simd256> Simd256LoadTransform(
      V<WordPtr> base, V<WordPtr> index,
      Simd256LoadTransformOp::LoadKind load_kind,
      Simd256LoadTransformOp::TransformKind transform_kind, int offset) {
    return ReduceIfReachableSimd256LoadTransform(base, index, load_kind,
                                                 transform_kind, offset);
  }

  V<Simd256> Simd256Unary(V<Simd256> input, Simd256UnaryOp::Kind kind) {
    return ReduceIfReachableSimd256Unary(input, kind);
  }

  V<Simd256> Simd256Unary(V<Simd128> input, Simd256UnaryOp::Kind kind) {
    DCHECK_GE(kind, Simd256UnaryOp::Kind::kFirstSignExtensionOp);
    DCHECK_LE(kind, Simd256UnaryOp::Kind::kLastSignExtensionOp);
    return ReduceIfReachableSimd256Unary(input, kind);
  }

  V<Simd256> Simd256Binop(V<Simd256> left, V<Simd256> right,
                          Simd256BinopOp::Kind kind) {
    return ReduceIfReachableSimd256Binop(left, right, kind);
  }

  V<Simd256> Simd256Binop(V<Simd128> left, V<Simd128> right,
                          Simd256BinopOp::Kind kind) {
    DCHECK_GE(kind, Simd256BinopOp::Kind::kFirstSignExtensionOp);
    DCHECK_LE(kind, Simd256BinopOp::Kind::kLastSignExtensionOp);
    return ReduceIfReachableSimd256Binop(left, right, kind);
  }

  V<Simd256> Simd256Shift(V<Simd256> input, V<Word32> shift,
                          Simd256ShiftOp::Kind kind) {
    return ReduceIfReachableSimd256Shift(input, shift, kind);
  }

  V<Simd256> Simd256Ternary(V<Simd256> first, V<Simd256> second,
                            V<Simd256> third, Simd256TernaryOp::Kind kind) {
    return ReduceIfReachableSimd256Ternary(first, second, third, kind);
  }

  V<Simd256> Simd256Splat(OpIndex input, Simd256SplatOp::Kind kind) {
    return ReduceIfReachableSimd256Splat(input, kind);
  }

  V<Simd256> SimdPack128To256(V<Simd128> left, V<Simd128> right) {
    return ReduceIfReachableSimdPack128To256(left, right);
  }

#ifdef V8_TARGET_ARCH_X64
  V<Simd256> Simd256Shufd(V<Simd256> input, const uint8_t control) {
    return ReduceIfReachableSimd256Shufd(input, control);
  }

  V<Simd256> Simd256Shufps(V<Simd256> left, V<Simd256> right,
                           const uint8_t control) {
    return ReduceIfReachableSimd256Shufps(left, right, control);
  }

  V<Simd256> Simd256Unpack(V<Simd256> left, V<Simd256> right,
                           Simd256UnpackOp::Kind kind) {
    return ReduceIfReachableSimd256Unpack(left, right, kind);
  }
#endif  // V8_TARGET_ARCH_X64
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

  V<WasmTrustedInstanceData> WasmInstanceParameter() {
    return Parameter(wasm::kWasmInstanceParameterIndex,
                     RegisterRepresentation::Tagged());
  }

  OpIndex LoadStackPointer() { return ReduceIfReachableLoadStackPointer(); }

  void SetStackPointer(V<WordPtr> value) {
    ReduceIfReachableSetStackPointer(value);
  }
#endif  // V8_ENABLE_WEBASSEMBLY

#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  V<Object> GetContinuationPreservedEmbedderData() {
    return ReduceIfReachableGetContinuationPreservedEmbedderData();
  }

  void SetContinuationPreservedEmbedderData(V<Object> data) {
    ReduceIfReachableSetContinuationPreservedEmbedderData(data);
  }
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA

  template <typename Rep>
  V<Rep> resolve(const V<Rep>& v) {
    return v;
  }
  V<Word32> resolve(const ConstOrV<Word32>& v) {
    return v.is_constant() ? Word32Constant(v.constant_value()) : v.value();
  }
  V<Word64> resolve(const ConstOrV<Word64>& v) {
    return v.is_constant() ? Word64Constant(v.constant_value()) : v.value();
  }
  V<Float32> resolve(const ConstOrV<Float32>& v) {
    return v.is_constant() ? Float32Constant(v.constant_value()) : v.value();
  }
  V<Float64> resolve(const ConstOrV<Float64>& v) {
    return v.is_constant() ? Float64Constant(v.constant_value()) : v.value();
  }

 private:
#ifdef DEBUG
#define REDUCE_OP(Op)                                                    \
  template <class... Args>                                               \
  V8_INLINE OpIndex ReduceIfReachable##Op(Args... args) {                \
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {        \
      DCHECK(Asm().conceptually_in_a_block());                           \
      return OpIndex::Invalid();                                         \
    }                                                                    \
    OpIndex result = Asm().Reduce##Op(args...);                          \
    if constexpr (!IsBlockTerminator(Opcode::k##Op)) {                   \
      if (Asm().current_block() == nullptr) {                            \
        /* The input operation was not a block terminator, but a reducer \
         * lowered it into a block terminator. */                        \
        Asm().set_conceptually_in_a_block(true);                         \
      }                                                                  \
    }                                                                    \
    return result;                                                       \
  }
#else
#define REDUCE_OP(Op)                                             \
  template <class... Args>                                        \
  V8_INLINE OpIndex ReduceIfReachable##Op(Args... args) {         \
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) { \
      return OpIndex::Invalid();                                  \
    }                                                             \
    return Asm().Reduce##Op(args...);                             \
  }
#endif
  TURBOSHAFT_OPERATION_LIST(REDUCE_OP)
#undef REDUCE_OP

  // LoadArrayBufferElement and LoadNonArrayBufferElement should be called
  // instead of LoadElement.
  template <typename T = Any, typename Base>
  V<T> LoadElement(V<Base> object, const ElementAccess& access,
                   V<WordPtr> index, bool is_array_buffer) {
    if constexpr (is_taggable_v<Base>) {
      DCHECK_EQ(access.base_is_tagged, BaseTaggedness::kTaggedBase);
    } else {
      static_assert(std::is_same_v<Base, WordPtr>);
      DCHECK_EQ(access.base_is_tagged, BaseTaggedness::kUntaggedBase);
    }
    LoadOp::Kind kind = LoadOp::Kind::Aligned(access.base_is_tagged);
    if (is_array_buffer) kind = kind.NotLoadEliminable();
    MemoryRepresentation rep =
        MemoryRepresentation::FromMachineType(access.machine_type);
    return Load(object, index, kind, rep, access.header_size,
                rep.SizeInBytesLog2());
  }

  // StoreArrayBufferElement and StoreNonArrayBufferElement should be called
  // instead of StoreElement.
  template <typename Base>
  void StoreElement(V<Base> object, const ElementAccess& access,
                    ConstOrV<WordPtr> index, V<Any> value,
                    bool is_array_buffer) {
    if constexpr (is_taggable_v<Base>) {
      DCHECK_EQ(access.base_is_tagged, BaseTaggedness::kTaggedBase);
    } else {
      static_assert(std::is_same_v<Base, WordPtr>);
      DCHECK_EQ(access.base_is_tagged, BaseTaggedness::kUntaggedBase);
    }
    LoadOp::Kind kind = LoadOp::Kind::Aligned(access.base_is_tagged);
    if (is_array_buffer) kind = kind.NotLoadEliminable();
    MemoryRepresentation rep =
        MemoryRepresentation::FromMachineType(access.machine_type);
    Store(object, resolve(index), value, kind, rep, access.write_barrier_kind,
          access.header_size, rep.SizeInBytesLog2());
  }

  // BranchAndBind should be called from GotoIf/GotoIfNot. It will insert a
  // Branch, bind {to_bind} (which should correspond to the implicit new block
  // following the GotoIf/GotoIfNot) and return a ConditionalGotoStatus
  // representing whether the destinations of the Branch are reachable or not.
  ConditionalGotoStatus BranchAndBind(V<Word32> condition, Block* if_true,
                                      Block* if_false, BranchHint hint,
                                      Block* to_bind) {
    DCHECK_EQ(to_bind, any_of(if_true, if_false));
    Block* other = to_bind == if_true ? if_false : if_true;
    Block* to_bind_last_pred = to_bind->LastPredecessor();
    Block* other_last_pred = other->LastPredecessor();
    Asm().Branch(condition, if_true, if_false, hint);
    bool to_bind_reachable = to_bind_last_pred != to_bind->LastPredecessor();
    bool other_reachable = other_last_pred != other->LastPredecessor();
    ConditionalGotoStatus status = static_cast<ConditionalGotoStatus>(
        static_cast<int>(other_reachable) | ((to_bind_reachable) << 1));
    bool bind_status = Asm().Bind(to_bind);
    DCHECK_EQ(bind_status, to_bind_reachable);
    USE(bind_status);
    return status;
  }

  base::SmallVector<OpIndex, 16> cached_parameters_;
  // [0] contains the stub with exit frame.
  MaybeHandle<Code> cached_centry_stub_constants_[4];
  bool in_object_initialization_ = false;

  OperationMatcher matcher_;
};

// Some members of Assembler that are used in the constructors of the stack are
// extracted to the AssemblerData class, so that they can be initialized before
// the rest of the stack, and thus don't need to be passed as argument to all of
// the constructors of the stack.
struct AssemblerData {
  // TODO(dmercadier): consider removing input_graph from this, and only having
  // it in GraphVisitor for Stacks that have it.
  AssemblerData(PipelineData* data, Graph& input_graph, Graph& output_graph,
                Zone* phase_zone)
      : data(data),
        phase_zone(phase_zone),
        input_graph(input_graph),
        output_graph(output_graph) {}
  PipelineData* data;
  Zone* phase_zone;
  Graph& input_graph;
  Graph& output_graph;
};

template <class Reducers>
class Assembler : public AssemblerData,
                  public reducer_stack_type<Reducers>::type {
  using Stack = typename reducer_stack_type<Reducers>::type;
  using node_t = typename Stack::node_t;

 public:
  explicit Assembler(PipelineData* data, Graph& input_graph,
                     Graph& output_graph, Zone* phase_zone)
      : AssemblerData(data, input_graph, output_graph, phase_zone), Stack() {
    SupportedOperations::Initialize();
  }

  using Stack::Asm;

  PipelineData* data() const { return AssemblerData::data; }
  Zone* phase_zone() { return AssemblerData::phase_zone; }
  const Graph& input_graph() const { return AssemblerData::input_graph; }
  Graph& output_graph() const { return AssemblerData::output_graph; }
  Zone* graph_zone() const { return output_graph().graph_zone(); }

  // When analyzers detect that an operation is dead, they replace its opcode by
  // kDead in-place, and thus need to have a non-const input graph.
  Graph& modifiable_input_graph() const { return AssemblerData::input_graph; }

  Block* NewLoopHeader() { return this->output_graph().NewLoopHeader(); }
  Block* NewBlock() { return this->output_graph().NewBlock(); }

  V8_INLINE bool Bind(Block* block) {
#ifdef DEBUG
    set_conceptually_in_a_block(true);
#endif
    if (!this->output_graph().Add(block)) {
      return false;
    }
    DCHECK_NULL(current_block_);
    current_block_ = block;
    Stack::Bind(block);
    return true;
  }

  // TODO(nicohartmann@): Remove this.
  V8_INLINE void BindReachable(Block* block) {
    bool bound = Bind(block);
    DCHECK(bound);
    USE(bound);
  }

  // Every loop should be finalized once, after it is certain that no backedge
  // can be added anymore.
  void FinalizeLoop(Block* loop_header) {
    if (loop_header->IsLoop() && loop_header->PredecessorCount() == 1) {
      this->output_graph().TurnLoopIntoMerge(loop_header);
    }
  }

  void SetCurrentOrigin(OpIndex operation_origin) {
    current_operation_origin_ = operation_origin;
  }

#ifdef DEBUG
  void set_conceptually_in_a_block(bool value) {
    conceptually_in_a_block_ = value;
  }
  bool conceptually_in_a_block() { return conceptually_in_a_block_; }
#endif

  Block* current_block() const { return current_block_; }
  bool generating_unreachable_operations() const {
    return current_block() == nullptr;
  }
  V<AnyOrNone> current_operation_origin() const {
    return current_operation_origin_;
  }

  const Operation& Get(OpIndex op_idx) const {
    return this->output_graph().Get(op_idx);
  }

  Block* current_catch_block() const { return current_catch_block_; }
  // CatchScope should be used in most cases to set the current catch block, but
  // this is sometimes impractical.
  void set_current_catch_block(Block* block) { current_catch_block_ = block; }

#ifdef DEBUG
  int& intermediate_tracing_depth() { return intermediate_tracing_depth_; }
#endif

  // Adds {source} to the predecessors of {destination}.
  void AddPredecessor(Block* source, Block* destination, bool branch) {
    DCHECK_IMPLIES(branch, source->EndsWithBranchingOp(this->output_graph()));
    if (destination->LastPredecessor() == nullptr) {
      // {destination} has currently no predecessors.
      DCHECK(destination->IsLoopOrMerge());
      if (branch && destination->IsLoop()) {
        // We always split Branch edges that go to loop headers.
        SplitEdge(source, destination);
      } else {
        destination->AddPredecessor(source);
        if (branch) {
          DCHECK(!destination->IsLoop());
          destination->SetKind(Block::Kind::kBranchTarget);
        }
      }
      return;
    } else if (destination->IsBranchTarget()) {
      // {destination} used to be a BranchTarget, but branch targets can only
      // have one predecessor. We'll thus split its (single) incoming edge, and
      // change its type to kMerge.
      DCHECK_EQ(destination->PredecessorCount(), 1);
      Block* pred = destination->LastPredecessor();
      destination->ResetLastPredecessor();
      destination->SetKind(Block::Kind::kMerge);
      // We have to split `pred` first to preserve order of predecessors.
      SplitEdge(pred, destination);
      if (branch) {
        // A branch always goes to a BranchTarget. We thus split the edge: we'll
        // insert a new Block, to which {source} will branch, and which will
        // "Goto" to {destination}.
        SplitEdge(source, destination);
      } else {
        // {destination} is a Merge, and {source} just does a Goto; nothing
        // special to do.
        destination->AddPredecessor(source);
      }
      return;
    }

    DCHECK(destination->IsLoopOrMerge());

    if (branch) {
      // A branch always goes to a BranchTarget. We thus split the edge: we'll
      // insert a new Block, to which {source} will branch, and which will
      // "Goto" to {destination}.
      SplitEdge(source, destination);
    } else {
      // {destination} is a Merge, and {source} just does a Goto; nothing
      // special to do.
      destination->AddPredecessor(source);
    }
  }

 private:
  void FinalizeBlock() {
    this->output_graph().Finalize(current_block_);
    current_block_ = nullptr;
#ifdef DEBUG
    set_conceptually_in_a_block(false);
#endif
  }

  // Insert a new Block between {source} and {destination}, in order to maintain
  // the split-edge form.
  void SplitEdge(Block* source, Block* destination) {
    DCHECK(source->EndsWithBranchingOp(this->output_graph()));
    // Creating the new intermediate block
    Block* intermediate_block = NewBlock();
    intermediate_block->SetKind(Block::Kind::kBranchTarget);
    // Updating "predecessor" edge of {intermediate_block}. This needs to be
    // done before calling Bind, because otherwise Bind will think that this
    // block is not reachable.
    intermediate_block->AddPredecessor(source);

    // Updating {source}'s last Branch/Switch/CheckException. Note that
    // this must be done before Binding {intermediate_block}, otherwise,
    // Reducer::Bind methods will see an invalid block being bound (because its
    // predecessor would be a branch, but none of its targets would be the block
    // being bound).
    Operation& op = this->output_graph().Get(
        this->output_graph().PreviousIndex(source->end()));
    switch (op.opcode) {
      case Opcode::kBranch: {
        BranchOp& branch = op.Cast<BranchOp>();
        if (branch.if_true == destination) {
          branch.if_true = intermediate_block;
          // We enforce that Branches if_false and if_true can never be the same
          // (there is a DCHECK in Assembler::Branch enforcing that).
          DCHECK_NE(branch.if_false, destination);
        } else {
          DCHECK_EQ(branch.if_false, destination);
          branch.if_false = intermediate_block;
        }
        break;
      }
      case Opcode::kCheckException: {
        CheckExceptionOp& catch_exception_op = op.Cast<CheckExceptionOp>();
        if (catch_exception_op.didnt_throw_block == destination) {
          catch_exception_op.didnt_throw_block = intermediate_block;
          // We assume that CheckException's successor and catch_block
          // can never be the same (there is a DCHECK in
          // CheckExceptionOp::Validate enforcing that).
          DCHECK_NE(catch_exception_op.catch_block, destination);
        } else {
          DCHECK_EQ(catch_exception_op.catch_block, destination);
          catch_exception_op.catch_block = intermediate_block;
          // A catch block always has to start with a `CatchBlockBeginOp`.
          BindReachable(intermediate_block);
          intermediate_block->SetOrigin(source->OriginForBlockEnd());
          this->CatchBlockBegin();
          this->Goto(destination);
          return;
        }
        break;
      }
      case Opcode::kSwitch: {
        SwitchOp& switch_op = op.Cast<SwitchOp>();
        bool found = false;
        for (auto& case_block : switch_op.cases) {
          if (case_block.destination == destination) {
            case_block.destination = intermediate_block;
            DCHECK(!found);
            found = true;
#ifndef DEBUG
            break;
#endif
          }
        }
        DCHECK_IMPLIES(found, switch_op.default_case != destination);
        if (!found) {
          DCHECK_EQ(switch_op.default_case, destination);
          switch_op.default_case = intermediate_block;
        }
        break;
      }

      default:
        UNREACHABLE();
    }

    BindReachable(intermediate_block);
    intermediate_block->SetOrigin(source->OriginForBlockEnd());
    // Inserting a Goto in {intermediate_block} to {destination}. This will
    // create the edge from {intermediate_block} to {destination}. Note that
    // this will call AddPredecessor, but we've already removed the eventual
    // edge of {destination} that need splitting, so no risks of infinite
    // recursion here.
    this->Goto(destination);
  }

  Block* current_block_ = nullptr;
  Block* current_catch_block_ = nullptr;

  // `current_block_` is nullptr after emitting a block terminator and before
  // Binding the next block. During this time, emitting an operation doesn't do
  // anything (because in which block would it be emitted?). However, we also
  // want to prevent silently skipping operations because of a missing Bind.
  // Consider for instance a lowering that would do:
  //
  //     __ Add(x, y)
  //     __ Goto(B)
  //     __ Add(i, j)
  //
  // The 2nd Add is unreachable, but this has to be a mistake, since we exitted
  // the current block before emitting it, and forgot to Bind a new block.
  // On the other hand, consider this:
  //
  //     __ Add(x, y)
  //     __ Goto(B1)
  //     __ Bind(B2)
  //     __ Add(i, j)
  //
  // It's possible that B2 is not reachable, in which case `Bind(B2)` will set
  // the current_block to nullptr.
  // Similarly, consider:
  //
  //    __ Add(x, y)
  //    __ DeoptimizeIf(cond)
  //    __ Add(i, j)
  //
  // It's possible that a reducer lowers the `DeoptimizeIf` to an unconditional
  // `Deoptimize`.
  //
  // The 1st case should produce an error (because a Bind was forgotten), but
  // the 2nd and 3rd case should not.
  //
  // The way we achieve this is with the following `conceptually_in_a_block_`
  // boolean:
  //   - when Binding a block (successfully or not), we set
  //   `conceptually_in_a_block_` to true.
  //   - when exiting a block (= emitting a block terminator), we set
  //   `conceptually_in_a_block_` to false.
  //   - after the AssemblerOpInterface lowers a non-block-terminator which
  //   makes the current_block_ become nullptr (= the last operation of its
  //   lowering became a block terminator), we set `conceptually_in_a_block_` to
  //   true (overriding the "false" that was set when emitting the block
  //   terminator).
  //
  // Note that there is one category of errors that this doesn't prevent: if a
  // lowering of a non-block terminator creates new control flow and forgets a
  // final Bind, we'll set `conceptually_in_a_block_` to true and assume that
  // this lowering unconditionally exits the control flow. However, it's hard to
  // distinguish between lowerings that voluntarily end with block terminators,
  // and those who forgot a Bind.
  bool conceptually_in_a_block_ = false;

  // TODO(dmercadier,tebbi): remove {current_operation_origin_} and pass instead
  // additional parameters to ReduceXXX methods.
  V<AnyOrNone> current_operation_origin_ = V<AnyOrNone>::Invalid();

#ifdef DEBUG
  int intermediate_tracing_depth_ = 0;
#endif

  template <class Next>
  friend class TSReducerBase;
  template <class AssemblerT>
  friend class CatchScopeImpl;
};

template <class AssemblerT>
class CatchScopeImpl {
 public:
  CatchScopeImpl(AssemblerT& assembler, Block* catch_block)
      : assembler_(assembler),
        previous_catch_block_(assembler.current_catch_block_) {
    assembler_.current_catch_block_ = catch_block;
#ifdef DEBUG
    this->catch_block = catch_block;
#endif
  }

  ~CatchScopeImpl() {
    DCHECK_EQ(assembler_.current_catch_block_, catch_block);
    assembler_.current_catch_block_ = previous_catch_block_;
  }

  CatchScopeImpl& operator=(const CatchScopeImpl&) = delete;
  CatchScopeImpl(const CatchScopeImpl&) = delete;
  CatchScopeImpl& operator=(CatchScopeImpl&&) = delete;
  CatchScopeImpl(CatchScopeImpl&&) = delete;

 private:
  AssemblerT& assembler_;
  Block* previous_catch_block_;
#ifdef DEBUG
  Block* catch_block = nullptr;
#endif

  template <class Reducers>
  friend class Assembler;
};

template <template <class> class... Reducers>
class TSAssembler
    : public Assembler<reducer_list<TurboshaftAssemblerOpInterface, Reducers...,
                                    TSReducerBase>> {
 public:
  using Assembler<reducer_list<TurboshaftAssemblerOpInterface, Reducers...,
                               TSReducerBase>>::Assembler;
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_ASSEMBLER_H_
