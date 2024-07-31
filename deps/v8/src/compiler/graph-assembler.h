// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_GRAPH_ASSEMBLER_H_
#define V8_COMPILER_GRAPH_ASSEMBLER_H_

#include <optional>
#include <type_traits>

#include "src/base/small-vector.h"
#include "src/codegen/tnode.h"
#include "src/common/globals.h"
#include "src/compiler/feedback-source.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/node.h"
#include "src/compiler/simplified-operator.h"
#include "src/objects/hole.h"
#include "src/objects/oddball.h"

namespace v8 {
namespace internal {

class JSGraph;
class Graph;

namespace compiler {

class Reducer;

#define PURE_ASSEMBLER_MACH_UNOP_LIST(V) \
  V(BitcastFloat32ToInt32)               \
  V(BitcastFloat64ToInt64)               \
  V(BitcastInt32ToFloat32)               \
  V(BitcastWord32ToWord64)               \
  V(BitcastInt64ToFloat64)               \
  V(ChangeFloat32ToFloat64)              \
  V(ChangeFloat64ToInt32)                \
  V(ChangeFloat64ToInt64)                \
  V(ChangeFloat64ToUint32)               \
  V(ChangeFloat64ToUint64)               \
  V(ChangeInt32ToFloat64)                \
  V(ChangeInt32ToInt64)                  \
  V(ChangeInt64ToFloat64)                \
  V(ChangeUint32ToFloat64)               \
  V(ChangeUint32ToUint64)                \
  V(Float64Abs)                          \
  V(Float64ExtractHighWord32)            \
  V(Float64ExtractLowWord32)             \
  V(Float64SilenceNaN)                   \
  V(RoundFloat64ToInt32)                 \
  V(RoundInt32ToFloat32)                 \
  V(TruncateFloat64ToFloat32)            \
  V(TruncateFloat64ToWord32)             \
  V(TruncateInt64ToInt32)                \
  V(TryTruncateFloat64ToInt64)           \
  V(TryTruncateFloat64ToUint64)          \
  V(TryTruncateFloat64ToInt32)           \
  V(TryTruncateFloat64ToUint32)          \
  V(Word32ReverseBytes)                  \
  V(Word64ReverseBytes)

#define PURE_ASSEMBLER_MACH_BINOP_LIST(V, T)        \
  V(Float64Add)                                     \
  V(Float64Div)                                     \
  V(Float64Equal)                                   \
  V(Float64InsertHighWord32)                        \
  V(Float64InsertLowWord32)                         \
  V(Float64LessThan)                                \
  V(Float64LessThanOrEqual)                         \
  V(Float64Max)                                     \
  V(Float64Min)                                     \
  V(Float64Mod)                                     \
  V(Float64Sub)                                     \
  V(Int32Add)                                       \
  V(Int32LessThan)                                  \
  T(Int32LessThanOrEqual, BoolT, Int32T, Int32T)    \
  V(Int32Mul)                                       \
  V(Int32Sub)                                       \
  V(Int64Add)                                       \
  V(Int64Sub)                                       \
  V(IntAdd)                                         \
  V(IntLessThan)                                    \
  V(IntMul)                                         \
  V(IntSub)                                         \
  T(Uint32LessThan, BoolT, Uint32T, Uint32T)        \
  T(Uint32LessThanOrEqual, BoolT, Uint32T, Uint32T) \
  T(Uint64LessThan, BoolT, Uint64T, Uint64T)        \
  T(Uint64LessThanOrEqual, BoolT, Uint64T, Uint64T) \
  V(UintLessThan)                                   \
  T(Word32And, Word32T, Word32T, Word32T)           \
  T(Word32Equal, BoolT, Word32T, Word32T)           \
  T(Word32Or, Word32T, Word32T, Word32T)            \
  V(Word32Sar)                                      \
  V(Word32SarShiftOutZeros)                         \
  V(Word32Shl)                                      \
  T(Word32Shr, Word32T, Word32T, Word32T)           \
  V(Word32Xor)                                      \
  V(Word64And)                                      \
  V(Word64Equal)                                    \
  V(Word64Or)                                       \
  V(Word64Sar)                                      \
  V(Word64SarShiftOutZeros)                         \
  V(Word64Shl)                                      \
  V(Word64Shr)                                      \
  V(Word64Xor)                                      \
  V(WordAnd)                                        \
  V(WordEqual)                                      \
  V(WordOr)                                         \
  V(WordSar)                                        \
  V(WordSarShiftOutZeros)                           \
  V(WordShl)                                        \
  V(WordShr)                                        \
  V(WordXor)

#define CHECKED_ASSEMBLER_MACH_BINOP_LIST(V) \
  V(Int32AddWithOverflow)                    \
  V(Int64AddWithOverflow)                    \
  V(Int32Div)                                \
  V(Int32Mod)                                \
  V(Int32MulWithOverflow)                    \
  V(Int64MulWithOverflow)                    \
  V(Int32SubWithOverflow)                    \
  V(Int64SubWithOverflow)                    \
  V(Int64Div)                                \
  V(Int64Mod)                                \
  V(Uint32Div)                               \
  V(Uint32Mod)                               \
  V(Uint64Div)                               \
  V(Uint64Mod)

#define JSGRAPH_SINGLETON_CONSTANT_LIST(V)                         \
  V(AllocateInOldGenerationStub, InstructionStream)                \
  V(AllocateInYoungGenerationStub, InstructionStream)              \
  IF_WASM(V, WasmAllocateInYoungGenerationStub, InstructionStream) \
  IF_WASM(V, WasmAllocateInOldGenerationStub, InstructionStream)   \
  V(BigIntMap, Map)                                                \
  V(BooleanMap, Map)                                               \
  V(EmptyString, String)                                           \
  V(ExternalObjectMap, Map)                                        \
  V(False, Boolean)                                                \
  V(FixedArrayMap, Map)                                            \
  V(FixedDoubleArrayMap, Map)                                      \
  V(WeakFixedArrayMap, Map)                                        \
  V(HeapNumberMap, Map)                                            \
  V(MinusOne, Number)                                              \
  V(NaN, Number)                                                   \
  V(NoContext, Object)                                             \
  V(Null, Null)                                                    \
  V(One, Number)                                                   \
  V(TheHole, Hole)                                                 \
  V(ToNumberBuiltin, InstructionStream)                            \
  V(PlainPrimitiveToNumberBuiltin, InstructionStream)              \
  V(True, Boolean)                                                 \
  V(Undefined, Undefined)                                          \
  V(Zero, Number)

class GraphAssembler;

enum class GraphAssemblerLabelType { kDeferred, kNonDeferred, kLoop };

namespace detail {
constexpr size_t kGraphAssemblerLabelDynamicCount = ~0u;

template <size_t VarCount>
struct GraphAssemblerHelper {
  template <typename T>
  using Array = std::array<T, VarCount>;
  static constexpr bool kIsDynamic = false;

  static Array<Node*> InitNodeArray(const Array<MachineRepresentation>& reps) {
    return {};
  }
};
template <>
struct GraphAssemblerHelper<kGraphAssemblerLabelDynamicCount> {
  // TODO(leszeks): We could allow other sizes of small vector here, by encoding
  // the size in the negative VarCount.
  template <typename T>
  using Array = base::SmallVector<T, 4>;
  static constexpr bool kIsDynamic = true;

