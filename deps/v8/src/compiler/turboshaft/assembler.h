// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_ASSEMBLER_H_
#define V8_COMPILER_TURBOSHAFT_ASSEMBLER_H_

#include <cstring>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/small-vector.h"
#include "src/base/string-format.h"
#include "src/base/template-utils.h"
#include "src/base/vector.h"
#include "src/codegen/callable.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/reloc-info.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/globals.h"
#include "src/compiler/simplified-operator.h"
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
#include "src/objects/elements-kind.h"
#include "src/objects/fixed-array.h"
#include "src/objects/heap-number.h"
#include "src/objects/oddball.h"
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
template <typename T, typename = void>
struct has_constexpr_type : std::false_type {};

template <typename T>
struct has_constexpr_type<T, std::void_t<typename v_traits<T>::constexpr_type>>
    : std::true_type {};

template <typename T, typename...>
struct make_const_or_v {
  using type = V<T>;
};

template <typename T>
struct make_const_or_v<
    T, typename std::enable_if_t<has_constexpr_type<T>::value>> {
  using type = ConstOrV<T>;
};

template <typename T>
struct make_const_or_v<
    T, typename std::enable_if_t<!has_constexpr_type<T>::value>> {
  using type = V<T>;
};

template <typename T>
using make_const_or_v_t = typename make_const_or_v<T, void>::type;

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
  using const_or_values_t = std::tuple<detail::make_const_or_v_t<Ts>...>;
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
  base::Optional<values_t> pending_loop_phis_;
};

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

  base::Optional<V<T>> object_;
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
#define TURBOSHAFT_REDUCER_GENERIC_BOILERPLATE(Name)                       \
  using ReducerList = typename Next::ReducerList;                          \
  Assembler<ReducerList>& Asm() {                                          \
    return *static_cast<Assembler<ReducerList>*>(this);                    \
  }                                                                        \
  template <class T>                                                       \
  using ScopedVar = turboshaft::ScopedVariable<T, Assembler<ReducerList>>; \
  using CatchScope = CatchScopeImpl<Assembler<ReducerList>>;               \
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
  template <typename U, typename = std::enable_if_t<v_traits<
                            T>::template implicitly_convertible_to<U>::value>>
  operator V<U>() const {
    return Get();
  }
  template <typename U, typename = std::enable_if_t<v_traits<
                            T>::template implicitly_convertible_to<U>::value>>
  operator OptionalV<U>() const {
    return Get();
  }
  template <typename U,
            typename = std::enable_if_t<
                const_or_v_exists_v<U> &&
                v_traits<T>::template implicitly_convertible_to<U>::value>>
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

  OpIndex ReduceCatchBlockBegin() {
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
  OpIndex WrapInTupleIfNeeded(const Op& op, OpIndex idx) {
    if (op.outputs_rep().size() > 1) {
      base::SmallVector<OpIndex, 8> projections;
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

// This empty base-class is used to provide default-implementations of plain
// methods emitting operations.
template <class Next>
class ReducerBaseForwarder : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(ReducerBaseForwarder)

#define EMIT_OP(Name)                                                    \
  OpIndex ReduceInputGraph##Name(OpIndex ig_index, const Name##Op& op) { \
    return this->Asm().AssembleOutputGraph##Name(op);                    \
  }                                                                      \
  template <class... Args>                                               \
  OpIndex Reduce##Name(Args... args) {                                   \
    return this->Asm().template Emit<Name##Op>(args...);                 \
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
        base::VectorOf(
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

  OpIndex REDUCE(Goto)(Block* destination, bool is_backedge) {
    // Calling Base::Goto will call Emit<Goto>, which will call FinalizeBlock,
    // which will reset {current_block_}. We thus save {current_block_} before
    // calling Base::Goto, as we'll need it for AddPredecessor. Note also that
    // AddPredecessor might introduce some new blocks/operations if it needs to
    // split an edge, which means that it has to run after Base::Goto
    // (otherwise, the current Goto could be inserted in the wrong block).
    Block* saved_current_block = Asm().current_block();
    OpIndex new_opindex = Base::ReduceGoto(destination, is_backedge);
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

  OpIndex REDUCE(CatchBlockBegin)() {
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
      OpIndex catch_begin = predecessor->begin();
      DCHECK(Asm().Get(catch_begin).template Is<CatchBlockBeginOp>());
      phi_inputs.push_back(catch_begin);
    }
    return Asm().Phi(base::VectorOf(phi_inputs),
                     RegisterRepresentation::Tagged());
  }

  OpIndex REDUCE(Switch)(OpIndex input, base::Vector<SwitchOp::Case> cases,
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
    OpIndex new_opindex =
        Base::ReduceSwitch(input, cases, default_case, default_hint);
    for (SwitchOp::Case c : cases) {
      Asm().AddPredecessor(saved_current_block, c.destination, true);
    }
    Asm().AddPredecessor(saved_current_block, default_case, true);
    return new_opindex;
  }

  OpIndex REDUCE(Call)(OpIndex callee, OptionalOpIndex frame_state,
                       base::Vector<const OpIndex> arguments,
                       const TSCallDescriptor* descriptor, OpEffects effects) {
    OpIndex raw_call =
        Base::ReduceCall(callee, frame_state, arguments, descriptor, effects);
    bool has_catch_block = false;
    if (descriptor->can_throw == CanThrow::kYes) {
      has_catch_block = CatchIfInCatchScope(raw_call);
    }
    return ReduceDidntThrow(raw_call, has_catch_block, &descriptor->out_reps);
  }

 private:
  // These reduce functions are private, as they should only be emitted
  // automatically by `CatchIfInCatchScope` and `DoNotCatch` defined below and
  // never explicitly.
  using Base::ReduceDidntThrow;
  OpIndex REDUCE(CheckException)(OpIndex throwing_operation, Block* successor,
                                 Block* catch_block) {
    // {successor} and {catch_block} should never be the same.  AddPredecessor
    // and SplitEdge rely on this.
    DCHECK_NE(successor, catch_block);
    Block* saved_current_block = Asm().current_block();
    OpIndex new_opindex =
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
  OpIndex REDUCE(Projection)(OpIndex tuple, uint16_t index,
                             RegisterRepresentation rep) {
    if (auto* tuple_op = Asm().matcher().template TryCast<TupleOp>(tuple)) {
      return tuple_op->input(index);
    }
    return Next::ReduceProjection(tuple, index, rep);
  }

  // Methods to be used by the reducers to reducer operations with the whole
  // reducer stack.

  V<Object> GenericBinop(V<Object> left, V<Object> right, OpIndex frame_state,
                         OpIndex context, GenericBinopOp::Kind kind) {
    return ReduceIfReachableGenericBinop(left, right, frame_state, context,
                                         kind);
  }
#define DECL_GENERIC_BINOP(Name)                                  \
  V<Object> Generic##Name(V<Object> left, V<Object> right,        \
                          OpIndex frame_state, OpIndex context) { \
    return GenericBinop(left, right, frame_state, context,        \
                        GenericBinopOp::Kind::k##Name);           \
  }
  GENERIC_BINOP_LIST(DECL_GENERIC_BINOP)
#undef DECL_GENERIC_BINOP

  V<Object> GenericUnop(V<Object> input, OpIndex frame_state, OpIndex context,
                        GenericUnopOp::Kind kind) {
    return ReduceIfReachableGenericUnop(input, frame_state, context, kind);
  }
#define DECL_GENERIC_UNOP(Name)                                 \
  V<Object> Generic##Name(V<Object> input, OpIndex frame_state, \
                          OpIndex context) {                    \
    return GenericUnop(input, frame_state, context,             \
                       GenericUnopOp::Kind::k##Name);           \
  }
  GENERIC_UNOP_LIST(DECL_GENERIC_UNOP)
#undef DECL_GENERIC_UNOP

  V<Object> ToNumberOrNumeric(V<Object> input, OpIndex frame_state,
                              OpIndex context, Object::Conversion kind) {
    return ReduceIfReachableToNumberOrNumeric(input, frame_state, context,
                                              kind);
  }
  V<Object> ToNumber(V<Object> input, OpIndex frame_state, OpIndex context) {
    return ToNumberOrNumeric(input, frame_state, context,
                             Object::Conversion::kToNumber);
  }
  V<Object> ToNumeric(V<Object> input, OpIndex frame_state, OpIndex context) {
    return ToNumberOrNumeric(input, frame_state, context,
                             Object::Conversion::kToNumeric);
  }

#define DECL_MULTI_REP_BINOP(name, operation, rep_type, kind)               \
  OpIndex name(OpIndex left, OpIndex right, rep_type rep) {                 \
    return ReduceIfReachable##operation(left, right,                        \
                                        operation##Op::Kind::k##kind, rep); \
  }

#define DECL_SINGLE_REP_BINOP_V(name, operation, kind, tag)            \
  V<tag> name(ConstOrV<tag> left, ConstOrV<tag> right) {               \
    return ReduceIfReachable##operation(resolve(left), resolve(right), \
                                        operation##Op::Kind::k##kind,  \
                                        V<tag>::rep);                  \
  }
  DECL_MULTI_REP_BINOP(WordAdd, WordBinop, WordRepresentation, Add)
  DECL_SINGLE_REP_BINOP_V(Word32Add, WordBinop, Add, Word32)
  DECL_SINGLE_REP_BINOP_V(Word64Add, WordBinop, Add, Word64)
  DECL_SINGLE_REP_BINOP_V(WordPtrAdd, WordBinop, Add, WordPtr)

  DECL_MULTI_REP_BINOP(WordMul, WordBinop, WordRepresentation, Mul)
  DECL_SINGLE_REP_BINOP_V(Word32Mul, WordBinop, Mul, Word32)
  DECL_SINGLE_REP_BINOP_V(Word64Mul, WordBinop, Mul, Word64)
  DECL_SINGLE_REP_BINOP_V(WordPtrMul, WordBinop, Mul, WordPtr)

  DECL_MULTI_REP_BINOP(WordBitwiseAnd, WordBinop, WordRepresentation,
                       BitwiseAnd)
  DECL_SINGLE_REP_BINOP_V(Word32BitwiseAnd, WordBinop, BitwiseAnd, Word32)
  DECL_SINGLE_REP_BINOP_V(Word64BitwiseAnd, WordBinop, BitwiseAnd, Word64)
  DECL_SINGLE_REP_BINOP_V(WordPtrBitwiseAnd, WordBinop, BitwiseAnd, WordPtr)

  DECL_MULTI_REP_BINOP(WordBitwiseOr, WordBinop, WordRepresentation, BitwiseOr)
  DECL_SINGLE_REP_BINOP_V(Word32BitwiseOr, WordBinop, BitwiseOr, Word32)
  DECL_SINGLE_REP_BINOP_V(Word64BitwiseOr, WordBinop, BitwiseOr, Word64)
  DECL_SINGLE_REP_BINOP_V(WordPtrBitwiseOr, WordBinop, BitwiseOr, WordPtr)

  DECL_MULTI_REP_BINOP(WordBitwiseXor, WordBinop, WordRepresentation,
                       BitwiseXor)
  DECL_SINGLE_REP_BINOP_V(Word32BitwiseXor, WordBinop, BitwiseXor, Word32)
  DECL_SINGLE_REP_BINOP_V(Word64BitwiseXor, WordBinop, BitwiseXor, Word64)

  DECL_MULTI_REP_BINOP(WordSub, WordBinop, WordRepresentation, Sub)
  DECL_SINGLE_REP_BINOP_V(Word32Sub, WordBinop, Sub, Word32)
  DECL_SINGLE_REP_BINOP_V(Word64Sub, WordBinop, Sub, Word64)
  DECL_SINGLE_REP_BINOP_V(WordPtrSub, WordBinop, Sub, WordPtr)

  DECL_MULTI_REP_BINOP(IntDiv, WordBinop, WordRepresentation, SignedDiv)
  DECL_SINGLE_REP_BINOP_V(Int32Div, WordBinop, SignedDiv, Word32)
  DECL_SINGLE_REP_BINOP_V(Int64Div, WordBinop, SignedDiv, Word64)
  DECL_MULTI_REP_BINOP(UintDiv, WordBinop, WordRepresentation, UnsignedDiv)
  DECL_SINGLE_REP_BINOP_V(Uint32Div, WordBinop, UnsignedDiv, Word32)
  DECL_SINGLE_REP_BINOP_V(Uint64Div, WordBinop, UnsignedDiv, Word64)
  DECL_MULTI_REP_BINOP(IntMod, WordBinop, WordRepresentation, SignedMod)
  DECL_SINGLE_REP_BINOP_V(Int32Mod, WordBinop, SignedMod, Word32)
  DECL_SINGLE_REP_BINOP_V(Int64Mod, WordBinop, SignedMod, Word64)
  DECL_MULTI_REP_BINOP(UintMod, WordBinop, WordRepresentation, UnsignedMod)
  DECL_SINGLE_REP_BINOP_V(Uint32Mod, WordBinop, UnsignedMod, Word32)
  DECL_SINGLE_REP_BINOP_V(Uint64Mod, WordBinop, UnsignedMod, Word64)
  DECL_MULTI_REP_BINOP(IntMulOverflownBits, WordBinop, WordRepresentation,
                       SignedMulOverflownBits)
  DECL_SINGLE_REP_BINOP_V(Int32MulOverflownBits, WordBinop,
                          SignedMulOverflownBits, Word32)
  DECL_SINGLE_REP_BINOP_V(Int64MulOverflownBits, WordBinop,
                          SignedMulOverflownBits, Word64)
  DECL_MULTI_REP_BINOP(UintMulOverflownBits, WordBinop, WordRepresentation,
                       UnsignedMulOverflownBits)
  DECL_SINGLE_REP_BINOP_V(Uint32MulOverflownBits, WordBinop,
                          UnsignedMulOverflownBits, Word32)
  DECL_SINGLE_REP_BINOP_V(Uint64MulOverflownBits, WordBinop,
                          UnsignedMulOverflownBits, Word64)

  OpIndex WordBinop(OpIndex left, OpIndex right, WordBinopOp::Kind kind,
                    WordRepresentation rep) {
    return ReduceIfReachableWordBinop(left, right, kind, rep);
  }
  OpIndex OverflowCheckedBinop(OpIndex left, OpIndex right,
                               OverflowCheckedBinopOp::Kind kind,
                               WordRepresentation rep) {
    return ReduceIfReachableOverflowCheckedBinop(left, right, kind, rep);
  }
  DECL_MULTI_REP_BINOP(IntAddCheckOverflow, OverflowCheckedBinop,
                       WordRepresentation, SignedAdd)
  DECL_SINGLE_REP_BINOP_V(Int32AddCheckOverflow, OverflowCheckedBinop,
                          SignedAdd, Word32)
  DECL_SINGLE_REP_BINOP_V(Int64AddCheckOverflow, OverflowCheckedBinop,
                          SignedAdd, Word64)
  DECL_MULTI_REP_BINOP(IntSubCheckOverflow, OverflowCheckedBinop,
                       WordRepresentation, SignedSub)
  DECL_SINGLE_REP_BINOP_V(Int32SubCheckOverflow, OverflowCheckedBinop,
                          SignedSub, Word32)
  DECL_SINGLE_REP_BINOP_V(Int64SubCheckOverflow, OverflowCheckedBinop,
                          SignedSub, Word64)
  DECL_MULTI_REP_BINOP(IntMulCheckOverflow, OverflowCheckedBinop,
                       WordRepresentation, SignedMul)
  DECL_SINGLE_REP_BINOP_V(Int32MulCheckOverflow, OverflowCheckedBinop,
                          SignedMul, Word32)
  DECL_SINGLE_REP_BINOP_V(Int64MulCheckOverflow, OverflowCheckedBinop,
                          SignedMul, Word64)

  DECL_MULTI_REP_BINOP(FloatAdd, FloatBinop, FloatRepresentation, Add)
  DECL_SINGLE_REP_BINOP_V(Float32Add, FloatBinop, Add, Float32)
  DECL_SINGLE_REP_BINOP_V(Float64Add, FloatBinop, Add, Float64)
  DECL_MULTI_REP_BINOP(FloatMul, FloatBinop, FloatRepresentation, Mul)
  DECL_SINGLE_REP_BINOP_V(Float32Mul, FloatBinop, Mul, Float32)
  DECL_SINGLE_REP_BINOP_V(Float64Mul, FloatBinop, Mul, Float64)
  DECL_MULTI_REP_BINOP(FloatSub, FloatBinop, FloatRepresentation, Sub)
  DECL_SINGLE_REP_BINOP_V(Float32Sub, FloatBinop, Sub, Float32)
  DECL_SINGLE_REP_BINOP_V(Float64Sub, FloatBinop, Sub, Float64)
  DECL_MULTI_REP_BINOP(FloatDiv, FloatBinop, FloatRepresentation, Div)
  DECL_SINGLE_REP_BINOP_V(Float32Div, FloatBinop, Div, Float32)
  DECL_SINGLE_REP_BINOP_V(Float64Div, FloatBinop, Div, Float64)
  DECL_MULTI_REP_BINOP(FloatMin, FloatBinop, FloatRepresentation, Min)
  DECL_SINGLE_REP_BINOP_V(Float32Min, FloatBinop, Min, Float32)
  DECL_SINGLE_REP_BINOP_V(Float64Min, FloatBinop, Min, Float64)
  DECL_MULTI_REP_BINOP(FloatMax, FloatBinop, FloatRepresentation, Max)
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
  DECL_SINGLE_REP_BINOP_V(WordPtrShiftRightLogical, Shift, ShiftRightLogical,
                          WordPtr)
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

  V<Word32> Equal(OpIndex left, OpIndex right, RegisterRepresentation rep) {
    return Comparison(left, right, ComparisonOp::Kind::kEqual, rep);
  }

  V<Word32> TaggedEqual(V<Object> left, V<Object> right) {
    return Equal(left, right, RegisterRepresentation::Tagged());
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

  OpIndex FloatUnary(OpIndex input, FloatUnaryOp::Kind kind,
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
#define DECL_SINGLE_REP_UNARY_V(name, operation, kind, tag)         \
  V<tag> name(ConstOrV<tag> input) {                                \
    return ReduceIfReachable##operation(                            \
        resolve(input), operation##Op::Kind::k##kind, V<tag>::rep); \
  }

  DECL_MULTI_REP_UNARY(FloatAbs, FloatUnary, FloatRepresentation, Abs)
  DECL_SINGLE_REP_UNARY_V(Float32Abs, FloatUnary, Abs, Float32)
  DECL_SINGLE_REP_UNARY_V(Float64Abs, FloatUnary, Abs, Float64)
  DECL_MULTI_REP_UNARY(FloatNegate, FloatUnary, FloatRepresentation, Negate)
  DECL_SINGLE_REP_UNARY_V(Float32Negate, FloatUnary, Negate, Float32)
  DECL_SINGLE_REP_UNARY_V(Float64Negate, FloatUnary, Negate, Float64)
  DECL_SINGLE_REP_UNARY_V(Float64SilenceNaN, FloatUnary, SilenceNaN, Float64)
  DECL_MULTI_REP_UNARY(FloatRoundDown, FloatUnary, FloatRepresentation,
                       RoundDown)
  DECL_SINGLE_REP_UNARY_V(Float32RoundDown, FloatUnary, RoundDown, Float32)
  DECL_SINGLE_REP_UNARY_V(Float64RoundDown, FloatUnary, RoundDown, Float64)
  DECL_MULTI_REP_UNARY(FloatRoundUp, FloatUnary, FloatRepresentation, RoundUp)
  DECL_SINGLE_REP_UNARY_V(Float32RoundUp, FloatUnary, RoundUp, Float32)
  DECL_SINGLE_REP_UNARY_V(Float64RoundUp, FloatUnary, RoundUp, Float64)
  DECL_MULTI_REP_UNARY(FloatRoundToZero, FloatUnary, FloatRepresentation,
                       RoundToZero)
  DECL_SINGLE_REP_UNARY_V(Float32RoundToZero, FloatUnary, RoundToZero, Float32)
  DECL_SINGLE_REP_UNARY_V(Float64RoundToZero, FloatUnary, RoundToZero, Float64)
  DECL_MULTI_REP_UNARY(FloatRoundTiesEven, FloatUnary, FloatRepresentation,
                       RoundTiesEven)
  DECL_SINGLE_REP_UNARY_V(Float32RoundTiesEven, FloatUnary, RoundTiesEven,
                          Float32)
  DECL_SINGLE_REP_UNARY_V(Float64RoundTiesEven, FloatUnary, RoundTiesEven,
                          Float64)
  DECL_SINGLE_REP_UNARY_V(Float64Log, FloatUnary, Log, Float64)
  DECL_MULTI_REP_UNARY(FloatSqrt, FloatUnary, FloatRepresentation, Sqrt)
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

  DECL_MULTI_REP_UNARY(WordReverseBytes, WordUnary, WordRepresentation,
                       ReverseBytes)
  DECL_SINGLE_REP_UNARY_V(Word32ReverseBytes, WordUnary, ReverseBytes, Word32)
  DECL_SINGLE_REP_UNARY_V(Word64ReverseBytes, WordUnary, ReverseBytes, Word64)
  DECL_MULTI_REP_UNARY(WordCountLeadingZeros, WordUnary, WordRepresentation,
                       CountLeadingZeros)
  DECL_SINGLE_REP_UNARY_V(Word32CountLeadingZeros, WordUnary, CountLeadingZeros,
                          Word32)
  DECL_SINGLE_REP_UNARY_V(Word64CountLeadingZeros, WordUnary, CountLeadingZeros,
                          Word64)
  DECL_MULTI_REP_UNARY(WordCountTrailingZeros, WordUnary, WordRepresentation,
                       CountTrailingZeros)
  DECL_SINGLE_REP_UNARY_V(Word32CountTrailingZeros, WordUnary,
                          CountTrailingZeros, Word32)
  DECL_SINGLE_REP_UNARY_V(Word64CountTrailingZeros, WordUnary,
                          CountTrailingZeros, Word64)
  DECL_MULTI_REP_UNARY(WordPopCount, WordUnary, WordRepresentation, PopCount)
  DECL_SINGLE_REP_UNARY_V(Word32PopCount, WordUnary, PopCount, Word32)
  DECL_SINGLE_REP_UNARY_V(Word64PopCount, WordUnary, PopCount, Word64)
  DECL_MULTI_REP_UNARY(WordSignExtend8, WordUnary, WordRepresentation,
                       SignExtend8)
  DECL_SINGLE_REP_UNARY_V(Word32SignExtend8, WordUnary, SignExtend8, Word32)
  DECL_SINGLE_REP_UNARY_V(Word64SignExtend8, WordUnary, SignExtend8, Word64)
  DECL_MULTI_REP_UNARY(WordSignExtend16, WordUnary, WordRepresentation,
                       SignExtend16)
  DECL_SINGLE_REP_UNARY_V(Word32SignExtend16, WordUnary, SignExtend16, Word32)
  DECL_SINGLE_REP_UNARY_V(Word64SignExtend16, WordUnary, SignExtend16, Word64)
#undef DECL_SINGLE_REP_UNARY_V
#undef DECL_MULTI_REP_UNARY

  OpIndex WordBinopDeoptOnOverflow(OpIndex left, OpIndex right,
                                   OpIndex frame_state,
                                   WordBinopDeoptOnOverflowOp::Kind kind,
                                   WordRepresentation rep,
                                   FeedbackSource feedback,
                                   CheckForMinusZeroMode mode) {
    return ReduceIfReachableWordBinopDeoptOnOverflow(left, right, frame_state,
                                                     kind, rep, feedback, mode);
  }
#define DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(operation, rep_type)             \
  OpIndex rep_type##operation##DeoptOnOverflow(                               \
      ConstOrV<rep_type> left, ConstOrV<rep_type> right, OpIndex frame_state, \
      FeedbackSource feedback,                                                \
      CheckForMinusZeroMode mode =                                            \
          CheckForMinusZeroMode::kDontCheckForMinusZero) {                    \
    return WordBinopDeoptOnOverflow(                                          \
        resolve(left), resolve(right), frame_state,                           \
        WordBinopDeoptOnOverflowOp::Kind::k##operation,                       \
        WordRepresentation::rep_type(), feedback, mode);                      \
  }

  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedAdd, Word32)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedAdd, Word64)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedSub, Word32)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedSub, Word64)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedMul, Word32)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedMul, Word64)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedDiv, Word32)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedDiv, Word64)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedMod, Word32)
  DECL_SINGLE_REP_BINOP_DEOPT_OVERFLOW(SignedMod, Word64)
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
  V<Word32> ObjectIsSmi(V<Object> object) {
    return ObjectIs(object, ObjectIsOp::Kind::kSmi,
                    ObjectIsOp::InputAssumptions::kNone);
  }

  V<Word32> FloatIs(OpIndex input, NumericKind kind,
                    FloatRepresentation input_rep) {
    return ReduceIfReachableFloatIs(input, kind, input_rep);
  }
  V<Word32> Float64IsNaN(V<Float64> input) {
    return FloatIs(input, NumericKind::kNaN, FloatRepresentation::Float64());
  }
  V<Word32> Float64IsHole(V<Float64> input) {
    return FloatIs(input, NumericKind::kFloat64Hole,
                   FloatRepresentation::Float64());
  }
  // Float64IsSmi returns true if {input} is an integer in smi range.
  V<Word32> Float64IsSmi(V<Float64> input) {
    return FloatIs(input, NumericKind::kSmi, FloatRepresentation::Float64());
  }

  OpIndex ObjectIsNumericValue(OpIndex input, NumericKind kind,
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

  V<Object> ConvertUntaggedToJSPrimitive(
      OpIndex input, ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind kind,
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

  OpIndex ConvertUntaggedToJSPrimitiveOrDeopt(
      OpIndex input, OpIndex frame_state,
      ConvertUntaggedToJSPrimitiveOrDeoptOp::JSPrimitiveKind kind,
      RegisterRepresentation input_rep,
      ConvertUntaggedToJSPrimitiveOrDeoptOp::InputInterpretation
          input_interpretation,
      const FeedbackSource& feedback) {
    return ReduceIfReachableConvertUntaggedToJSPrimitiveOrDeopt(
        input, frame_state, kind, input_rep, input_interpretation, feedback);
  }

  OpIndex ConvertJSPrimitiveToUntagged(
      V<Object> object, ConvertJSPrimitiveToUntaggedOp::UntaggedKind kind,
      ConvertJSPrimitiveToUntaggedOp::InputAssumptions input_assumptions) {
    return ReduceIfReachableConvertJSPrimitiveToUntagged(object, kind,
                                                         input_assumptions);
  }

  OpIndex ConvertJSPrimitiveToUntaggedOrDeopt(
      V<Object> object, OpIndex frame_state,
      ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind from_kind,
      ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind to_kind,
      CheckForMinusZeroMode minus_zero_mode, const FeedbackSource& feedback) {
    return ReduceIfReachableConvertJSPrimitiveToUntaggedOrDeopt(
        object, frame_state, from_kind, to_kind, minus_zero_mode, feedback);
  }
  V<Word32> CheckedSmiUntag(V<Object> object, OpIndex frame_state,
                            const FeedbackSource& feedback) {
    return ConvertJSPrimitiveToUntaggedOrDeopt(
        object, frame_state,
        ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::kSmi,
        ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kInt32,
        CheckForMinusZeroMode::kDontCheckForMinusZero, feedback);
  }

  OpIndex TruncateJSPrimitiveToUntagged(
      V<Object> object, TruncateJSPrimitiveToUntaggedOp::UntaggedKind kind,
      TruncateJSPrimitiveToUntaggedOp::InputAssumptions input_assumptions) {
    return ReduceIfReachableTruncateJSPrimitiveToUntagged(object, kind,
                                                          input_assumptions);
  }

  OpIndex TruncateJSPrimitiveToUntaggedOrDeopt(
      V<Object> object, OpIndex frame_state,
      TruncateJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind kind,
      TruncateJSPrimitiveToUntaggedOrDeoptOp::InputRequirement
          input_requirement,
      const FeedbackSource& feedback) {
    return ReduceIfReachableTruncateJSPrimitiveToUntaggedOrDeopt(
        object, frame_state, kind, input_requirement, feedback);
  }

  V<Object> ConvertJSPrimitiveToObject(V<Object> value,
                                       V<Object> native_context,
                                       OptionalV<Object> global_proxy,
                                       ConvertReceiverMode mode) {
    return ReduceIfReachableConvertJSPrimitiveToObject(value, native_context,
                                                       global_proxy, mode);
  }
  V<Object> ConvertJSPrimitiveToObject(V<Object> value,
                                       V<Object> native_context,
                                       ConvertReceiverMode mode) {
    return ConvertJSPrimitiveToObject(value, native_context, OpIndex::Invalid(),
                                      mode);
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
    return WordConstant(value, WordRepresentation::WordPtr());
  }
  OpIndex WordConstant(uint64_t value, WordRepresentation rep) {
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
  V<WordPtr> UintPtrConstant(uintptr_t value) {
    return WordConstant(static_cast<uint64_t>(value),
                        WordRepresentation::WordPtr());
  }
  V<Smi> SmiConstant(intptr_t value) {
    return SmiConstant(i::Tagged<Smi>(value));
  }
  V<Smi> SmiConstant(i::Tagged<Smi> value) {
    return V<Smi>::Cast(
        ReduceIfReachableConstant(ConstantOp::Kind::kSmi, value));
  }
  V<Float32> Float32Constant(float value) {
    return ReduceIfReachableConstant(ConstantOp::Kind::kFloat32, value);
  }
  V<Float64> Float64Constant(double value) {
    return ReduceIfReachableConstant(ConstantOp::Kind::kFloat64, value);
  }
  OpIndex FloatConstant(double value, FloatRepresentation rep) {
    switch (rep.value()) {
      case FloatRepresentation::Float32():
        return Float32Constant(static_cast<float>(value));
      case FloatRepresentation::Float64():
        return Float64Constant(value);
    }
  }
  OpIndex NumberConstant(double value) {
    return ReduceIfReachableConstant(ConstantOp::Kind::kNumber, value);
  }
  OpIndex TaggedIndexConstant(int32_t value) {
    return ReduceIfReachableConstant(ConstantOp::Kind::kTaggedIndex,
                                     uint64_t{static_cast<uint32_t>(value)});
  }
  template <typename T,
            typename = std::enable_if_t<is_subtype_v<T, HeapObject>>>
  V<T> HeapConstant(Handle<T> value) {
    return ReduceIfReachableConstant(ConstantOp::Kind::kHeapObject,
                                     ConstantOp::Storage{value});
  }
  V<Code> BuiltinCode(Builtin builtin, Isolate* isolate) {
    return HeapConstant(BuiltinCodeHandle(builtin, isolate));
  }
  OpIndex CompressedHeapConstant(Handle<HeapObject> value) {
    return ReduceIfReachableConstant(ConstantOp::Kind::kHeapObject, value);
  }
  OpIndex ExternalConstant(ExternalReference value) {
    return ReduceIfReachableConstant(ConstantOp::Kind::kExternal, value);
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

  V<Context> NoContextConstant() {
    return V<Context>::Cast(TagSmi(Context::kNoContext));
  }
  // TODO(nicohartmann@): Might want to get rid of the isolate when supporting
  // Wasm.
  V<Object> CEntryStubConstant(Isolate* isolate, int result_size,
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
  V<to> name(V<from> input) {                                         \
    return ReduceIfReachableTryChange(input, TryChangeOp::Kind::kind, \
                                      FloatRepresentation::from(),    \
                                      WordRepresentation::to());      \
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
  OpIndex ZeroExtendWord32ToRep(V<Word32> value, WordRepresentation rep) {
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

  OpIndex ChangeOrDeopt(OpIndex input, OpIndex frame_state,
                        ChangeOrDeoptOp::Kind kind,
                        CheckForMinusZeroMode minus_zero_mode,
                        const FeedbackSource& feedback) {
    return ReduceIfReachableChangeOrDeopt(input, frame_state, kind,
                                          minus_zero_mode, feedback);
  }

  V<Word32> ChangeFloat64ToInt32OrDeopt(V<Float64> input, OpIndex frame_state,
                                        CheckForMinusZeroMode minus_zero_mode,
                                        const FeedbackSource& feedback) {
    return ChangeOrDeopt(input, frame_state,
                         ChangeOrDeoptOp::Kind::kFloat64ToInt32,
                         minus_zero_mode, feedback);
  }
  V<Word64> ChangeFloat64ToInt64OrDeopt(V<Float64> input, OpIndex frame_state,
                                        CheckForMinusZeroMode minus_zero_mode,
                                        const FeedbackSource& feedback) {
    return ChangeOrDeopt(input, frame_state,
                         ChangeOrDeoptOp::Kind::kFloat64ToInt64,
                         minus_zero_mode, feedback);
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
                    RegisterRepresentation result_rep,
                    MemoryRepresentation input_rep,
                    MemoryAccessKind memory_access_kind) {
    DCHECK_NE(bin_op, AtomicRMWOp::BinOp::kCompareExchange);
    return ReduceIfReachableAtomicRMW(base, index, value, OpIndex::Invalid(),
                                      bin_op, result_rep, input_rep,
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
#if V8_ENABLE_SANDBOX
    static_assert(COMPRESS_POINTERS_BOOL);
    V<Word32> tagged = Load(base, index, kind, MemoryRepresentation::Uint32(),
                            offset, index.valid() ? element_size_log2 : 0);
    OpIndex trusted_cage_base =
        Load(LoadRootRegister(), LoadOp::Kind::RawAligned().Immutable(),
             MemoryRepresentation::UintPtr(),
             IsolateData::trusted_cage_base_offset());
    // The bit cast is needed to change the type of the node to Tagged. This is
    // necessary so that if this value gets spilled on the stack, then the GC
    // will process it.
    // TODO(clemensb): Can an addition instead of bitwise-or generate better
    // code?
    return BitcastWordPtrToTagged(
        WordPtrBitwiseOr(ChangeUint32ToUintPtr(tagged), trusted_cage_base));
#else
    return Load(base, index, LoadOp::Kind::TaggedBase(),
                MemoryRepresentation::TaggedPointer(), offset,
                index.valid() ? element_size_log2 : 0);
#endif  // V8_ENABLE_SANDBOX
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
  V<Rep> LoadField(V<Object> object, const FieldAccess& access) {
    DCHECK_EQ(access.base_is_tagged, BaseTaggedness::kTaggedBase);
    return LoadFieldImpl<Rep>(object, access);
  }

  template <typename Rep = Any>
  V<Rep> LoadField(V<WordPtr> raw_base, const FieldAccess& access) {
    DCHECK_EQ(access.base_is_tagged, BaseTaggedness::kUntaggedBase);
    return LoadFieldImpl<Rep>(raw_base, access);
  }

  template <typename Rep>
  V<Rep> LoadFieldImpl(OpIndex object, const FieldAccess& access) {
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

  template <typename T = Any, typename Base>
  V<T> LoadArrayBufferElement(V<Base> object, const ElementAccess& access,
                              V<WordPtr> index) {
    return LoadElement<T>(object, access, index, true);
  }
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

  template <typename Base>
  void InitializeArrayBufferElement(Uninitialized<Base>& object,
                                    const ElementAccess& access,
                                    V<WordPtr> index, V<Any> value) {
    StoreArrayBufferElement(object.object(), access, index, value);
  }
  template <typename Base>
  void InitializeNonArrayBufferElement(Uninitialized<Base>& object,
                                       const ElementAccess& access,
                                       V<WordPtr> index, V<Any> value) {
    StoreNonArrayBufferElement(object.object(), access, index, value);
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

  OpIndex DecodeExternalPointer(OpIndex handle, ExternalPointerTag tag) {
    return ReduceIfReachableDecodeExternalPointer(handle, tag);
  }

  void StackCheck(StackCheckOp::CheckOrigin origin,
                  StackCheckOp::CheckKind kind) {
    ReduceIfReachableStackCheck(origin, kind);
  }

  void Retain(OpIndex value) { ReduceIfReachableRetain(value); }

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
#ifdef V8_ENABLE_DIRECT_LOCAL
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

  OpIndex Select(OpIndex cond, OpIndex vtrue, OpIndex vfalse,
                 RegisterRepresentation rep, BranchHint hint,
                 SelectOp::Implementation implem) {
    return ReduceIfReachableSelect(cond, vtrue, vfalse, rep, hint, implem);
  }
#define DEF_SELECT(Rep)                                             \
  V<Rep> Rep##Select(ConstOrV<Word32> cond, ConstOrV<Rep> vtrue,    \
                     ConstOrV<Rep> vfalse) {                        \
    return Select(resolve(cond), resolve(vtrue), resolve(vfalse),   \
                  RegisterRepresentation::Rep(), BranchHint::kNone, \
                  SelectOp::Implementation::kCMove);                \
  }
  DEF_SELECT(Word32)
  DEF_SELECT(Word64)
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
  OpIndex OsrValue(int index) { return ReduceIfReachableOsrValue(index); }
  void Return(V<Word32> pop_count, base::Vector<const OpIndex> return_values) {
    ReduceIfReachableReturn(pop_count, return_values);
  }
  void Return(OpIndex result) {
    Return(Word32Constant(0), base::VectorOf({result}));
  }

  OpIndex Call(OpIndex callee, OpIndex frame_state,
               base::Vector<const OpIndex> arguments,
               const TSCallDescriptor* descriptor,
               OpEffects effects = OpEffects().CanCallAnything()) {
    return ReduceIfReachableCall(callee, frame_state, arguments, descriptor,
                                 effects);
  }
  OpIndex Call(OpIndex callee, std::initializer_list<OpIndex> arguments,
               const TSCallDescriptor* descriptor,
               OpEffects effects = OpEffects().CanCallAnything()) {
    return Call(callee, OpIndex::Invalid(), base::VectorOf(arguments),
                descriptor, effects);
  }

  template <typename Descriptor>
  std::enable_if_t<Descriptor::kNeedsFrameState && Descriptor::kNeedsContext,
                   detail::index_type_for_t<typename Descriptor::results_t>>
  CallBuiltin(Isolate* isolate, OpIndex frame_state, OpIndex context,
              const typename Descriptor::arguments_t& args) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return OpIndex::Invalid();
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
    return CallBuiltinImpl(
        isolate, Descriptor::kFunction, frame_state, base::VectorOf(arguments),
        Descriptor::Create(StubCallMode::kCallCodeObject,
                           Asm().output_graph().graph_zone()),
        Descriptor::kEffects);
  }

  template <typename Descriptor>
  std::enable_if_t<!Descriptor::kNeedsFrameState && Descriptor::kNeedsContext,
                   detail::index_type_for_t<typename Descriptor::results_t>>
  CallBuiltin(Isolate* isolate, OpIndex context,
              const typename Descriptor::arguments_t& args) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return OpIndex::Invalid();
    }
    DCHECK(context.valid());
    auto arguments = std::apply(
        [context](auto&&... as) {
          return base::SmallVector<
              OpIndex, std::tuple_size_v<typename Descriptor::arguments_t> + 1>{
              std::forward<decltype(as)>(as)..., context};
        },
        args);
    return CallBuiltinImpl(
        isolate, Descriptor::kFunction, OpIndex::Invalid(),
        base::VectorOf(arguments),
        Descriptor::Create(StubCallMode::kCallCodeObject,
                           Asm().output_graph().graph_zone()),
        Descriptor::kEffects);
  }
  template <typename Descriptor>
  std::enable_if_t<Descriptor::kNeedsFrameState && !Descriptor::kNeedsContext,
                   detail::index_type_for_t<typename Descriptor::results_t>>
  CallBuiltin(Isolate* isolate, OpIndex frame_state,
              const typename Descriptor::arguments_t& args) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return OpIndex::Invalid();
    }
    DCHECK(frame_state.valid());
    auto arguments = std::apply(
        [](auto&&... as) {
          return base::SmallVector<OpIndex, std::tuple_size_v<decltype(args)>>{
              std::forward<decltype(as)>(as)...};
        },
        args);
    return CallBuiltinImpl(
        isolate, Descriptor::kFunction, frame_state, base::VectorOf(arguments),
        Descriptor::Create(StubCallMode::kCallCodeObject,
                           Asm().output_graph().graph_zone()),
        Descriptor::kEffects);
  }
  template <typename Descriptor>
  std::enable_if_t<!Descriptor::kNeedsFrameState && !Descriptor::kNeedsContext,
                   detail::index_type_for_t<typename Descriptor::results_t>>
  CallBuiltin(Isolate* isolate, const typename Descriptor::arguments_t& args) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return OpIndex::Invalid();
    }
    auto arguments = std::apply(
        [](auto&&... as) {
          return base::SmallVector<
              OpIndex, std::tuple_size_v<typename Descriptor::arguments_t>>{
              std::forward<decltype(as)>(as)...};
        },
        args);
    return CallBuiltinImpl(
        isolate, Descriptor::kFunction, OpIndex::Invalid(),
        base::VectorOf(arguments),
        Descriptor::Create(StubCallMode::kCallCodeObject,
                           Asm().output_graph().graph_zone()),
        Descriptor::kEffects);
  }

#if V8_ENABLE_WEBASSEMBLY

  template <typename Descriptor>
  std::enable_if_t<!Descriptor::kNeedsContext,
                   detail::index_type_for_t<typename Descriptor::results_t>>
  WasmCallBuiltinThroughJumptable(
      const typename Descriptor::arguments_t& args) {
    static_assert(!Descriptor::kNeedsFrameState);
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return OpIndex::Invalid();
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
    return Call(call_target, OpIndex::Invalid(), base::VectorOf(arguments),
                Descriptor::Create(StubCallMode::kCallWasmRuntimeStub,
                                   Asm().output_graph().graph_zone()),
                Descriptor::kEffects);
  }

  template <typename Descriptor>
  std::enable_if_t<Descriptor::kNeedsContext,
                   detail::index_type_for_t<typename Descriptor::results_t>>
  WasmCallBuiltinThroughJumptable(
      OpIndex context, const typename Descriptor::arguments_t& args) {
    static_assert(!Descriptor::kNeedsFrameState);
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return OpIndex::Invalid();
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
    return Call(call_target, OpIndex::Invalid(), base::VectorOf(arguments),
                Descriptor::Create(StubCallMode::kCallWasmRuntimeStub,
                                   Asm().output_graph().graph_zone()),
                Descriptor::kEffects);
  }

#endif  // V8_ENABLE_WEBASSEMBLY

  OpIndex CallBuiltinImpl(Isolate* isolate, Builtin builtin,
                          OptionalOpIndex frame_state,
                          base::Vector<const OpIndex> arguments,
                          const TSCallDescriptor* desc, OpEffects effects) {
    Callable callable = Builtins::CallableFor(isolate, builtin);
    return Call(HeapConstant(callable.code()), frame_state.value_or_invalid(),
                arguments, desc, effects);
  }

#define DECL_GENERIC_BINOP_BUILTIN_CALL(Name)                         \
  V<Object> CallBuiltin_##Name(Isolate* isolate, OpIndex frame_state, \
                               V<Context> context, V<Object> lhs,     \
                               V<Object> rhs) {                       \
    return CallBuiltin<typename BuiltinCallDescriptor::Name>(         \
        isolate, frame_state, context, {lhs, rhs});                   \
  }
  GENERIC_BINOP_LIST(DECL_GENERIC_BINOP_BUILTIN_CALL)
#undef DECL_GENERIC_BINOP_BUILTIN_CALL

#define DECL_GENERIC_UNOP_BUILTIN_CALL(Name)                          \
  V<Object> CallBuiltin_##Name(Isolate* isolate, OpIndex frame_state, \
                               V<Context> context, V<Object> input) { \
    return CallBuiltin<typename BuiltinCallDescriptor::Name>(         \
        isolate, frame_state, context, {input});                      \
  }
  GENERIC_UNOP_LIST(DECL_GENERIC_UNOP_BUILTIN_CALL)
#undef DECL_GENERIC_UNOP_BUILTIN_CALL

  V<Number> CallBuiltin_ToNumber(Isolate* isolate, OpIndex frame_state,
                                 V<Context> context, V<Object> input) {
    return CallBuiltin<typename BuiltinCallDescriptor::ToNumber>(
        isolate, frame_state, context, {input});
  }
  V<Numeric> CallBuiltin_ToNumeric(Isolate* isolate, OpIndex frame_state,
                                   V<Context> context, V<Object> input) {
    return CallBuiltin<typename BuiltinCallDescriptor::ToNumeric>(
        isolate, frame_state, context, {input});
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
  V<Object> CallBuiltin_ToObject(Isolate* isolate, V<Context> context,
                                 V<Object> object) {
    return CallBuiltin<typename BuiltinCallDescriptor::ToObject>(
        isolate, context, {object});
  }
  V<String> CallBuiltin_Typeof(Isolate* isolate, V<Object> object) {
    return CallBuiltin<typename BuiltinCallDescriptor::Typeof>(isolate,
                                                               {object});
  }

  template <typename Descriptor>
  std::enable_if_t<Descriptor::kNeedsFrameState, typename Descriptor::result_t>
  CallRuntime(Isolate* isolate, OpIndex frame_state, OpIndex context,
              const typename Descriptor::arguments_t& args) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return OpIndex::Invalid();
    }
    DCHECK(frame_state.valid());
    DCHECK(context.valid());
    return CallRuntimeImpl<typename Descriptor::result_t>(
        isolate, Descriptor::kFunction,
        Descriptor::Create(Asm().output_graph().graph_zone()), frame_state,
        context, args);
  }
  template <typename Descriptor>
  std::enable_if_t<!Descriptor::kNeedsFrameState, typename Descriptor::result_t>
  CallRuntime(Isolate* isolate, OpIndex context,
              const typename Descriptor::arguments_t& args) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return OpIndex::Invalid();
    }
    DCHECK(context.valid());
    return CallRuntimeImpl<typename Descriptor::result_t>(
        isolate, Descriptor::kFunction,
        Descriptor::Create(Asm().output_graph().graph_zone()), {}, context,
        args);
  }

  template <typename Ret, typename Args>
  Ret CallRuntimeImpl(Isolate* isolate, Runtime::FunctionId function,
                      const TSCallDescriptor* desc, OpIndex frame_state,
                      OpIndex context, const Args& args) {
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
      return Call(CEntryStubConstant(isolate, result_size), frame_state,
                  base::VectorOf(inputs), desc);
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
  V<Object> CallRuntime_StackGuardWithGap(Isolate* isolate, V<Context> context,
                                          V<Smi> gap) {
    return CallRuntime<typename RuntimeCallDescriptor::StackGuardWithGap>(
        isolate, context, {gap});
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
  V<Object> CallRuntime_TerminateExecution(Isolate* isolate,
                                           OpIndex frame_state,
                                           V<Context> context) {
    return CallRuntime<typename RuntimeCallDescriptor::TerminateExecution>(
        isolate, frame_state, context, {});
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

  void TailCall(OpIndex callee, base::Vector<const OpIndex> arguments,
                const TSCallDescriptor* descriptor) {
    ReduceIfReachableTailCall(callee, arguments, descriptor);
  }

  OpIndex FrameState(base::Vector<const OpIndex> inputs, bool inlined,
                     const FrameStateData* data) {
    return ReduceIfReachableFrameState(inputs, inlined, data);
  }
  void DeoptimizeIf(V<Word32> condition, OpIndex frame_state,
                    const DeoptimizeParameters* parameters) {
    ReduceIfReachableDeoptimizeIf(condition, frame_state, false, parameters);
  }
  void DeoptimizeIfNot(V<Word32> condition, OpIndex frame_state,
                       const DeoptimizeParameters* parameters) {
    ReduceIfReachableDeoptimizeIf(condition, frame_state, true, parameters);
  }
  void DeoptimizeIf(V<Word32> condition, OpIndex frame_state,
                    DeoptimizeReason reason, const FeedbackSource& feedback) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return;
    }
    Zone* zone = Asm().output_graph().graph_zone();
    const DeoptimizeParameters* params =
        zone->New<DeoptimizeParameters>(reason, feedback);
    DeoptimizeIf(condition, frame_state, params);
  }
  void DeoptimizeIfNot(V<Word32> condition, OpIndex frame_state,
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
  void Deoptimize(OpIndex frame_state, const DeoptimizeParameters* parameters) {
    ReduceIfReachableDeoptimize(frame_state, parameters);
  }
  void Deoptimize(OpIndex frame_state, DeoptimizeReason reason,
                  const FeedbackSource& feedback) {
    if (V8_UNLIKELY(Asm().generating_unreachable_operations())) {
      return;
    }
    Zone* zone = Asm().output_graph().graph_zone();
    const DeoptimizeParameters* params =
        zone->New<DeoptimizeParameters>(reason, feedback);
    Deoptimize(frame_state, params);
  }

#if V8_ENABLE_WEBASSEMBLY
  void TrapIf(V<Word32> condition, OptionalOpIndex frame_state,
              TrapId trap_id) {
    ReduceIfReachableTrapIf(condition, frame_state, false, trap_id);
  }
  void TrapIfNot(V<Word32> condition, OptionalOpIndex frame_state,
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

  OpIndex Tuple(base::Vector<OpIndex> indices) {
    return ReduceIfReachableTuple(indices);
  }
  OpIndex Tuple(std::initializer_list<OpIndex> indices) {
    return ReduceIfReachableTuple(base::VectorOf(indices));
  }
  OpIndex Tuple(OpIndex a, OpIndex b) {
    return ReduceIfReachableTuple(base::VectorOf({a, b}));
  }
  OpIndex Projection(OpIndex tuple, uint16_t index,
                     RegisterRepresentation rep) {
    return ReduceIfReachableProjection(tuple, index, rep);
  }
  template <typename T>
  V<T> Projection(OpIndex tuple, uint16_t index) {
    return Projection(tuple, index, V<T>::rep);
  }
  OpIndex CheckTurboshaftTypeOf(OpIndex input, RegisterRepresentation rep,
                                Type expected_type, bool successful) {
    CHECK(v8_flags.turboshaft_enable_debug_features);
    return ReduceIfReachableCheckTurboshaftTypeOf(input, rep, expected_type,
                                                  successful);
  }

  OpIndex CatchBlockBegin() { return ReduceIfReachableCatchBlockBegin(); }

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
  ConditionalGotoStatus GotoIf(OpIndex condition, Block* if_true,
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
  ConditionalGotoStatus GotoIfNot(OpIndex condition, Block* if_false,
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

  OpIndex CallBuiltin(Builtin builtin, OpIndex frame_state,
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

    const TSCallDescriptor* ts_call_descriptor =
        TSCallDescriptor::Create(call_descriptor, can_throw, graph_zone);

    OpIndex callee = Asm().HeapConstant(callable.code());

    return Asm().Call(callee, frame_state, arguments, ts_call_descriptor);
  }

  V<Object> NewConsString(V<Word32> length, V<Object> first, V<Object> second) {
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

  void AssertImpl(V<Word32> condition, const char* condition_string,
                  const char* file, int line) {
#ifdef DEBUG
    // We use 256 characters as a buffer size. This can be increased if
    // necessary.
    static constexpr size_t kMaxAssertCommentLength = 256;
    base::Vector<char> buffer =
        PipelineData::Get().shared_zone()->AllocateVector<char>(
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
  void DebugPrint(V<WordPtr> input) {
    return DebugPrint(input, RegisterRepresentation::WordPtr());
  }
  void DebugPrint(V<Float64> input) {
    return DebugPrint(input, RegisterRepresentation::Float64());
  }

  void Comment(const char* message) { ReduceIfReachableComment(message); }

  V<Object> BigIntBinop(V<Object> left, V<Object> right, OpIndex frame_state,
                        BigIntBinopOp::Kind kind) {
    return ReduceIfReachableBigIntBinop(left, right, frame_state, kind);
  }
#define BIGINT_BINOP(kind)                                \
  V<Object> BigInt##kind(V<Object> left, V<Object> right, \
                         OpIndex frame_state) {           \
    return BigIntBinop(left, right, frame_state,          \
                       BigIntBinopOp::Kind::k##kind);     \
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

  V<Boolean> BigIntComparison(V<Object> left, V<Object> right,
                              BigIntComparisonOp::Kind kind) {
    return ReduceIfReachableBigIntComparison(left, right, kind);
  }
  V<Boolean> BigIntEqual(V<Object> left, V<Object> right) {
    return BigIntComparison(left, right, BigIntComparisonOp::Kind::kEqual);
  }
  V<Boolean> BigIntLessThan(V<Object> left, V<Object> right) {
    return BigIntComparison(left, right, BigIntComparisonOp::Kind::kLessThan);
  }
  V<Boolean> BigIntLessThanOrEqual(V<Object> left, V<Object> right) {
    return BigIntComparison(left, right,
                            BigIntComparisonOp::Kind::kLessThanOrEqual);
  }

  V<Object> BigIntUnary(V<Object> input, BigIntUnaryOp::Kind kind) {
    return ReduceIfReachableBigIntUnary(input, kind);
  }
  V<Object> BigIntNegate(V<Object> input) {
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
                            V<Word32> is_little_endian,
                            ExternalArrayType element_type) {
    ReduceIfReachableStoreDataViewElement(object, storage, index, value,
                                          is_little_endian, element_type);
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

  void CheckMaps(V<HeapObject> heap_object, OpIndex frame_state,
                 const ZoneRefSet<Map>& maps, CheckMapsFlags flags,
                 const FeedbackSource& feedback) {
    ReduceIfReachableCheckMaps(heap_object, frame_state, maps, flags, feedback);
  }

  void AssumeMap(V<HeapObject> heap_object, const ZoneRefSet<Map>& maps) {
    ReduceIfReachableAssumeMap(heap_object, maps);
  }

  V<Object> CheckedClosure(V<Object> input, OpIndex frame_state,
                           Handle<FeedbackCell> feedback_cell) {
    return ReduceIfReachableCheckedClosure(input, frame_state, feedback_cell);
  }

  void CheckEqualsInternalizedString(V<Object> expected, V<Object> value,
                                     OpIndex frame_state) {
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

  V<Word32> Float64SameValue(OpIndex left, OpIndex right) {
    return ReduceIfReachableFloat64SameValue(left, right);
  }

  OpIndex FastApiCall(OpIndex data_argument,
                      base::Vector<const OpIndex> arguments,
                      const FastApiCallParameters* parameters) {
    return ReduceIfReachableFastApiCall(data_argument, arguments, parameters);
  }

  void RuntimeAbort(AbortReason reason) {
    ReduceIfReachableRuntimeAbort(reason);
  }

  V<Object> EnsureWritableFastElements(V<Object> object, V<Object> elements) {
    return ReduceIfReachableEnsureWritableFastElements(object, elements);
  }

  V<Object> MaybeGrowFastElements(V<Object> object, V<Object> elements,
                                  V<Word32> index, V<Word32> elements_length,
                                  OpIndex frame_state,
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
                                   OpIndex frame_state,
                                   SpeculativeNumberBinopOp::Kind kind) {
    return ReduceIfReachableSpeculativeNumberBinop(left, right, frame_state,
                                                   kind);
  }

#ifdef V8_ENABLE_WEBASSEMBLY
  OpIndex GlobalGet(V<WasmTrustedInstanceData> trusted_instance_data,
                    const wasm::WasmGlobal* global) {
    return ReduceIfReachableGlobalGet(trusted_instance_data, global);
  }

  OpIndex GlobalSet(V<WasmTrustedInstanceData> trusted_instance_data,
                    OpIndex value, const wasm::WasmGlobal* global) {
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

  OpIndex AnnotateWasmType(OpIndex value, const wasm::ValueType type) {
    return ReduceIfReachableWasmTypeAnnotation(value, type);
  }

  OpIndex StructGet(V<HeapObject> object, const wasm::StructType* type,
                    uint32_t type_index, int field_index, bool is_signed,
                    CheckForNull null_check) {
    return ReduceIfReachableStructGet(object, type, type_index, field_index,
                                      is_signed, null_check);
  }

  void StructSet(V<HeapObject> object, OpIndex value,
                 const wasm::StructType* type, uint32_t type_index,
                 int field_index, CheckForNull null_check) {
    ReduceIfReachableStructSet(object, value, type, type_index, field_index,
                               null_check);
  }

  OpIndex ArrayGet(V<HeapObject> array, V<Word32> index,
                   const wasm::ArrayType* array_type, bool is_signed) {
    return ReduceIfReachableArrayGet(array, index, array_type, is_signed);
  }

  void ArraySet(V<HeapObject> array, V<Word32> index, OpIndex value,
                wasm::ValueType element_type) {
    ReduceIfReachableArraySet(array, index, value, element_type);
  }

  V<Word32> ArrayLength(V<HeapObject> array, CheckForNull null_check) {
    return ReduceIfReachableArrayLength(array, null_check);
  }

  V<HeapObject> WasmAllocateArray(V<Map> rtt, ConstOrV<Word32> length,
                                  const wasm::ArrayType* array_type) {
    return ReduceIfReachableWasmAllocateArray(rtt, resolve(length), array_type);
  }

  V<HeapObject> WasmAllocateStruct(V<Map> rtt,
                                   const wasm::StructType* struct_type) {
    return ReduceIfReachableWasmAllocateStruct(rtt, struct_type);
  }

  V<Object> WasmRefFunc(V<Object> wasm_instance, uint32_t function_index) {
    return ReduceIfReachableWasmRefFunc(wasm_instance, function_index);
  }

  V<Object> StringAsWtf16(V<Object> string) {
    return ReduceIfReachableStringAsWtf16(string);
  }

  V<Object> StringPrepareForGetCodeUnit(V<Object> string) {
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

  V<Simd128> Simd128Splat(OpIndex input, Simd128SplatOp::Kind kind) {
    return ReduceIfReachableSimd128Splat(input, kind);
  }

  V<Simd128> Simd128Ternary(OpIndex first, OpIndex second, OpIndex third,
                            Simd128TernaryOp::Kind kind) {
    return ReduceIfReachableSimd128Ternary(first, second, third, kind);
  }

  OpIndex Simd128ExtractLane(V<Simd128> input, Simd128ExtractLaneOp::Kind kind,
                             uint8_t lane) {
    return ReduceIfReachableSimd128ExtractLane(input, kind, lane);
  }

  V<Simd128> Simd128ReplaceLane(V<Simd128> into, OpIndex new_lane,
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

  OpIndex Simd128LoadTransform(
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

  V<Simd256> Simd256Binop(V<Simd256> left, V<Simd256> right,
                          Simd256BinopOp::Kind kind) {
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
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

  V<WasmTrustedInstanceData> WasmInstanceParameter() {
    return Parameter(wasm::kWasmInstanceParameterIndex,
                     RegisterRepresentation::Tagged());
  }

  OpIndex LoadStackPointer() { return ReduceIfReachableLoadStackPointer(); }

  void SetStackPointer(V<WordPtr> value, wasm::FPRelativeScope fp_scope) {
    ReduceIfReachableSetStackPointer(value, fp_scope);
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
                    V<WordPtr> index, V<Any> value, bool is_array_buffer) {
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
    Store(object, index, value, kind, rep, access.write_barrier_kind,
          access.header_size, rep.SizeInBytesLog2());
  }

  // BranchAndBind should be called from GotoIf/GotoIfNot. It will insert a
  // Branch, bind {to_bind} (which should correspond to the implicit new block
  // following the GotoIf/GotoIfNot) and return a ConditionalGotoStatus
  // representing whether the destinations of the Branch are reachable or not.
  ConditionalGotoStatus BranchAndBind(OpIndex condition, Block* if_true,
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
  AssemblerData(Graph& input_graph, Graph& output_graph, Zone* phase_zone)
      : phase_zone(phase_zone),
        input_graph(input_graph),
        output_graph(output_graph) {}
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
  explicit Assembler(Graph& input_graph, Graph& output_graph, Zone* phase_zone)
      : AssemblerData(input_graph, output_graph, phase_zone), Stack() {
    SupportedOperations::Initialize();
  }

  using Stack::Asm;

  Zone* phase_zone() { return AssemblerData::phase_zone; }
  const Graph& input_graph() const { return AssemblerData::input_graph; }
  Graph& output_graph() const { return AssemblerData::output_graph; }
  Zone* graph_zone() const { return output_graph().graph_zone(); }

  // Analyzers set Operations' saturated_use_count to zero when they are unused,
  // and thus need to have a non-const input graph.
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
  OpIndex current_operation_origin() const { return current_operation_origin_; }

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
  OpIndex current_operation_origin_ = OpIndex::Invalid();

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