  static Array<Node*> InitNodeArray(const Array<MachineRepresentation>& reps) {
    return Array<Node*>(reps.size());
  }
};
}  // namespace detail

// Label with statically known count of incoming branches and phis.
template <size_t VarCount>
class GraphAssemblerLabel {
  using Helper = detail::GraphAssemblerHelper<VarCount>;
  template <typename T>
  using Array = typename Helper::template Array<T>;
  static constexpr bool kIsDynamic = Helper::kIsDynamic;

 public:
  size_t Count() { return representations_.size(); }

  Node* PhiAt(size_t index);

  template <typename T>
  TNode<T> PhiAt(size_t index) {
    // TODO(jgruber): Investigate issues on ptr compression bots and enable.
    // DCHECK(IsMachineRepresentationOf<T>(representations_[index]));
    return TNode<T>::UncheckedCast(PhiAt(index));
  }

  bool IsUsed() const { return merged_count_ > 0; }

  GraphAssemblerLabel(GraphAssemblerLabelType type, int loop_nesting_level,
                      Array<MachineRepresentation> reps)
      : type_(type),
        loop_nesting_level_(loop_nesting_level),
        bindings_(Helper::InitNodeArray(reps)),
        representations_(std::move(reps)) {}

  ~GraphAssemblerLabel() { DCHECK(IsBound() || merged_count_ == 0); }

 private:
  friend class GraphAssembler;

  void SetBound() {
    DCHECK(!IsBound());
    is_bound_ = true;
  }
  bool IsBound() const { return is_bound_; }
  bool IsDeferred() const {
    return type_ == GraphAssemblerLabelType::kDeferred;
  }
  bool IsLoop() const { return type_ == GraphAssemblerLabelType::kLoop; }

  bool is_bound_ = false;
  const GraphAssemblerLabelType type_;
  const int loop_nesting_level_;
  size_t merged_count_ = 0;
  Node* effect_;
  Node* control_;
  Array<Node*> bindings_;
  const Array<MachineRepresentation> representations_;
};

using GraphAssemblerDynamicLabel =
    GraphAssemblerLabel<detail::kGraphAssemblerLabelDynamicCount>;

namespace detail {
template <typename T, typename Enable, typename... Us>
struct GraphAssemblerLabelForXHelper;

// If the Us are a template pack each assignable to T, use a static label.
template <typename T, typename... Us>
struct GraphAssemblerLabelForXHelper<
    T, std::enable_if_t<std::conjunction_v<std::is_assignable<T&, Us>...>>,
    Us...> {
  using Type = GraphAssemblerLabel<sizeof...(Us)>;
};

// If the single arg is a vector of U assignable to T, use a dynamic label.
template <typename T, typename U>
struct GraphAssemblerLabelForXHelper<
    T, std::enable_if_t<std::is_assignable_v<T&, U>>, base::SmallVector<U, 4>> {
  using Type = GraphAssemblerDynamicLabel;
};

template <typename... Vars>
using GraphAssemblerLabelForVars =
    typename GraphAssemblerLabelForXHelper<Node*, void, Vars...>::Type;

template <typename... Reps>
using GraphAssemblerLabelForReps =
    typename GraphAssemblerLabelForXHelper<MachineRepresentation, void,
                                           Reps...>::Type;

}  // namespace detail

using NodeChangedCallback = std::function<void(Node*)>;
class V8_EXPORT_PRIVATE GraphAssembler {
 public:
  // Constructs a GraphAssembler. If {schedule} is not null, the graph assembler
  // will maintain the schedule as it updates blocks.
  GraphAssembler(
      MachineGraph* jsgraph, Zone* zone,
      BranchSemantics default_branch_semantics,
      base::Optional<NodeChangedCallback> node_changed_callback = base::nullopt,
      bool mark_loop_exits = false);
  virtual ~GraphAssembler();
  virtual SimplifiedOperatorBuilder* simplified() { UNREACHABLE(); }

  void Reset();
  void InitializeEffectControl(Node* effect, Node* control);

  // Create label.
  template <typename... Reps>
  detail::GraphAssemblerLabelForReps<Reps...> MakeLabelFor(
      GraphAssemblerLabelType type, Reps... reps) {
    std::array<MachineRepresentation, sizeof...(Reps)> reps_array = {reps...};
    return detail::GraphAssemblerLabelForReps<Reps...>(
        type, loop_nesting_level_, std::move(reps_array));
  }
  GraphAssemblerDynamicLabel MakeLabelFor(
      GraphAssemblerLabelType type,
      base::SmallVector<MachineRepresentation, 4> reps) {
    return GraphAssemblerDynamicLabel(type, loop_nesting_level_,
                                      std::move(reps));
  }

  // Convenience wrapper for creating non-deferred labels.
  template <typename... Reps>
  detail::GraphAssemblerLabelForReps<Reps...> MakeLabel(Reps... reps) {
    return MakeLabelFor(GraphAssemblerLabelType::kNonDeferred, reps...);
  }

  // Convenience wrapper for creating loop labels.
  template <typename... Reps>
  detail::GraphAssemblerLabelForReps<Reps...> MakeLoopLabel(Reps... reps) {
    return MakeLabelFor(GraphAssemblerLabelType::kLoop, reps...);
  }

  // Convenience wrapper for creating deferred labels.
  template <typename... Reps>
  detail::GraphAssemblerLabelForReps<Reps...> MakeDeferredLabel(Reps... reps) {
    return MakeLabelFor(GraphAssemblerLabelType::kDeferred, reps...);
  }

  // Value creation.
  Node* IntPtrConstant(intptr_t value);
  TNode<UintPtrT> UintPtrConstant(uintptr_t value);
  Node* Int32Constant(int32_t value);
  TNode<Uint32T> Uint32Constant(uint32_t value);
  Node* Int64Constant(int64_t value);
  Node* Uint64Constant(uint64_t value);
  Node* UniqueIntPtrConstant(intptr_t value);
  Node* Float64Constant(double value);
  Node* ExternalConstant(ExternalReference ref);

  Node* Projection(int index, Node* value, Node* ctrl = nullptr);

  Node* Parameter(int index);

  Node* LoadFramePointer();
#if V8_ENABLE_WEBASSEMBLY
  Node* LoadStackPointer();
  Node* SetStackPointer(Node* sp, wasm::FPRelativeScope fp_scope);
#endif

  Node* LoadHeapNumberValue(Node* heap_number);

#define PURE_UNOP_DECL(Name) Node* Name(Node* input);
  PURE_ASSEMBLER_MACH_UNOP_LIST(PURE_UNOP_DECL)
#undef PURE_UNOP_DECL

#define BINOP_DECL(Name) Node* Name(Node* left, Node* right);
#define BINOP_DECL_TNODE(Name, Result, Left, Right) \
  TNode<Result> Name(SloppyTNode<Left> left, SloppyTNode<Right> right);
  PURE_ASSEMBLER_MACH_BINOP_LIST(BINOP_DECL, BINOP_DECL_TNODE)
  CHECKED_ASSEMBLER_MACH_BINOP_LIST(BINOP_DECL)
#undef BINOP_DECL
#undef BINOP_DECL_TNODE
  TNode<BoolT> UintPtrLessThan(TNode<UintPtrT> left, TNode<UintPtrT> right);
  TNode<BoolT> UintPtrLessThanOrEqual(TNode<UintPtrT> left,
                                      TNode<UintPtrT> right);
  TNode<UintPtrT> UintPtrAdd(TNode<UintPtrT> left, TNode<UintPtrT> right);
  TNode<UintPtrT> UintPtrSub(TNode<UintPtrT> left, TNode<UintPtrT> right);
  TNode<UintPtrT> UintPtrDiv(TNode<UintPtrT> left, TNode<UintPtrT> right);
  TNode<UintPtrT> ChangeUint32ToUintPtr(SloppyTNode<Uint32T> value);

#ifdef V8_MAP_PACKING
  Node* PackMapWord(TNode<Map> map);
  TNode<Map> UnpackMapWord(Node* map_word);
#endif
  TNode<Map> LoadMap(Node* object);

  Node* DebugBreak();

  Node* Unreachable();
  // This special variant doesn't connect the Unreachable node to end, and does
  // not reset current effect/control. Intended only for special use-cases like
  // lowering DeadValue.
  Node* UnreachableWithoutConnectToEnd();

  Node* IntPtrEqual(Node* left, Node* right);
  Node* TaggedEqual(Node* left, Node* right);

  Node* SmiSub(Node* left, Node* right);
  Node* SmiLessThan(Node* left, Node* right);

  Node* Float64RoundDown(Node* value);
  Node* Float64RoundTruncate(Node* value);
  Node* TruncateFloat64ToInt64(Node* value, TruncateKind kind);

  Node* BitcastWordToTagged(Node* value);
  Node* BitcastWordToTaggedSigned(Node* value);
  Node* BitcastTaggedToWord(Node* value);
  Node* BitcastTaggedToWordForTagAndSmiBits(Node* value);
  Node* BitcastMaybeObjectToWord(Node* value);

  Node* TypeGuard(Type type, Node* value);
  template <typename T>
  TNode<T> TypeGuard(Type type, TNode<T> value) {
    return TNode<T>::UncheckedCast(TypeGuard(type, static_cast<Node*>(value)));
  }
  Node* Checkpoint(FrameState frame_state);

  TNode<RawPtrT> StackSlot(int size, int alignment, bool is_tagged = false);

  Node* AdaptLocalArgument(Node* argument);

  Node* Store(StoreRepresentation rep, Node* object, Node* offset, Node* value);
  Node* Store(StoreRepresentation rep, Node* object, int offset, Node* value);
  Node* Load(MachineType type, Node* object, Node* offset);
  Node* Load(MachineType type, Node* object, int offset);

  Node* StoreUnaligned(MachineRepresentation rep, Node* object, Node* offset,
                       Node* value);
  Node* LoadUnaligned(MachineType type, Node* object, Node* offset);

  Node* ProtectedStore(MachineRepresentation rep, Node* object, Node* offset,
                       Node* value);
  Node* ProtectedLoad(MachineType type, Node* object, Node* offset);
  Node* LoadTrapOnNull(MachineType type, Node* object, Node* offset);
  Node* StoreTrapOnNull(StoreRepresentation rep, Node* object, Node* offset,
                        Node* value);

  Node* Retain(Node* buffer);
  Node* IntPtrAdd(Node* a, Node* b);
  Node* IntPtrSub(Node* a, Node* b);

  Node* DeoptimizeIf(DeoptimizeReason reason, FeedbackSource const& feedback,
                     Node* condition, Node* frame_state);
  Node* DeoptimizeIfNot(DeoptimizeReason reason, FeedbackSource const& feedback,
                        Node* condition, Node* frame_state);
  TNode<Object> Call(const CallDescriptor* call_descriptor, int inputs_size,
                     Node** inputs);
  TNode<Object> Call(const Operator* op, int inputs_size, Node** inputs);
  template <typename... Args>
  TNode<Object> Call(const CallDescriptor* call_descriptor, Node* first_arg,
                     Args... args);
  template <typename... Args>
  TNode<Object> Call(const Operator* op, Node* first_arg, Args... args);
  void TailCall(const CallDescriptor* call_descriptor, int inputs_size,
                Node** inputs);

  // Basic control operations.
  template <size_t VarCount>
  void Bind(GraphAssemblerLabel<VarCount>* label);

  template <typename... Vars>
  void Goto(detail::GraphAssemblerLabelForVars<Vars...>* label, Vars...);

  // Branch hints are inferred from if_true/if_false deferred states.
  void BranchWithCriticalSafetyCheck(Node* condition,
                                     GraphAssemblerLabel<0u>* if_true,
                                     GraphAssemblerLabel<0u>* if_false);

  // Branch hints are inferred from if_true/if_false deferred states.
  template <typename... Vars>
  void Branch(Node* condition,
              detail::GraphAssemblerLabelForVars<Vars...>* if_true,
              detail::GraphAssemblerLabelForVars<Vars...>* if_false, Vars...);

  template <typename... Vars>
  void BranchWithHint(Node* condition,
                      detail::GraphAssemblerLabelForVars<Vars...>* if_true,
                      detail::GraphAssemblerLabelForVars<Vars...>* if_false,
                      BranchHint hint, Vars...);
  template <typename... Vars>
  void MachineBranch(TNode<Word32T> condition,
                     GraphAssemblerLabel<sizeof...(Vars)>* if_true,
                     GraphAssemblerLabel<sizeof...(Vars)>* if_false,
                     BranchHint hint, Vars...);
  template <typename... Vars>
  void JSBranch(TNode<Boolean> condition,
                GraphAssemblerLabel<sizeof...(Vars)>* if_true,
                GraphAssemblerLabel<sizeof...(Vars)>* if_false, BranchHint hint,
                Vars...);

  // Control helpers.

  // {GotoIf(c, l, h)} is equivalent to {BranchWithHint(c, l, templ, h);
  // Bind(templ)}.
  template <typename... Vars>
  void GotoIf(Node* condition,
              detail::GraphAssemblerLabelForVars<Vars...>* label,
              BranchHint hint, Vars...);

  // {GotoIfNot(c, l, h)} is equivalent to {BranchWithHint(c, templ, l, h);
  // Bind(templ)}.
  // The branch hint refers to the expected outcome of the provided condition,
  // so {GotoIfNot(..., BranchHint::kTrue)} means "optimize for the case where
  // the branch is *not* taken".
  template <typename... Vars>
  void GotoIfNot(Node* condition,
                 detail::GraphAssemblerLabelForVars<Vars...>* label,
                 BranchHint hint, Vars...);

  // {GotoIf(c, l)} is equivalent to {Branch(c, l, templ);Bind(templ)}.
  template <typename... Vars>
  void GotoIf(Node* condition,
              detail::GraphAssemblerLabelForVars<Vars...>* label, Vars...);

  // {GotoIfNot(c, l)} is equivalent to {Branch(c, templ, l);Bind(templ)}.
  template <typename... Vars>
  void GotoIfNot(Node* condition,
                 detail::GraphAssemblerLabelForVars<Vars...>* label, Vars...);

  bool HasActiveBlock() const {
    // This is false if the current block has been terminated (e.g. by a Goto or
    // Unreachable). In that case, a new label must be bound before we can
    // continue emitting nodes.
    return control() != nullptr;
  }

  // Updates current effect and control based on outputs of {node}.
  V8_INLINE void UpdateEffectControlWith(Node* node) {
    if (node->op()->EffectOutputCount() > 0) {
      effect_ = node;
    }
    if (node->op()->ControlOutputCount() > 0) {
      control_ = node;
    }
  }

  // Adds {node} to the current position and updates assembler's current effect
  // and control.
  Node* AddNode(Node* node);

  template <typename T>
  TNode<T> AddNode(Node* node) {
    return TNode<T>::UncheckedCast(AddNode(node));
  }

  void ConnectUnreachableToEnd();

  // Add an inline reducers such that nodes added to the graph will be run
  // through the reducers and possibly further lowered. Each reducer should
  // operate on independent node types since once a reducer changes a node we
  // no longer run any other reducers on that node. The reducers should also
  // only generate new nodes that wouldn't be further reduced, as new nodes
  // generated by a reducer won't be passed through the reducers again.
  void AddInlineReducer(Reducer* reducer) {
    inline_reducers_.push_back(reducer);
  }

  Control control() const { return Control(control_); }
  Effect effect() const { return Effect(effect_); }

  Node* start() const { return graph()->start(); }

 protected:
  constexpr bool Is64() const { return kSystemPointerSize == 8; }

  template <typename... Vars>
  void MergeState(detail::GraphAssemblerLabelForVars<Vars...>* label,
                  Vars... vars);

  V8_INLINE Node* AddClonedNode(Node* node);

  MachineGraph* mcgraph() const { return mcgraph_; }
  Graph* graph() const { return mcgraph_->graph(); }
  Zone* temp_zone() const { return temp_zone_; }
  CommonOperatorBuilder* common() const { return mcgraph()->common(); }
  MachineOperatorBuilder* machine() const { return mcgraph()->machine(); }

  // Updates machinery for creating {LoopExit,LoopExitEffect,LoopExitValue}
  // nodes on loop exits (which are necessary for loop peeling).
  //
  // All labels created while a LoopScope is live are considered to be inside
  // the loop.
  template <MachineRepresentation... Reps>
  class V8_NODISCARD LoopScope final {
   private:
    // The internal scope is only here to increment the graph assembler's
    // nesting level prior to `loop_header_label` creation below.
    class V8_NODISCARD LoopScopeInternal {
     public:
      explicit LoopScopeInternal(GraphAssembler* gasm)
          : previous_loop_nesting_level_(gasm->loop_nesting_level_),
            gasm_(gasm) {
        gasm->loop_nesting_level_++;
      }

      ~LoopScopeInternal() {
        gasm_->loop_nesting_level_--;
        DCHECK_EQ(gasm_->loop_nesting_level_, previous_loop_nesting_level_);
      }

     private:
      const int previous_loop_nesting_level_;
      GraphAssembler* const gasm_;
    };

   public:
    explicit LoopScope(GraphAssembler* gasm)
        : internal_scope_(gasm),
          gasm_(gasm),
          loop_header_label_(gasm->MakeLoopLabel(Reps...)) {
      // This feature may only be used if it has been enabled.
      DCHECK(gasm_->mark_loop_exits_);
      gasm->loop_headers_.push_back(&loop_header_label_.control_);
      DCHECK_EQ(static_cast<int>(gasm_->loop_headers_.size()),
                gasm_->loop_nesting_level_);
    }

    ~LoopScope() {
      DCHECK_EQ(static_cast<int>(gasm_->loop_headers_.size()),
                gasm_->loop_nesting_level_);
      gasm_->loop_headers_.pop_back();
    }

    GraphAssemblerLabel<sizeof...(Reps)>* loop_header_label() {
      return &loop_header_label_;
    }

   private:
    const LoopScopeInternal internal_scope_;
    GraphAssembler* const gasm_;
    GraphAssemblerLabel<sizeof...(Reps)> loop_header_label_;
  };

  // Upon destruction, restores effect and control to the state at construction.
  class V8_NODISCARD RestoreEffectControlScope {
   public:
    explicit RestoreEffectControlScope(GraphAssembler* gasm)
        : gasm_(gasm), effect_(gasm->effect()), control_(gasm->control()) {}

    ~RestoreEffectControlScope() {
      gasm_->effect_ = effect_;
      gasm_->control_ = control_;
    }

   private:
    GraphAssembler* const gasm_;
    const Effect effect_;
    const Control control_;
  };

 private:
  class BlockInlineReduction;

  template <typename... Vars>
  void BranchImpl(BranchSemantics semantics, Node* condition,
                  GraphAssemblerLabel<sizeof...(Vars)>* if_true,
                  GraphAssemblerLabel<sizeof...(Vars)>* if_false,
                  BranchHint hint, Vars...);

  Zone* temp_zone_;
  MachineGraph* mcgraph_;
  BranchSemantics default_branch_semantics_;
  Node* effect_;
  Node* control_;
  // {node_changed_callback_} should be called when a node outside the
  // subgraph created by the graph assembler changes.
  base::Optional<NodeChangedCallback> node_changed_callback_;

  // Inline reducers enable reductions to be performed to nodes as they are
  // added to the graph with the graph assembler.
  ZoneVector<Reducer*> inline_reducers_;
  bool inline_reductions_blocked_;

  // Track loop information in order to properly mark loop exits with
  // {LoopExit,LoopExitEffect,LoopExitValue} nodes. The outermost level has
  // a nesting level of 0. See also GraphAssembler::LoopScope.
  int loop_nesting_level_ = 0;
  ZoneVector<Node**> loop_headers_;

  // Feature configuration. As more features are added, this should be turned
  // into a bitfield.
  const bool mark_loop_exits_;
};

template <size_t VarCount>
Node* GraphAssemblerLabel<VarCount>::PhiAt(size_t index) {
  DCHECK(IsBound());
  DCHECK_LT(index, Count());
  return bindings_[index];
}

template <typename... Vars>
void GraphAssembler::MergeState(
    detail::GraphAssemblerLabelForVars<Vars...>* label, Vars... vars) {
  using NodeArray = typename detail::GraphAssemblerLabelForVars<
      Vars...>::template Array<Node*>;
  RestoreEffectControlScope restore_effect_control_scope(this);

  const int merged_count = static_cast<int>(label->merged_count_);

  const size_t var_count = label->Count();
  NodeArray var_array{vars...};

  const bool is_loop_exit = label->loop_nesting_level_ != loop_nesting_level_;
  if (is_loop_exit) {
    // This feature may only be used if it has been enabled.
    USE(mark_loop_exits_);
    DCHECK(mark_loop_exits_);
    // Jumping from loops to loops not supported.
    DCHECK(!label->IsLoop());
    // Currently only the simple case of jumping one level is supported.
    DCHECK_EQ(label->loop_nesting_level_, loop_nesting_level_ - 1);
    DCHECK(!loop_headers_.empty());
    DCHECK_NOT_NULL(*loop_headers_.back());

    // Mark this exit to enable loop peeling.
    AddNode(graph()->NewNode(common()->LoopExit(), control(),
                             *loop_headers_.back()));
    AddNode(graph()->NewNode(common()->LoopExitEffect(), effect(), control()));
    for (size_t i = 0; i < var_count; i++) {
      var_array[i] = AddNode(graph()->NewNode(
          common()->LoopExitValue(MachineRepresentation::kTagged), var_array[i],
          control()));
    }
  }

  if (label->IsLoop()) {
    if (merged_count == 0) {
      DCHECK(!label->IsBound());
      label->control_ =
          graph()->NewNode(common()->Loop(2), control(), control());
      label->effect_ = graph()->NewNode(common()->EffectPhi(2), effect(),
                                        effect(), label->control_);
      Node* terminate = graph()->NewNode(common()->Terminate(), label->effect_,
                                         label->control_);
      NodeProperties::MergeControlToEnd(graph(), common(), terminate);
      for (size_t i = 0; i < var_count; i++) {
        label->bindings_[i] =
            graph()->NewNode(common()->Phi(label->representations_[i], 2),
                             var_array[i], var_array[i], label->control_);
      }
    } else {
      DCHECK(label->IsBound());
      DCHECK_EQ(1, merged_count);
      label->control_->ReplaceInput(1, control());
      label->effect_->ReplaceInput(1, effect());
      for (size_t i = 0; i < var_count; i++) {
        label->bindings_[i]->ReplaceInput(1, var_array[i]);
        CHECK(!NodeProperties::IsTyped(var_array[i]));  // Unsupported.
      }
    }
  } else {
    DCHECK(!label->IsLoop());
    DCHECK(!label->IsBound());
    if (merged_count == 0) {
      // Just set the control, effect and variables directly.
      label->control_ = control();
      label->effect_ = effect();
      for (size_t i = 0; i < var_count; i++) {
        label->bindings_[i] = var_array[i];
      }
    } else if (merged_count == 1) {
      // Create merge, effect phi and a phi for each variable.
      label->control_ =
          graph()->NewNode(common()->Merge(2), label->control_, control());
      label->effect_ = graph()->NewNode(common()->EffectPhi(2), label->effect_,
                                        effect(), label->control_);
      for (size_t i = 0; i < var_count; i++) {
        label->bindings_[i] = graph()->NewNode(
            common()->Phi(label->representations_[i], 2), label->bindings_[i],
            var_array[i], label->control_);
      }
    } else {
      // Append to the merge, effect phi and phis.
      DCHECK_EQ(IrOpcode::kMerge, label->control_->opcode());
      label->control_->AppendInput(graph()->zone(), control());
      NodeProperties::ChangeOp(label->control_,
                               common()->Merge(merged_count + 1));

      DCHECK_EQ(IrOpcode::kEffectPhi, label->effect_->opcode());
      label->effect_->ReplaceInput(merged_count, effect());
      label->effect_->AppendInput(graph()->zone(), label->control_);
      NodeProperties::ChangeOp(label->effect_,
                               common()->EffectPhi(merged_count + 1));

      for (size_t i = 0; i < var_count; i++) {
        DCHECK_EQ(IrOpcode::kPhi, label->bindings_[i]->opcode());
        label->bindings_[i]->ReplaceInput(merged_count, var_array[i]);
        label->bindings_[i]->AppendInput(graph()->zone(), label->control_);
        NodeProperties::ChangeOp(
            label->bindings_[i],
            common()->Phi(label->representations_[i], merged_count + 1));
        if (NodeProperties::IsTyped(label->bindings_[i])) {
          CHECK(NodeProperties::IsTyped(var_array[i]));
          Type old_type = NodeProperties::GetType(label->bindings_[i]);
          Type new_type = Type::Union(
              old_type, NodeProperties::GetType(var_array[i]), graph()->zone());
          NodeProperties::SetType(label->bindings_[i], new_type);
        }
      }
    }
  }
  label->merged_count_++;
}

template <size_t VarCount>
void GraphAssembler::Bind(GraphAssemblerLabel<VarCount>* label) {
  DCHECK_NULL(control());
  DCHECK_NULL(effect());
  DCHECK_LT(0, label->merged_count_);
  DCHECK_EQ(label->loop_nesting_level_, loop_nesting_level_);

  control_ = label->control_;
  effect_ = label->effect_;

  label->SetBound();

  if (label->merged_count_ > 1 || label->IsLoop()) {
    AddNode(label->control_);
    AddNode(label->effect_);
    for (size_t i = 0; i < label->Count(); i++) {
      AddNode(label->bindings_[i]);
    }
  } else {
    // If the basic block does not have a control node, insert a dummy
    // Merge node, so that other passes have a control node to start from.
    control_ = AddNode(graph()->NewNode(common()->Merge(1), control()));
  }
}

template <typename... Vars>
void GraphAssembler::Branch(
    Node* condition, detail::GraphAssemblerLabelForVars<Vars...>* if_true,
    detail::GraphAssemblerLabelForVars<Vars...>* if_false, Vars... vars) {
  BranchHint hint = BranchHint::kNone;
  if (if_true->IsDeferred() != if_false->IsDeferred()) {
    hint = if_false->IsDeferred() ? BranchHint::kTrue : BranchHint::kFalse;
  }

  BranchImpl(default_branch_semantics_, condition, if_true, if_false, hint,
             vars...);
}

template <typename... Vars>
void GraphAssembler::BranchWithHint(
    Node* condition, detail::GraphAssemblerLabelForVars<Vars...>* if_true,
    detail::GraphAssemblerLabelForVars<Vars...>* if_false, BranchHint hint,
    Vars... vars) {
  BranchImpl(default_branch_semantics_, condition, if_true, if_false, hint,
             vars...);
}

template <typename... Vars>
void GraphAssembler::MachineBranch(
    TNode<Word32T> condition, GraphAssemblerLabel<sizeof...(Vars)>* if_true,
    GraphAssemblerLabel<sizeof...(Vars)>* if_false, BranchHint hint,
    Vars... vars) {
  BranchImpl(BranchSemantics::kMachine, condition, if_true, if_false, hint,
             vars...);
}

template <typename... Vars>
void GraphAssembler::JSBranch(TNode<Boolean> condition,
                              GraphAssemblerLabel<sizeof...(Vars)>* if_true,
                              GraphAssemblerLabel<sizeof...(Vars)>* if_false,
                              BranchHint hint, Vars... vars) {
  BranchImpl(BranchSemantics::kJS, condition, if_true, if_false, hint, vars...);
}

template <typename... Vars>
void GraphAssembler::BranchImpl(BranchSemantics semantics, Node* condition,
                                GraphAssemblerLabel<sizeof...(Vars)>* if_true,
                                GraphAssemblerLabel<sizeof...(Vars)>* if_false,
                                BranchHint hint, Vars... vars) {
  DCHECK_NOT_NULL(control());

  Node* branch =
      graph()->NewNode(common()->Branch(hint, semantics), condition, control());

  control_ = graph()->NewNode(common()->IfTrue(), branch);
  MergeState(if_true, vars...);

  control_ = graph()->NewNode(common()->IfFalse(), branch);
  MergeState(if_false, vars...);

  control_ = nullptr;
  effect_ = nullptr;
}

template <typename... Vars>
void GraphAssembler::Goto(detail::GraphAssemblerLabelForVars<Vars...>* label,
                          Vars... vars) {
  DCHECK_NOT_NULL(control());
  DCHECK_NOT_NULL(effect());
  MergeState(label, vars...);

  control_ = nullptr;
  effect_ = nullptr;
}

template <typename... Vars>
void GraphAssembler::GotoIf(Node* condition,
                            detail::GraphAssemblerLabelForVars<Vars...>* label,
                            BranchHint hint, Vars... vars) {
  Node* branch = graph()->NewNode(
      common()->Branch(hint, default_branch_semantics_), condition, control());

  control_ = graph()->NewNode(common()->IfTrue(), branch);
  MergeState(label, vars...);

  control_ = AddNode(graph()->NewNode(common()->IfFalse(), branch));
}

template <typename... Vars>
void GraphAssembler::GotoIfNot(
    Node* condition, detail::GraphAssemblerLabelForVars<Vars...>* label,
    BranchHint hint, Vars... vars) {
  Node* branch = graph()->NewNode(
      common()->Branch(hint, default_branch_semantics_), condition, control());

  control_ = graph()->NewNode(common()->IfFalse(), branch);
  MergeState(label, vars...);

  control_ = AddNode(graph()->NewNode(common()->IfTrue(), branch));
}

template <typename... Vars>
void GraphAssembler::GotoIf(Node* condition,
                            detail::GraphAssemblerLabelForVars<Vars...>* label,
                            Vars... vars) {
  BranchHint hint =
      label->IsDeferred() ? BranchHint::kFalse : BranchHint::kNone;
  return GotoIf(condition, label, hint, vars...);
}

template <typename... Vars>
void GraphAssembler::GotoIfNot(
    Node* condition, detail::GraphAssemblerLabelForVars<Vars...>* label,
    Vars... vars) {
  BranchHint hint = label->IsDeferred() ? BranchHint::kTrue : BranchHint::kNone;
  return GotoIfNot(condition, label, hint, vars...);
}

template <typename... Args>
TNode<Object> GraphAssembler::Call(const CallDescriptor* call_descriptor,
                                   Node* first_arg, Args... args) {
  const Operator* op = common()->Call(call_descriptor);
  return Call(op, first_arg, args...);
}

template <typename... Args>
TNode<Object> GraphAssembler::Call(const Operator* op, Node* first_arg,
                                   Args... args) {
  Node* args_array[] = {first_arg, args..., effect(), control()};
  int size = static_cast<int>(1 + sizeof...(args)) + op->EffectInputCount() +
             op->ControlInputCount();
  return Call(op, size, args_array);
}

class V8_EXPORT_PRIVATE JSGraphAssembler : public GraphAssembler {
 public:
  // Constructs a JSGraphAssembler. If {schedule} is not null, the graph
  // assembler will maintain the schedule as it updates blocks.
  JSGraphAssembler(
      JSHeapBroker* broker, JSGraph* jsgraph, Zone* zone,
      BranchSemantics branch_semantics,
      base::Optional<NodeChangedCallback> node_changed_callback = base::nullopt,
      bool mark_loop_exits = false)
      : GraphAssembler(jsgraph, zone, branch_semantics, node_changed_callback,
                       mark_loop_exits),
        broker_(broker),
        jsgraph_(jsgraph),
        outermost_catch_scope_(CatchScope::Outermost(zone)),
        catch_scope_(&outermost_catch_scope_) {
    outermost_catch_scope_.set_gasm(this);
  }

  Node* SmiConstant(int32_t value);
  TNode<HeapObject> HeapConstant(Handle<HeapObject> object);
  TNode<Object> Constant(ObjectRef ref);
  TNode<Number> NumberConstant(double value);
  Node* CEntryStubConstant(int result_size);

#define SINGLETON_CONST_DECL(Name, Type) TNode<Type> Name##Constant();
  JSGRAPH_SINGLETON_CONSTANT_LIST(SINGLETON_CONST_DECL)
#undef SINGLETON_CONST_DECL

#define SINGLETON_CONST_TEST_DECL(Name, ...) \
  TNode<Boolean> Is##Name(TNode<Object> value);
  JSGRAPH_SINGLETON_CONSTANT_LIST(SINGLETON_CONST_TEST_DECL)
#undef SINGLETON_CONST_TEST_DECL

  Node* Allocate(AllocationType allocation, Node* size);
  TNode<Map> LoadMap(TNode<HeapObject> object);
  Node* LoadField(FieldAccess const&, Node* object);
  template <typename T>
  TNode<T> LoadField(FieldAccess const& access, TNode<HeapObject> object) {
    // TODO(jgruber): Investigate issues on ptr compression bots and enable.
    // DCHECK(IsMachineRepresentationOf<T>(
    //     access.machine_type.representation()));
    return TNode<T>::UncheckedCast(LoadField(access, object));
  }
  TNode<Uint32T> LoadElementsKind(TNode<Map> map);
  Node* LoadElement(ElementAccess const&, Node* object, Node* index);
  template <typename T>
  TNode<T> LoadElement(ElementAccess const& access, TNode<HeapObject> object,
                       TNode<Number> index) {
    // TODO(jgruber): Investigate issues on ptr compression bots and enable.
    // DCHECK(IsMachineRepresentationOf<T>(
    //     access.machine_type.representation()));
    return TNode<T>::UncheckedCast(LoadElement(access, object, index));
  }
  Node* StoreField(FieldAccess const&, Node* object, Node* value);
  Node* StoreElement(ElementAccess const&, Node* object, Node* index,
                     Node* value);
  Node* ClearPendingMessage();

  void TransitionAndStoreElement(MapRef double_map, MapRef fast_map,
                                 TNode<HeapObject> object, TNode<Number> index,
                                 TNode<Object> value);
  TNode<Number> StringLength(TNode<String> string);
  TNode<Boolean> ReferenceEqual(TNode<Object> lhs, TNode<Object> rhs);
  TNode<Number> PlainPrimitiveToNumber(TNode<Object> value);
  TNode<Number> NumberMin(TNode<Number> lhs, TNode<Number> rhs);
  TNode<Number> NumberMax(TNode<Number> lhs, TNode<Number> rhs);
  TNode<Boolean> NumberEqual(TNode<Number> lhs, TNode<Number> rhs);
  TNode<Boolean> NumberLessThan(TNode<Number> lhs, TNode<Number> rhs);
  TNode<Boolean> NumberLessThanOrEqual(TNode<Number> lhs, TNode<Number> rhs);
  TNode<Number> NumberAdd(TNode<Number> lhs, TNode<Number> rhs);
  TNode<Number> NumberSubtract(TNode<Number> lhs, TNode<Number> rhs);
  TNode<Number> NumberShiftRightLogical(TNode<Number> lhs, TNode<Number> rhs);
  TNode<Number> NumberBitwiseAnd(TNode<Number> lhs, TNode<Number> rhs);
  TNode<Number> NumberBitwiseOr(TNode<Number> lhs, TNode<Number> rhs);
  TNode<Number> NumberDivide(TNode<Number> lhs, TNode<Number> rhs);
  TNode<Number> NumberFloor(TNode<Number> value);
  TNode<String> StringSubstring(TNode<String> string, TNode<Number> from,
                                TNode<Number> to);
  TNode<Boolean> ObjectIsCallable(TNode<Object> value);
  TNode<Boolean> ObjectIsSmi(TNode<Object> value);
  TNode<Boolean> ObjectIsUndetectable(TNode<Object> value);
  Node* CheckIf(Node* cond, DeoptimizeReason reason,
                const FeedbackSource& feedback = {});
  Node* Assert(Node* cond, const char* condition_string = "",
               const char* file = "", int line = -1);
  TNode<Boolean> NumberIsFloat64Hole(TNode<Number> value);
  TNode<Boolean> ToBoolean(TNode<Object> value);
  TNode<Object> ConvertTaggedHoleToUndefined(TNode<Object> value);
  TNode<FixedArrayBase> MaybeGrowFastElements(ElementsKind kind,
                                              const FeedbackSource& feedback,
                                              TNode<JSArray> array,
                                              TNode<FixedArrayBase> elements,
                                              TNode<Number> new_length,
                                              TNode<Number> old_length);
  Node* StringCharCodeAt(TNode<String> string, TNode<Number> position);
  TNode<String> StringFromSingleCharCode(TNode<Number> code);
  TNode<Object> DoubleArrayMax(TNode<JSArray> array);
  TNode<Object> DoubleArrayMin(TNode<JSArray> array);
  // Computes the byte length for a given {array_buffer_view}. If the set of
  // possible ElementsKinds is known statically pass as
  // {elements_kinds_candidates} to allow the assembler to generate more
  // efficient code. Pass an empty {elements_kinds_candidates} to generate code
  // that is generic enough to handle all ElementsKinds.
  TNode<Number> ArrayBufferViewByteLength(
      TNode<JSArrayBufferView> array_buffer_view, InstanceType instance_type,
      std::set<ElementsKind> elements_kinds_candidates, TNode<Context> context);
  // Computes the length for a given {typed_array}. If the set of possible
  // ElementsKinds is known statically pass as {elements_kinds_candidates} to
  // allow the assembler to generate more efficient code. Pass an empty
  // {elements_kinds_candidates} to generate code that is generic enough to
  // handle all ElementsKinds.
  TNode<Number> TypedArrayLength(
      TNode<JSTypedArray> typed_array,
      std::set<ElementsKind> elements_kinds_candidates, TNode<Context> context);
  // Performs the full detached check. This includes fixed-length RABs whos
  // underlying buffer has been shrunk OOB.
  void CheckIfTypedArrayWasDetached(
      TNode<JSTypedArray> typed_array,
      std::set<ElementsKind> elements_kinds_candidates,
      const FeedbackSource& feedback);
  TNode<Uint32T> LookupByteShiftForElementsKind(TNode<Uint32T> elements_kind);
  TNode<Uint32T> LookupByteSizeForElementsKind(TNode<Uint32T> elements_kind);

  TNode<Object> JSCallRuntime1(
      Runtime::FunctionId function_id, TNode<Object> arg0,
      TNode<Context> context, base::Optional<FrameState> frame_state,
      Operator::Properties properties = Operator::kNoProperties);
  TNode<Object> JSCallRuntime2(Runtime::FunctionId function_id,
                               TNode<Object> arg0, TNode<Object> arg1,
                               TNode<Context> context, FrameState frame_state);
  Node* Chained(const Operator* op, Node* input);

  JSHeapBroker* broker() const { return broker_; }
  JSGraph* jsgraph() const { return jsgraph_; }
  Isolate* isolate() const { return jsgraph()->isolate(); }
  SimplifiedOperatorBuilder* simplified() override {
    return jsgraph()->simplified();
  }
  JSOperatorBuilder* javascript() const { return jsgraph()->javascript(); }

  template <typename T, typename U>
  TNode<T> EnterMachineGraph(TNode<U> input, UseInfo use_info) {
    DCHECK_EQ(use_info.type_check(), TypeCheckKind::kNone);
    return AddNode<T>(
        graph()->NewNode(common()->EnterMachineGraph(use_info), input));
  }

  template <typename T, typename U>
  TNode<T> ExitMachineGraph(TNode<U> input,
                            MachineRepresentation output_representation,
                            Type output_type) {
    return AddNode<T>(graph()->NewNode(
        common()->ExitMachineGraph(output_representation, output_type), input));
  }

  // A catch scope represents a single catch handler. The handler can be
  // custom catch logic within the reduction itself; or a catch handler in the
  // outside graph into which the reduction will be integrated (in this case
  // the scope is called 'outermost').
  class V8_NODISCARD CatchScope {
   private:
    // Only used to partially construct the outermost scope.
    explicit CatchScope(Zone* zone) : if_exception_nodes_(zone) {}

    // For all inner scopes.
    CatchScope(Zone* zone, JSGraphAssembler* gasm)
        : gasm_(gasm),
          parent_(gasm->catch_scope_),
          has_handler_(true),
          if_exception_nodes_(zone) {
      DCHECK_NOT_NULL(gasm_);
      gasm_->catch_scope_ = this;
    }

   public:
    ~CatchScope() { gasm_->catch_scope_ = parent_; }

    static CatchScope Outermost(Zone* zone) { return CatchScope{zone}; }
    static CatchScope Inner(Zone* zone, JSGraphAssembler* gasm) {
      return {zone, gasm};
    }

    bool has_handler() const { return has_handler_; }
    bool is_outermost() const { return parent_ == nullptr; }
    CatchScope* parent() const { return parent_; }

    // Should only be used to initialize the outermost scope (inner scopes
    // always have a handler and are passed the gasm pointer at construction).
    void set_has_handler(bool v) {
      DCHECK(is_outermost());
      has_handler_ = v;
    }
    void set_gasm(JSGraphAssembler* v) {
      DCHECK(is_outermost());
      DCHECK_NOT_NULL(v);
      gasm_ = v;
    }

    bool has_exceptional_control_flow() const {
      return !if_exception_nodes_.empty();
    }

    void RegisterIfExceptionNode(Node* if_exception) {
      DCHECK(has_handler());
      if_exception_nodes_.push_back(if_exception);
    }

    void MergeExceptionalPaths(TNode<Object>* exception_out, Effect* effect_out,
                               Control* control_out) {
      DCHECK(has_handler());
      DCHECK(has_exceptional_control_flow());

      const int size = static_cast<int>(if_exception_nodes_.size());

      if (size == 1) {
        // No merge needed.
        Node* e = if_exception_nodes_.at(0);
        *exception_out = TNode<Object>::UncheckedCast(e);
        *effect_out = Effect(e);
        *control_out = Control(e);
      } else {
        DCHECK_GT(size, 1);

        Node* merge = gasm_->graph()->NewNode(gasm_->common()->Merge(size),
                                              size, if_exception_nodes_.data());

        // These phis additionally take {merge} as an input. Temporarily add
        // it to the list.
        if_exception_nodes_.push_back(merge);
        const int size_with_merge =
            static_cast<int>(if_exception_nodes_.size());

        Node* ephi = gasm_->graph()->NewNode(gasm_->common()->EffectPhi(size),
                                             size_with_merge,
                                             if_exception_nodes_.data());
        Node* phi = gasm_->graph()->NewNode(
            gasm_->common()->Phi(MachineRepresentation::kTagged, size),
            size_with_merge, if_exception_nodes_.data());
        if_exception_nodes_.pop_back();

        *exception_out = TNode<Object>::UncheckedCast(phi);
        *effect_out = Effect(ephi);
        *control_out = Control(merge);
      }
    }

   private:
    JSGraphAssembler* gasm_ = nullptr;
    CatchScope* const parent_ = nullptr;
    bool has_handler_ = false;
    NodeVector if_exception_nodes_;
  };

  CatchScope* catch_scope() const { return catch_scope_; }
  Node* outermost_handler() const { return outermost_handler_; }

  using NodeGenerator0 = std::function<TNode<Object>()>;
  // TODO(jgruber): Currently, it's the responsibility of the developer to note
  // which operations may throw and appropriately wrap these in a call to
  // MayThrow (see e.g. JSCall3 and CallRuntime2). A more methodical approach
  // would be good.
  TNode<Object> MayThrow(const NodeGenerator0& body) {
    TNode<Object> result = body();

    if (catch_scope()->has_handler()) {
      // The IfException node is later merged into the outer graph.
      // Note: AddNode is intentionally not called since effect and control
      // should not be updated.
      Node* if_exception =
          graph()->NewNode(common()->IfException(), effect(), control());
      catch_scope()->RegisterIfExceptionNode(if_exception);

      // Control resumes here.
      AddNode(graph()->NewNode(common()->IfSuccess(), control()));
    }

    return result;
  }

  using VoidGenerator0 = std::function<void()>;
  // TODO(jgruber): Currently IfBuilder0 and IfBuilder1 are implemented as
  // separate classes. If, in the future, we encounter additional use cases that
  // return more than 1 value, we should merge these back into a single variadic
  // implementation.
  class IfBuilder0 final {
   public:
    IfBuilder0(JSGraphAssembler* gasm, TNode<Boolean> cond, bool negate_cond)
        : gasm_(gasm),
          cond_(cond),
          negate_cond_(negate_cond),
          initial_effect_(gasm->effect()),
          initial_control_(gasm->control()) {}

    IfBuilder0& ExpectTrue() {
      DCHECK_EQ(hint_, BranchHint::kNone);
      hint_ = BranchHint::kTrue;
      return *this;
    }
    IfBuilder0& ExpectFalse() {
      DCHECK_EQ(hint_, BranchHint::kNone);
      hint_ = BranchHint::kFalse;
      return *this;
    }

    IfBuilder0& Then(const VoidGenerator0& body) {
      then_body_ = body;
      return *this;
    }
    IfBuilder0& Else(const VoidGenerator0& body) {
      else_body_ = body;
      return *this;
    }

    ~IfBuilder0() {
      // Ensure correct usage: effect/control must not have been modified while
      // the IfBuilder0 instance is alive.
      DCHECK_EQ(gasm_->effect(), initial_effect_);
      DCHECK_EQ(gasm_->control(), initial_control_);

      // Unlike IfBuilder1, this supports an empty then or else body. This is
      // possible since the merge does not take any value inputs.
      DCHECK(then_body_ || else_body_);

      if (negate_cond_) std::swap(then_body_, else_body_);

      auto if_true = (hint_ == BranchHint::kFalse) ? gasm_->MakeDeferredLabel()
                                                   : gasm_->MakeLabel();
      auto if_false = (hint_ == BranchHint::kTrue) ? gasm_->MakeDeferredLabel()
                                                   : gasm_->MakeLabel();
      auto merge = gasm_->MakeLabel();
      gasm_->Branch(cond_, &if_true, &if_false);

      gasm_->Bind(&if_true);
      if (then_body_) then_body_();
      if (gasm_->HasActiveBlock()) gasm_->Goto(&merge);

      gasm_->Bind(&if_false);
      if (else_body_) else_body_();
      if (gasm_->HasActiveBlock()) gasm_->Goto(&merge);

      gasm_->Bind(&merge);
    }

    IfBuilder0(const IfBuilder0&) = delete;
    IfBuilder0& operator=(const IfBuilder0&) = delete;

   private:
    JSGraphAssembler* const gasm_;
    const TNode<Boolean> cond_;
    const bool negate_cond_;
    const Effect initial_effect_;
    const Control initial_control_;
    BranchHint hint_ = BranchHint::kNone;
    VoidGenerator0 then_body_;
    VoidGenerator0 else_body_;
  };

  IfBuilder0 If(TNode<Boolean> cond) { return {this, cond, false}; }
  IfBuilder0 IfNot(TNode<Boolean> cond) { return {this, cond, true}; }

  template <typename T, typename Cond>
  class IfBuilder1 {
    using If1BodyFunction = std::function<TNode<T>()>;

   public:
    IfBuilder1(JSGraphAssembler* gasm, TNode<Cond> cond, bool negate_cond)
        : gasm_(gasm), cond_(cond), negate_cond_(negate_cond) {}

    V8_WARN_UNUSED_RESULT IfBuilder1& ExpectTrue() {
      DCHECK_EQ(hint_, BranchHint::kNone);
      hint_ = BranchHint::kTrue;
      return *this;
    }

    V8_WARN_UNUSED_RESULT IfBuilder1& ExpectFalse() {
      DCHECK_EQ(hint_, BranchHint::kNone);
      hint_ = BranchHint::kFalse;
      return *this;
    }

    V8_WARN_UNUSED_RESULT IfBuilder1& Then(const If1BodyFunction& body) {
      then_body_ = body;
      return *this;
    }
    V8_WARN_UNUSED_RESULT IfBuilder1& Else(const If1BodyFunction& body) {
      else_body_ = body;
      return *this;
    }

    V8_WARN_UNUSED_RESULT TNode<T> Value() {
      DCHECK(then_body_);
      DCHECK(else_body_);

      if (negate_cond_) std::swap(then_body_, else_body_);

      auto if_true = (hint_ == BranchHint::kFalse) ? gasm_->MakeDeferredLabel()
                                                   : gasm_->MakeLabel();
      auto if_false = (hint_ == BranchHint::kTrue) ? gasm_->MakeDeferredLabel()
                                                   : gasm_->MakeLabel();
      auto merge = gasm_->MakeLabel(PhiMachineRepresentationOf<T>);
      if constexpr (std::is_same_v<Cond, Word32T>) {
        gasm_->MachineBranch(cond_, &if_true, &if_false, hint_);
      } else {
        static_assert(std::is_same_v<Cond, Boolean>);
        if (hint_ != BranchHint::kNone) {
          gasm_->BranchWithHint(cond_, &if_true, &if_false, hint_);
        } else {
          gasm_->Branch(cond_, &if_true, &if_false);
        }
      }

      gasm_->Bind(&if_true);
      TNode<T> then_result = then_body_();
      if (gasm_->HasActiveBlock()) gasm_->Goto(&merge, then_result);

      gasm_->Bind(&if_false);
      TNode<T> else_result = else_body_();
      if (gasm_->HasActiveBlock()) {
        gasm_->Goto(&merge, else_result);
      }

      gasm_->Bind(&merge);
      return merge.template PhiAt<T>(0);
    }

   private:
    static constexpr MachineRepresentation kPhiRepresentation =
        MachineRepresentation::kTagged;

    JSGraphAssembler* const gasm_;
    const TNode<Cond> cond_;
    const bool negate_cond_;
    BranchHint hint_ = BranchHint::kNone;
    If1BodyFunction then_body_;
    If1BodyFunction else_body_;
  };

  template <typename T>
  IfBuilder1<T, Boolean> SelectIf(TNode<Boolean> cond) {
    return {this, cond, false};
  }
  template <typename T>
  IfBuilder1<T, Boolean> SelectIfNot(TNode<Boolean> cond) {
    return {this, cond, true};
  }
  template <typename T>
  IfBuilder1<T, Word32T> MachineSelectIf(TNode<Word32T> cond) {
    return {this, cond, false};
  }

 protected:
  Operator const* PlainPrimitiveToNumberOperator();

 private:
  JSHeapBroker* broker_;
  JSGraph* jsgraph_;
  SetOncePointer<Operator const> to_number_operator_;

 protected:
  CatchScope outermost_catch_scope_;
  Node* outermost_handler_;
  CatchScope* catch_scope_;
  friend class CatchScope;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_GRAPH_ASSEMBLER_H_
