// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_INSTRUCTION_SELECTOR_ADAPTER_H_
#define V8_COMPILER_BACKEND_INSTRUCTION_SELECTOR_ADAPTER_H_

#include <optional>

#include "src/codegen/machine-type.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/compiler/schedule.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/operation-matcher.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/opmasks.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/use-map.h"


namespace v8::internal::compiler {
namespace detail {
template <typename...>
struct AnyTurbofanNodeOrBlock;
template <typename Head, typename... Tail>
struct AnyTurbofanNodeOrBlock<Head, Tail...> {
  static constexpr bool value = std::is_same_v<Head, Node*> ||
                                std::is_same_v<Head, BasicBlock*> ||
                                AnyTurbofanNodeOrBlock<Tail...>::value;
};
template <>
struct AnyTurbofanNodeOrBlock<> {
  static constexpr bool value = false;
};
}  // namespace detail

struct TurbofanAdapter {
  static constexpr bool IsTurbofan = true;
  static constexpr bool IsTurboshaft = false;
  static constexpr bool AllowsImplicitWord64ToWord32Truncation = false;
  using schedule_t = Schedule*;
  using block_t = BasicBlock*;
  using block_range_t = ZoneVector<block_t>;
  using node_t = Node*;
  using optional_node_t = Node*;
  using inputs_t = Node::Inputs;
  using opcode_t = IrOpcode::Value;
  using id_t = uint32_t;
  static_assert(std::is_same_v<NodeId, id_t>);
  using source_position_table_t = SourcePositionTable;

  explicit TurbofanAdapter(Schedule*) {}

  class ConstantView {
   public:
    explicit ConstantView(node_t node) : node_(node) {
      DCHECK(node_->opcode() == IrOpcode::kInt32Constant ||
             node_->opcode() == IrOpcode::kInt64Constant ||
             node_->opcode() == IrOpcode::kRelocatableInt32Constant ||
             node_->opcode() == IrOpcode::kRelocatableInt64Constant ||
             node_->opcode() == IrOpcode::kHeapConstant ||
             node_->opcode() == IrOpcode::kCompressedHeapConstant ||
             node_->opcode() == IrOpcode::kNumberConstant ||
             node_->opcode() == IrOpcode::kFloat32Constant ||
             node_->opcode() == IrOpcode::kFloat64Constant);
    }

    bool is_int32() const {
      return node_->opcode() == IrOpcode::kInt32Constant;
    }
    bool is_relocatable_int32() const {
      return node_->opcode() == IrOpcode::kRelocatableInt32Constant;
    }
    int32_t int32_value() const {
      if (is_int32()) return OpParameter<int32_t>(node_->op());
      DCHECK(is_relocatable_int32());
      RelocatablePtrConstantInfo constant_info =
          OpParameter<RelocatablePtrConstantInfo>(node_->op());
      DCHECK_EQ(RelocatablePtrConstantInfo::kInt32, constant_info.type());
      return static_cast<int32_t>(constant_info.value());
    }
    bool is_int64() const {
      return node_->opcode() == IrOpcode::kInt64Constant;
    }
    bool is_relocatable_int64() const {
      return node_->opcode() == IrOpcode::kRelocatableInt64Constant;
    }
    int64_t int64_value() const {
      if (is_int64()) return OpParameter<int64_t>(node_->op());
      DCHECK(is_relocatable_int64());
      RelocatablePtrConstantInfo constant_info =
          OpParameter<RelocatablePtrConstantInfo>(node_->op());
      DCHECK_EQ(RelocatablePtrConstantInfo::kInt64, constant_info.type());
      return constant_info.value();
    }
    bool is_heap_object() const {
      return node_->opcode() == IrOpcode::kHeapConstant;
    }
    bool is_compressed_heap_object() const {
      return node_->opcode() == IrOpcode::kCompressedHeapConstant;
    }
    Handle<HeapObject> heap_object_value() const {
      DCHECK(is_heap_object() || is_compressed_heap_object());
      return OpParameter<Handle<HeapObject>>(node_->op());
    }
    bool is_number() const {
      return node_->opcode() == IrOpcode::kNumberConstant;
    }
    bool is_number_zero() const {
      if (!is_number()) return false;
      return base::bit_cast<uint64_t>(OpParameter<double>(node_->op())) == 0;
    }
    bool is_float() const {
      return node_->opcode() == IrOpcode::kFloat32Constant ||
             node_->opcode() == IrOpcode::kFloat64Constant;
    }
    bool is_float_zero() const {
      if (!is_float()) return false;
      if (node_->opcode() == IrOpcode::kFloat32Constant) {
        return base::bit_cast<uint32_t>(OpParameter<float>(node_->op())) == 0;
      } else {
        return base::bit_cast<uint64_t>(OpParameter<double>(node_->op())) == 0;
      }
    }

    operator node_t() const { return node_; }

   private:
    node_t node_;
  };

  class CallView {
   public:
    explicit CallView(node_t node) : node_(node) {
      DCHECK(node_->opcode() == IrOpcode::kCall ||
             node_->opcode() == IrOpcode::kTailCall);
    }

    int return_count() const { return node_->op()->ValueOutputCount(); }
    node_t callee() const { return node_->InputAt(0); }
    node_t frame_state() const {
      return node_->InputAt(static_cast<int>(call_descriptor()->InputCount()));
    }
    base::Vector<node_t> arguments() const {
      base::Vector<node_t> inputs = node_->inputs_vector();
      return inputs.SubVector(1, inputs.size());
    }
    const CallDescriptor* call_descriptor() const {
      return CallDescriptorOf(node_->op());
    }

    operator node_t() const { return node_; }

   private:
    node_t node_;
  };

  class BranchView {
   public:
    explicit BranchView(node_t node) : node_(node) {
      DCHECK_EQ(node_->opcode(), IrOpcode::kBranch);
    }

    node_t condition() const { return node_->InputAt(0); }

    operator node_t() const { return node_; }

   private:
    node_t node_;
  };

  class WordBinopView {
   public:
    explicit WordBinopView(node_t node) : node_(node), m_(node) {}

    void EnsureConstantIsRightIfCommutative() {
      // Nothing to do. Matcher already ensures that.
    }

    node_t left() const { return m_.left().node(); }
    node_t right() const { return m_.right().node(); }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    Int32BinopMatcher m_;
  };

  class LoadView {
   public:
    explicit LoadView(node_t node) : node_(node) {}

    LoadRepresentation loaded_rep() const {
      if (is_atomic()) {
        return AtomicLoadParametersOf(node_->op()).representation();
#if V8_ENABLE_WEBASSEMBLY
      } else if (node_->opcode() == IrOpcode::kF64x2PromoteLowF32x4) {
        return LoadRepresentation::Simd128();
#endif  // V8_ENABLE_WEBASSEMBLY
      }
      return LoadRepresentationOf(node_->op());
    }
    bool is_protected(bool* traps_on_null) const {
      if (node_->opcode() == IrOpcode::kLoadTrapOnNull) {
        *traps_on_null = true;
        return true;
      }
      *traps_on_null = false;
      return node_->opcode() == IrOpcode::kProtectedLoad ||
             (is_atomic() && AtomicLoadParametersOf(node_->op()).kind() ==
                                 MemoryAccessKind::kProtected);
    }
    bool is_atomic() const {
      return node_->opcode() == IrOpcode::kWord32AtomicLoad ||
             node_->opcode() == IrOpcode::kWord64AtomicLoad;
    }

    node_t base() const { return node_->InputAt(0); }
    node_t index() const { return node_->InputAt(1); }
    int32_t displacement() const { return 0; }
    uint8_t element_size_log2() const { return 0; }

    operator node_t() const { return node_; }

   private:
    node_t node_;
  };

  class StoreView {
   public:
    explicit StoreView(node_t node) : node_(node) {
      DCHECK(node->opcode() == IrOpcode::kStore ||
             node->opcode() == IrOpcode::kProtectedStore ||
             node->opcode() == IrOpcode::kStoreTrapOnNull ||
             node->opcode() == IrOpcode::kStoreIndirectPointer ||
             node->opcode() == IrOpcode::kUnalignedStore ||
             node->opcode() == IrOpcode::kWord32AtomicStore ||
             node->opcode() == IrOpcode::kWord64AtomicStore);
    }

    StoreRepresentation stored_rep() const {
      switch (node_->opcode()) {
        case IrOpcode::kStore:
        case IrOpcode::kProtectedStore:
        case IrOpcode::kStoreTrapOnNull:
        case IrOpcode::kStoreIndirectPointer:
          return StoreRepresentationOf(node_->op());
        case IrOpcode::kUnalignedStore:
          return {UnalignedStoreRepresentationOf(node_->op()),
                  WriteBarrierKind::kNoWriteBarrier};
        case IrOpcode::kWord32AtomicStore:
        case IrOpcode::kWord64AtomicStore:
          return AtomicStoreParametersOf(node_->op()).store_representation();
        default:
          UNREACHABLE();
      }
    }
    std::optional<AtomicMemoryOrder> memory_order() const {
      switch (node_->opcode()) {
        case IrOpcode::kStore:
        case IrOpcode::kProtectedStore:
        case IrOpcode::kStoreTrapOnNull:
        case IrOpcode::kStoreIndirectPointer:
        case IrOpcode::kUnalignedStore:
          return std::nullopt;
        case IrOpcode::kWord32AtomicStore:
        case IrOpcode::kWord64AtomicStore:
          return AtomicStoreParametersOf(node_->op()).order();
        default:
          UNREACHABLE();
      }
    }
    MemoryAccessKind access_kind() const {
      switch (node_->opcode()) {
        case IrOpcode::kStore:
        case IrOpcode::kStoreIndirectPointer:
        case IrOpcode::kUnalignedStore:
          return MemoryAccessKind::kNormal;
        case IrOpcode::kProtectedStore:
        case IrOpcode::kStoreTrapOnNull:
          return MemoryAccessKind::kProtected;
        case IrOpcode::kWord32AtomicStore:
        case IrOpcode::kWord64AtomicStore:
          return AtomicStoreParametersOf(node_->op()).kind();
        default:
          UNREACHABLE();
      }
    }
    bool is_atomic() const {
      return node_->opcode() == IrOpcode::kWord32AtomicStore ||
             node_->opcode() == IrOpcode::kWord64AtomicStore;
    }

    node_t base() const { return node_->InputAt(0); }
    optional_node_t index() const { return node_->InputAt(1); }
    node_t value() const { return node_->InputAt(2); }
    // TODO(saelo): once we have turboshaft everywhere, we should convert this
    // to an operation parameter instead of an addition input (which is
    // currently required for turbofan, since all store opcodes are cached).
    IndirectPointerTag indirect_pointer_tag() const {
      DCHECK_EQ(node_->opcode(), IrOpcode::kStoreIndirectPointer);
      Node* tag = node_->InputAt(3);
      DCHECK_EQ(tag->opcode(), IrOpcode::kInt64Constant);
      return static_cast<IndirectPointerTag>(OpParameter<int64_t>(tag->op()));
    }
    int32_t displacement() const { return 0; }
    uint8_t element_size_log2() const { return 0; }

    bool is_store_trap_on_null() const {
      return node_->opcode() == IrOpcode::kStoreTrapOnNull;
    }

    operator node_t() const { return node_; }

   private:
    node_t node_;
  };

  class DeoptimizeView {
   public:
    explicit DeoptimizeView(node_t node) : node_(node) {
      DCHECK(node_->opcode() == IrOpcode::kDeoptimize ||
             node_->opcode() == IrOpcode::kDeoptimizeIf ||
             node_->opcode() == IrOpcode::kDeoptimizeUnless);
    }

    DeoptimizeReason reason() const {
      return DeoptimizeParametersOf(node_->op()).reason();
    }
    FeedbackSource feedback() const {
      return DeoptimizeParametersOf(node_->op()).feedback();
    }
    node_t frame_state() const {
      if (is_deoptimize()) {
        DCHECK_EQ(node_->InputAt(0)->opcode(), IrOpcode::kFrameState);
        return node_->InputAt(0);
      }
      DCHECK(is_deoptimize_if() || is_deoptimize_unless());
      DCHECK_EQ(node_->InputAt(1)->opcode(), IrOpcode::kFrameState);
      return node_->InputAt(1);
    }

    bool is_deoptimize() const {
      return node_->opcode() == IrOpcode::kDeoptimize;
    }
    bool is_deoptimize_if() const {
      return node_->opcode() == IrOpcode::kDeoptimizeIf;
    }
    bool is_deoptimize_unless() const {
      return node_->opcode() == IrOpcode::kDeoptimizeUnless;
    }

    node_t condition() const {
      DCHECK(is_deoptimize_if() || is_deoptimize_unless());
      return node_->InputAt(0);
    }

    operator node_t() const { return node_; }

   private:
    node_t node_;
  };

  class AtomicRMWView {
   public:
    explicit AtomicRMWView(node_t node) : node_(node) {
      DCHECK(node_->opcode() == IrOpcode::kWord32AtomicAdd ||
             node_->opcode() == IrOpcode::kWord32AtomicSub ||
             node_->opcode() == IrOpcode::kWord32AtomicAnd ||
             node_->opcode() == IrOpcode::kWord32AtomicOr ||
             node_->opcode() == IrOpcode::kWord32AtomicXor ||
             node_->opcode() == IrOpcode::kWord32AtomicExchange ||
             node_->opcode() == IrOpcode::kWord32AtomicCompareExchange ||
             node_->opcode() == IrOpcode::kWord64AtomicAdd ||
             node_->opcode() == IrOpcode::kWord64AtomicSub ||
             node_->opcode() == IrOpcode::kWord64AtomicAnd ||
             node_->opcode() == IrOpcode::kWord64AtomicOr ||
             node_->opcode() == IrOpcode::kWord64AtomicXor ||
             node_->opcode() == IrOpcode::kWord64AtomicExchange ||
             node_->opcode() == IrOpcode::kWord64AtomicCompareExchange);
    }

    node_t base() const { return node_->InputAt(0); }
    node_t index() const { return node_->InputAt(1); }
    node_t value() const {
      if (node_->opcode() == IrOpcode::kWord32AtomicCompareExchange ||
          node_->opcode() == IrOpcode::kWord64AtomicCompareExchange) {
        return node_->InputAt(3);
      }
      return node_->InputAt(2);
    }
    node_t expected() const {
      DCHECK(node_->opcode() == IrOpcode::kWord32AtomicCompareExchange ||
             node_->opcode() == IrOpcode::kWord64AtomicCompareExchange);
      return node_->InputAt(2);
    }

    operator node_t() const { return node_; }

   private:
    node_t node_;
  };

  class Word32AtomicPairStoreView {
   public:
    explicit Word32AtomicPairStoreView(node_t node) : node_(node) {
      DCHECK(node_->opcode() == IrOpcode::kWord32AtomicPairStore);
    }

    node_t base() const { return node_->InputAt(0); }
    node_t index() const { return node_->InputAt(1); }
    node_t value_low() const { return node_->InputAt(2); }
    node_t value_high() const { return node_->InputAt(3); }

   private:
    node_t node_;
  };

#if V8_ENABLE_WEBASSEMBLY
  class SimdShuffleView {
   public:
    explicit SimdShuffleView(node_t node) : node_(node) {
      DCHECK(node_->opcode() == IrOpcode::kI8x16Shuffle ||
             node_->opcode() == IrOpcode::kI8x32Shuffle);
    }

    bool isSimd128() const {
      return node_->opcode() == IrOpcode::kI8x16Shuffle;
    }

    const uint8_t* data() const {
      return isSimd128() ? S128ImmediateParameterOf(node_->op()).data()
                         : S256ImmediateParameterOf(node_->op()).data();
    }

    node_t input(int index) const {
      DCHECK_LT(index, node_->InputCount());
      return node_->InputAt(index);
    }

    void SwapInputs() {
      Node* input0 = node_->InputAt(0);
      Node* input1 = node_->InputAt(1);
      node_->ReplaceInput(0, input1);
      node_->ReplaceInput(1, input0);
    }

    void DuplicateFirstInput() { node_->ReplaceInput(1, node_->InputAt(0)); }

    operator node_t() const { return node_; }

   private:
    node_t node_;
  };
#endif

  bool is_constant(node_t node) const {
    switch (node->opcode()) {
      case IrOpcode::kInt32Constant:
      case IrOpcode::kInt64Constant:
      case IrOpcode::kRelocatableInt32Constant:
      case IrOpcode::kRelocatableInt64Constant:
      case IrOpcode::kHeapConstant:
      case IrOpcode::kCompressedHeapConstant:
      case IrOpcode::kNumberConstant:
      case IrOpcode::kFloat32Constant:
      case IrOpcode::kFloat64Constant:
        // For those, a view must be constructible.
        DCHECK_EQ(constant_view(node), node);
        return true;
      default:
        return false;
    }
  }
  bool is_load(node_t node) const {
    switch (node->opcode()) {
      case IrOpcode::kLoad:
      case IrOpcode::kLoadImmutable:
      case IrOpcode::kProtectedLoad:
      case IrOpcode::kLoadTrapOnNull:
      case IrOpcode::kWord32AtomicLoad:
      case IrOpcode::kWord64AtomicLoad:
      case IrOpcode::kUnalignedLoad:
#if V8_ENABLE_WEBASSEMBLY
      case IrOpcode::kLoadTransform:
      case IrOpcode::kF64x2PromoteLowF32x4:
#endif  // V8_ENABLE_WEBASSEMBLY
        return true;
      default:
        return false;
    }
  }
  bool is_load_root_register(node_t node) const {
    return node->opcode() == IrOpcode::kLoadRootRegister;
  }
  ConstantView constant_view(node_t node) const { return ConstantView{node}; }
  CallView call_view(node_t node) { return CallView{node}; }
  BranchView branch_view(node_t node) { return BranchView(node); }
  WordBinopView word_binop_view(node_t node) { return WordBinopView(node); }
  LoadView load_view(node_t node) {
    DCHECK(is_load(node));
    return LoadView(node);
  }
  StoreView store_view(node_t node) { return StoreView(node); }
  DeoptimizeView deoptimize_view(node_t node) { return DeoptimizeView(node); }
  AtomicRMWView atomic_rmw_view(node_t node) { return AtomicRMWView(node); }
  Word32AtomicPairStoreView word32_atomic_pair_store_view(node_t node) {
    return Word32AtomicPairStoreView(node);
  }
#if V8_ENABLE_WEBASSEMBLY
  SimdShuffleView simd_shuffle_view(node_t node) {
    return SimdShuffleView(node);
  }
#endif

  block_t block(schedule_t schedule, node_t node) const {
    return schedule->block(node);
  }

  RpoNumber rpo_number(block_t block) const {
    return RpoNumber::FromInt(block->rpo_number());
  }

  const block_range_t& rpo_order(schedule_t schedule) const {
    return *schedule->rpo_order();
  }

  bool IsLoopHeader(block_t block) const { return block->IsLoopHeader(); }

  size_t PredecessorCount(block_t block) const {
    return block->PredecessorCount();
  }
  block_t PredecessorAt(block_t block, size_t index) const {
    return block->PredecessorAt(index);
  }

  base::iterator_range<NodeVector::iterator> nodes(block_t block) {
    return {block->begin(), block->end()};
  }

  bool IsPhi(node_t node) const { return node->opcode() == IrOpcode::kPhi; }
  MachineRepresentation phi_representation_of(node_t node) const {
    DCHECK(IsPhi(node));
    return PhiRepresentationOf(node->op());
  }
  bool IsRetain(node_t node) const {
    return node->opcode() == IrOpcode::kRetain;
  }
  bool IsHeapConstant(node_t node) const {
    return node->opcode() == IrOpcode::kHeapConstant;
  }
  bool IsExternalConstant(node_t node) const {
    return node->opcode() == IrOpcode::kExternalConstant;
  }
  bool IsRelocatableWasmConstant(node_t node) const {
    return node->opcode() == IrOpcode::kRelocatableInt32Constant ||
           node->opcode() == IrOpcode::kRelocatableInt64Constant;
  }
  bool IsLoadOrLoadImmutable(node_t node) const {
    return node->opcode() == IrOpcode::kLoad ||
           node->opcode() == IrOpcode::kLoadImmutable;
  }

  int value_input_count(node_t node) const {
    return node->op()->ValueInputCount();
  }
  node_t input_at(node_t node, size_t index) const {
    return node->InputAt(static_cast<int>(index));
  }
  inputs_t inputs(node_t node) const { return node->inputs(); }
  opcode_t opcode(node_t node) const { return node->opcode(); }
  bool is_exclusive_user_of(node_t user, node_t value) const {
    for (Edge const edge : value->use_edges()) {
      if (edge.from() != user && NodeProperties::IsValueEdge(edge)) {
        return false;
      }
    }
    return true;
  }

  id_t id(node_t node) const { return node->id(); }
  static bool valid(node_t node) { return node != nullptr; }
  static node_t value(optional_node_t node) {
    DCHECK(valid(node));
    return node;
  }

  node_t block_terminator(block_t block) const {
    return block->control_input();
  }
  node_t parent_frame_state(node_t node) const {
    DCHECK_EQ(node->opcode(), IrOpcode::kFrameState);
    DCHECK_EQ(FrameState{node}.outer_frame_state(),
              NodeProperties::GetFrameStateInput(node));
    return NodeProperties::GetFrameStateInput(node);
  }
  int parameter_index_of(node_t node) const {
    DCHECK_EQ(node->opcode(), IrOpcode::kParameter);
    return ParameterIndexOf(node->op());
  }
  bool is_projection(node_t node) const {
    return node->opcode() == IrOpcode::kProjection;
  }
  size_t projection_index_of(node_t node) const {
    DCHECK(is_projection(node));
    return ProjectionIndexOf(node->op());
  }
  int osr_value_index_of(node_t node) const {
    DCHECK_EQ(node->opcode(), IrOpcode::kOsrValue);
    return OsrValueIndexOf(node->op());
  }

  bool is_truncate_word64_to_word32(node_t node) const {
    return node->opcode() == IrOpcode::kTruncateInt64ToInt32;
  }

  bool is_stack_slot(node_t node) const {
    return node->opcode() == IrOpcode::kStackSlot;
  }
  StackSlotRepresentation stack_slot_representation_of(node_t node) const {
    DCHECK(is_stack_slot(node));
    return StackSlotRepresentationOf(node->op());
  }
  bool is_integer_constant(node_t node) const {
    return node->opcode() == IrOpcode::kInt32Constant ||
           node->opcode() == IrOpcode::kInt64Constant;
  }
  int64_t integer_constant(node_t node) const {
    if (node->opcode() == IrOpcode::kInt32Constant) {
      return OpParameter<int32_t>(node->op());
    }
    DCHECK_EQ(node->opcode(), IrOpcode::kInt64Constant);
    return OpParameter<int64_t>(node->op());
  }

  bool IsRequiredWhenUnused(node_t node) const {
    return !node->op()->HasProperty(Operator::kEliminatable);
  }
  bool IsCommutative(node_t node) const {
    return node->op()->HasProperty(Operator::kCommutative);
  }
};

struct TurboshaftAdapter : public turboshaft::OperationMatcher {
  static constexpr bool IsTurbofan = false;
  static constexpr bool IsTurboshaft = true;
  static constexpr bool AllowsImplicitWord64ToWord32Truncation = true;
  // TODO(nicohartmann@): Rename schedule_t once Turbofan is gone.
  using schedule_t = turboshaft::Graph*;
  using block_t = turboshaft::Block*;
  using block_range_t = ZoneVector<block_t>;
  using node_t = turboshaft::OpIndex;
  using optional_node_t = turboshaft::OptionalOpIndex;
  using inputs_t = base::Vector<const node_t>;
  using opcode_t = turboshaft::Opcode;
  using id_t = uint32_t;
  using source_position_table_t =
      turboshaft::GrowingOpIndexSidetable<SourcePosition>;

  explicit TurboshaftAdapter(turboshaft::Graph* graph)
      : turboshaft::OperationMatcher(*graph), graph_(graph) {}

  class ConstantView {
    using Kind = turboshaft::ConstantOp::Kind;

   public:
    ConstantView(turboshaft::Graph* graph, node_t node) : node_(node) {
      op_ = &graph->Get(node_).Cast<turboshaft::ConstantOp>();
    }

    bool is_int32() const {
      return op_->kind == Kind::kWord32 || (op_->kind == Kind::kSmi && !Is64());
    }
    bool is_relocatable_int32() const {
      // We don't have this in turboshaft currently.
      return false;
    }
    int32_t int32_value() const {
      DCHECK(is_int32() || is_relocatable_int32());
      if (op_->kind == Kind::kWord32) {
        return op_->word32();
      } else {
        DCHECK_EQ(op_->kind, Kind::kSmi);
        DCHECK(!Is64());
        return static_cast<int32_t>(op_->smi().ptr());
      }
    }
    bool is_int64() const {
      return op_->kind == Kind::kWord64 || (op_->kind == Kind::kSmi && Is64());
    }
    bool is_relocatable_int64() const {
      return op_->kind == Kind::kRelocatableWasmCall ||
             op_->kind == Kind::kRelocatableWasmStubCall;
    }
    int64_t int64_value() const {
      if (op_->kind == Kind::kWord64) {
        return op_->word64();
      } else if (op_->kind == Kind::kSmi) {
        DCHECK(Is64());
        return static_cast<int64_t>(op_->smi().ptr());
      } else {
        DCHECK(is_relocatable_int64());
        return static_cast<int64_t>(op_->integral());
      }
    }
    bool is_heap_object() const { return op_->kind == Kind::kHeapObject; }
    bool is_compressed_heap_object() const {
      return op_->kind == Kind::kCompressedHeapObject;
    }
    Handle<HeapObject> heap_object_value() const {
      DCHECK(is_heap_object() || is_compressed_heap_object());
      return op_->handle();
    }
    bool is_number() const { return op_->kind == Kind::kNumber; }
    bool is_number_zero() const {
      if (!is_number()) return false;
      return op_->number().get_bits() == 0;
    }
    bool is_float() const {
      return op_->kind == Kind::kFloat32 || op_->kind == Kind::kFloat64;
    }
    bool is_float_zero() const {
      if (!is_float()) return false;
      if (op_->kind == Kind::kFloat32) {
        return op_->float32().get_bits() == 0;
      } else {
        return op_->float64().get_bits() == 0;
      }
    }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    const turboshaft::ConstantOp* op_;
  };

  class CallView {
   public:
    explicit CallView(turboshaft::Graph* graph, node_t node) : node_(node) {
      call_op_ = graph->Get(node_).TryCast<turboshaft::CallOp>();
      if (call_op_ != nullptr) return;
      tail_call_op_ = graph->Get(node_).TryCast<turboshaft::TailCallOp>();
      if (tail_call_op_ != nullptr) return;
      UNREACHABLE();
    }

    int return_count() const {
      if (call_op_) {
        return static_cast<int>(call_op_->results_rep().size());
      }
      if (tail_call_op_) {
        return static_cast<int>(tail_call_op_->outputs_rep().size());
      }
      UNREACHABLE();
    }
    node_t callee() const {
      if (call_op_) return call_op_->callee();
      if (tail_call_op_) return tail_call_op_->callee();
      UNREACHABLE();
    }
    node_t frame_state() const {
      if (call_op_) return call_op_->frame_state().value();
      UNREACHABLE();
    }
    base::Vector<const node_t> arguments() const {
      if (call_op_) return call_op_->arguments();
      if (tail_call_op_) return tail_call_op_->arguments();
      UNREACHABLE();
    }
    const CallDescriptor* call_descriptor() const {
      if (call_op_) return call_op_->descriptor->descriptor;
      if (tail_call_op_) return tail_call_op_->descriptor->descriptor;
      UNREACHABLE();
    }

    const turboshaft::TSCallDescriptor* ts_call_descriptor() const {
      if (call_op_) return call_op_->descriptor;
      if (tail_call_op_) return tail_call_op_->descriptor;
      UNREACHABLE();
    }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    const turboshaft::CallOp* call_op_;
    const turboshaft::TailCallOp* tail_call_op_;
  };

  class BranchView {
   public:
    explicit BranchView(turboshaft::Graph* graph, node_t node) : node_(node) {
      op_ = &graph->Get(node_).Cast<turboshaft::BranchOp>();
    }

    node_t condition() const { return op_->condition(); }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    const turboshaft::BranchOp* op_;
  };

  class WordBinopView {
   public:
    explicit WordBinopView(turboshaft::Graph* graph, node_t node)
        : node_(node) {
      op_ = &graph->Get(node_).Cast<turboshaft::WordBinopOp>();
      left_ = op_->left();
      right_ = op_->right();
      can_put_constant_right_ =
          op_->IsCommutative(op_->kind) &&
          graph->Get(left_).Is<turboshaft::ConstantOp>() &&
          !graph->Get(right_).Is<turboshaft::ConstantOp>();
    }

    void EnsureConstantIsRightIfCommutative() {
      if (can_put_constant_right_) {
        std::swap(left_, right_);
        can_put_constant_right_ = false;
      }
    }

    node_t left() const { return left_; }
    node_t right() const { return right_; }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    const turboshaft::WordBinopOp* op_;
    node_t left_;
    node_t right_;
    bool can_put_constant_right_;
  };

  class LoadView {
   public:
    LoadView(turboshaft::Graph* graph, node_t node) : node_(node) {
      switch (graph->Get(node_).opcode) {
        case opcode_t::kLoad:
          load_ = &graph->Get(node_).Cast<turboshaft::LoadOp>();
          break;
#if V8_ENABLE_WEBASSEMBLY
        case opcode_t::kSimd128LoadTransform:
          load_transform_ =
              &graph->Get(node_).Cast<turboshaft::Simd128LoadTransformOp>();
          break;
#if V8_ENABLE_WASM_SIMD256_REVEC
        case opcode_t::kSimd256LoadTransform:
          load_transform256_ =
              &graph->Get(node_).Cast<turboshaft::Simd256LoadTransformOp>();
          break;
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#endif  // V8_ENABLE_WEBASSEMBLY
        default:
          UNREACHABLE();
      }
    }
    LoadRepresentation loaded_rep() const {
      DCHECK_NOT_NULL(load_);
      return load_->machine_type();
    }
    turboshaft::MemoryRepresentation ts_loaded_rep() const {
      DCHECK_NOT_NULL(load_);
      return load_->loaded_rep;
    }
    turboshaft::RegisterRepresentation ts_result_rep() const {
      DCHECK_NOT_NULL(load_);
      return load_->result_rep;
    }
    bool is_protected(bool* traps_on_null) const {
      if (kind().with_trap_handler) {
        if (load_) {
          *traps_on_null = load_->kind.trap_on_null;
#if V8_ENABLE_WEBASSEMBLY
        } else {
#if V8_ENABLE_WASM_SIMD256_REVEC
          DCHECK(
              (load_transform_ && !load_transform_->load_kind.trap_on_null) ||
              (load_transform256_ &&
               !load_transform256_->load_kind.trap_on_null));
#else
          DCHECK(load_transform_);
          DCHECK(!load_transform_->load_kind.trap_on_null);
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
          *traps_on_null = false;
#endif  // V8_ENABLE_WEBASSEMBLY
        }
        return true;
      }
      return false;
    }
    bool is_atomic() const { return kind().is_atomic; }

    node_t base() const {
      if (load_) return load_->base();
#if V8_ENABLE_WEBASSEMBLY
      if (load_transform_) return load_transform_->base();
#if V8_ENABLE_WASM_SIMD256_REVEC
      if (load_transform256_) return load_transform256_->base();
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#endif
      UNREACHABLE();
    }
    node_t index() const {
      if (load_) return load_->index().value_or_invalid();
#if V8_ENABLE_WEBASSEMBLY
      if (load_transform_) return load_transform_->index();
#if V8_ENABLE_WASM_SIMD256_REVEC
      if (load_transform256_) return load_transform256_->index();
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#endif
      UNREACHABLE();
    }
    int32_t displacement() const {
      static_assert(
          std::is_same_v<decltype(turboshaft::StoreOp::offset), int32_t>);
      if (load_) {
        int32_t offset = load_->offset;
        if (load_->kind.tagged_base) {
          CHECK_GE(offset,
                   std::numeric_limits<int32_t>::min() + kHeapObjectTag);
          offset -= kHeapObjectTag;
        }
        return offset;
#if V8_ENABLE_WEBASSEMBLY
      } else if (load_transform_) {
        int32_t offset = load_transform_->offset;
        DCHECK(!load_transform_->load_kind.tagged_base);
        return offset;
#if V8_ENABLE_WASM_SIMD256_REVEC
      } else if (load_transform256_) {
        int32_t offset = load_transform256_->offset;
        DCHECK(!load_transform256_->load_kind.tagged_base);
        return offset;
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#endif
      }
      UNREACHABLE();
    }
    uint8_t element_size_log2() const {
      static_assert(
          std::is_same_v<decltype(turboshaft::StoreOp::element_size_log2),
                         uint8_t>);
      if (load_) return load_->element_size_log2;
#if V8_ENABLE_WEBASSEMBLY
      if (load_transform_) return 0;
#if V8_ENABLE_WASM_SIMD256_REVEC
      if (load_transform256_) return 0;
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#endif
      UNREACHABLE();
    }

    operator node_t() const { return node_; }

   private:
    turboshaft::LoadOp::Kind kind() const {
      if (load_) return load_->kind;
#if V8_ENABLE_WEBASSEMBLY
      if (load_transform_) return load_transform_->load_kind;
#if V8_ENABLE_WASM_SIMD256_REVEC
      if (load_transform256_) return load_transform256_->load_kind;
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#endif
      UNREACHABLE();
    }

    node_t node_;
    const turboshaft::LoadOp* load_ = nullptr;
#if V8_ENABLE_WEBASSEMBLY
    const turboshaft::Simd128LoadTransformOp* load_transform_ = nullptr;
#if V8_ENABLE_WASM_SIMD256_REVEC
    const turboshaft::Simd256LoadTransformOp* load_transform256_ = nullptr;
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#endif
  };

  class StoreView {
   public:
    StoreView(turboshaft::Graph* graph, node_t node) : node_(node) {
      op_ = &graph->Get(node_).Cast<turboshaft::StoreOp>();
    }

    StoreRepresentation stored_rep() const {
      return {op_->stored_rep.ToMachineType().representation(),
              op_->write_barrier};
    }
    turboshaft::MemoryRepresentation ts_stored_rep() const {
      return op_->stored_rep;
    }
    std::optional<AtomicMemoryOrder> memory_order() const {
      // TODO(nicohartmann@): Currently we don't support memory orders.
      if (op_->kind.is_atomic) return AtomicMemoryOrder::kSeqCst;
      return std::nullopt;
    }
    MemoryAccessKind access_kind() const {
      return op_->kind.with_trap_handler ? MemoryAccessKind::kProtected
                                         : MemoryAccessKind::kNormal;
    }
    bool is_atomic() const { return op_->kind.is_atomic; }

    node_t base() const { return op_->base(); }
    optional_node_t index() const { return op_->index(); }
    node_t value() const { return op_->value(); }
    IndirectPointerTag indirect_pointer_tag() const {
      return static_cast<IndirectPointerTag>(op_->indirect_pointer_tag());
    }
    int32_t displacement() const {
      static_assert(
          std::is_same_v<decltype(turboshaft::StoreOp::offset), int32_t>);
      int32_t offset = op_->offset;
      if (op_->kind.tagged_base) {
        CHECK_GE(offset, std::numeric_limits<int32_t>::min() + kHeapObjectTag);
        offset -= kHeapObjectTag;
      }
      return offset;
    }
    uint8_t element_size_log2() const {
      static_assert(
          std::is_same_v<decltype(turboshaft::StoreOp::element_size_log2),
                         uint8_t>);
      return op_->element_size_log2;
    }

    bool is_store_trap_on_null() const {
      return op_->kind.with_trap_handler && op_->kind.trap_on_null;
    }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    const turboshaft::StoreOp* op_;
  };

  class DeoptimizeView {
   public:
    DeoptimizeView(const turboshaft::Graph* graph, node_t node) : node_(node) {
      const auto& op = graph->Get(node);
      if (op.Is<turboshaft::DeoptimizeOp>()) {
        deoptimize_op_ = &op.Cast<turboshaft::DeoptimizeOp>();
        parameters_ = deoptimize_op_->parameters;
      } else {
        DCHECK(op.Is<turboshaft::DeoptimizeIfOp>());
        deoptimize_if_op_ = &op.Cast<turboshaft::DeoptimizeIfOp>();
        parameters_ = deoptimize_if_op_->parameters;
      }
    }

    DeoptimizeReason reason() const { return parameters_->reason(); }
    FeedbackSource feedback() const { return parameters_->feedback(); }
    node_t frame_state() const {
      return deoptimize_op_ ? deoptimize_op_->frame_state()
                            : deoptimize_if_op_->frame_state();
    }

    bool is_deoptimize() const { return deoptimize_op_ != nullptr; }
    bool is_deoptimize_if() const {
      return deoptimize_if_op_ != nullptr && !deoptimize_if_op_->negated;
    }
    bool is_deoptimize_unless() const {
      return deoptimize_if_op_ != nullptr && deoptimize_if_op_->negated;
    }

    node_t condition() const {
      DCHECK(is_deoptimize_if() || is_deoptimize_unless());
      return deoptimize_if_op_->condition();
    }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    const turboshaft::DeoptimizeOp* deoptimize_op_ = nullptr;
    const turboshaft::DeoptimizeIfOp* deoptimize_if_op_ = nullptr;
    const DeoptimizeParameters* parameters_;
  };

  class AtomicRMWView {
   public:
    AtomicRMWView(const turboshaft::Graph* graph, node_t node) : node_(node) {
      op_ = &graph->Get(node).Cast<turboshaft::AtomicRMWOp>();
    }

    node_t base() const { return op_->base(); }
    node_t index() const { return op_->index(); }
    node_t value() const { return op_->value(); }
    node_t expected() const {
      DCHECK_EQ(op_->bin_op, turboshaft::AtomicRMWOp::BinOp::kCompareExchange);
      return op_->expected().value_or_invalid();
    }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    const turboshaft::AtomicRMWOp* op_;
  };

  class Word32AtomicPairStoreView {
   public:
    explicit Word32AtomicPairStoreView(const turboshaft::Graph* graph,
                                       node_t node)
        : store_(graph->Get(node).Cast<turboshaft::AtomicWord32PairOp>()) {}

    node_t base() const { return store_.base(); }
    node_t index() const { return store_.index().value(); }
    node_t value_low() const { return store_.value_low().value(); }
    node_t value_high() const { return store_.value_high().value(); }

   private:
    const turboshaft::AtomicWord32PairOp& store_;
  };

#if V8_ENABLE_WEBASSEMBLY
  class SimdShuffleView {
   public:
    explicit SimdShuffleView(const turboshaft::Graph* graph, node_t node)
        : node_(node) {
      op128_ = &graph->Get(node).Cast<turboshaft::Simd128ShuffleOp>();
      // Initialize input mapping.
      for (int i = 0; i < op128_->input_count; ++i) {
        input_mapping_.push_back(i);
      }
    }

    bool isSimd128() const {
      // TODO(nicohartmann@): Extend when we add support for Simd256.
      return true;
    }

    const uint8_t* data() const { return op128_->shuffle; }

    node_t input(int index) const {
      DCHECK_LT(index, op128_->input_count);
      return op128_->input(input_mapping_[index]);
    }

    void SwapInputs() { std::swap(input_mapping_[0], input_mapping_[1]); }

    void DuplicateFirstInput() {
      DCHECK_LE(2, input_mapping_.size());
      input_mapping_[1] = input_mapping_[0];
    }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    base::SmallVector<int, 2> input_mapping_;
    const turboshaft::Simd128ShuffleOp* op128_;
  };
#endif

  bool is_constant(node_t node) const {
    return graph_->Get(node).Is<turboshaft::ConstantOp>();
  }
  bool is_load(node_t node) const {
    return graph_->Get(node).Is<turboshaft::LoadOp>()
#if V8_ENABLE_WEBASSEMBLY
           || graph_->Get(node).Is<turboshaft::Simd128LoadTransformOp>()
#if V8_ENABLE_WASM_SIMD256_REVEC
           || graph_->Get(node).Is<turboshaft::Simd256LoadTransformOp>()
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#endif
        ;
  }
  bool is_load_root_register(node_t node) const {
    return graph_->Get(node).Is<turboshaft::LoadRootRegisterOp>();
  }
  ConstantView constant_view(node_t node) { return ConstantView{graph_, node}; }
  CallView call_view(node_t node) { return CallView{graph_, node}; }
  BranchView branch_view(node_t node) { return BranchView(graph_, node); }
  WordBinopView word_binop_view(node_t node) {
    return WordBinopView(graph_, node);
  }
  LoadView load_view(node_t node) {
    DCHECK(is_load(node));
    return LoadView(graph_, node);
  }
  StoreView store_view(node_t node) { return StoreView(graph_, node); }
  DeoptimizeView deoptimize_view(node_t node) {
    return DeoptimizeView(graph_, node);
  }
  AtomicRMWView atomic_rmw_view(node_t node) {
    return AtomicRMWView(graph_, node);
  }
  Word32AtomicPairStoreView word32_atomic_pair_store_view(node_t node) {
    return Word32AtomicPairStoreView(graph_, node);
  }
#if V8_ENABLE_WEBASSEMBLY
  SimdShuffleView simd_shuffle_view(node_t node) {
    return SimdShuffleView(graph_, node);
  }
#endif

  turboshaft::Graph* turboshaft_graph() const { return graph_; }

  block_t block(schedule_t schedule, node_t node) const {
    // TODO(nicohartmann@): This might be too slow and we should consider
    // precomputing.
    return &schedule->Get(schedule->BlockOf(node));
  }

  RpoNumber rpo_number(block_t block) const {
    return RpoNumber::FromInt(block->index().id());
  }

  const block_range_t& rpo_order(schedule_t schedule) {
    return schedule->blocks_vector();
  }

  bool IsLoopHeader(block_t block) const { return block->IsLoop(); }

  size_t PredecessorCount(block_t block) const {
    return block->PredecessorCount();
  }
  block_t PredecessorAt(block_t block, size_t index) const {
    return block->Predecessors()[index];
  }

  base::iterator_range<turboshaft::Graph::OpIndexIterator> nodes(
      block_t block) {
    return graph_->OperationIndices(*block);
  }

  bool IsPhi(node_t node) const {
    return graph_->Get(node).Is<turboshaft::PhiOp>();
  }
  MachineRepresentation phi_representation_of(node_t node) const {
    DCHECK(IsPhi(node));
    const turboshaft::PhiOp& phi = graph_->Get(node).Cast<turboshaft::PhiOp>();
    return phi.rep.machine_representation();
  }
  bool IsRetain(node_t node) const {
    return graph_->Get(node).Is<turboshaft::RetainOp>();
  }
  bool IsHeapConstant(node_t node) const {
    turboshaft::ConstantOp* constant =
        graph_->Get(node).TryCast<turboshaft::ConstantOp>();
    if (constant == nullptr) return false;
    return constant->kind == turboshaft::ConstantOp::Kind::kHeapObject;
  }
  bool IsExternalConstant(node_t node) const {
    turboshaft::ConstantOp* constant =
        graph_->Get(node).TryCast<turboshaft::ConstantOp>();
    if (constant == nullptr) return false;
    return constant->kind == turboshaft::ConstantOp::Kind::kExternal;
  }
  bool IsRelocatableWasmConstant(node_t node) const {
    turboshaft::ConstantOp* constant =
        graph_->Get(node).TryCast<turboshaft::ConstantOp>();
    if (constant == nullptr) return false;
    return constant->kind ==
           turboshaft::any_of(
               turboshaft::ConstantOp::Kind::kRelocatableWasmCall,
               turboshaft::ConstantOp::Kind::kRelocatableWasmStubCall);
  }
  bool IsLoadOrLoadImmutable(node_t node) const {
    return graph_->Get(node).opcode == turboshaft::Opcode::kLoad;
  }
  bool IsProtectedLoad(node_t node) const {
#if V8_ENABLE_WEBASSEMBLY
    if (graph_->Get(node).opcode == turboshaft::Opcode::kSimd128LoadTransform) {
      return true;
    }
#if V8_ENABLE_WASM_SIMD256_REVEC
    if (graph_->Get(node).opcode == turboshaft::Opcode::kSimd256LoadTransform) {
      return true;
    }
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#endif  // V8_ENABLE_WEBASSEMBLY

    if (!IsLoadOrLoadImmutable(node)) return false;

    bool traps_on_null;
    return LoadView(graph_, node).is_protected(&traps_on_null);
  }

  int value_input_count(node_t node) const {
    return graph_->Get(node).input_count;
  }
  node_t input_at(node_t node, size_t index) const {
    return graph_->Get(node).input(index);
  }
  inputs_t inputs(node_t node) const { return graph_->Get(node).inputs(); }
  opcode_t opcode(node_t node) const { return graph_->Get(node).opcode; }
  bool is_exclusive_user_of(node_t user, node_t value) const {
    DCHECK(valid(user));
    DCHECK(valid(value));
    const turboshaft::Operation& value_op = graph_->Get(value);
    const turboshaft::Operation& user_op = graph_->Get(user);
    size_t use_count = base::count_if(
        user_op.inputs(),
        [value](turboshaft::OpIndex input) { return input == value; });
    if (V8_UNLIKELY(use_count == 0)) {
      // We have a special case here:
      //
      //         value
      //           |
      // TruncateWord64ToWord32
      //           |
      //         user
      //
      // If emitting user performs the truncation implicitly, we end up calling
      // CanCover with value and user such that user might have no (direct) uses
      // of value. There are cases of other unnecessary operations that can lead
      // to the same situation (e.g. bitwise and, ...). In this case, we still
      // cover if value has only a single use and this is one of the direct
      // inputs of user, which also only has a single use (in user).
      // TODO(nicohartmann@): We might generalize this further if we see use
      // cases.
      if (!value_op.saturated_use_count.IsOne()) return false;
      for (auto input : user_op.inputs()) {
        const turboshaft::Operation& input_op = graph_->Get(input);
        const size_t indirect_use_count = base::count_if(
            input_op.inputs(),
            [value](turboshaft::OpIndex input) { return input == value; });
        if (indirect_use_count > 0) {
          return input_op.saturated_use_count.IsOne();
        }
      }
      return false;
    }
    if (value_op.Is<turboshaft::ProjectionOp>()) {
      // Projections always have a Tuple use, but it shouldn't count as a use as
      // far as is_exclusive_user_of is concerned, since no instructions are
      // emitted for the TupleOp, which is just a Turboshaft "meta operation".
      // We thus increase the use_count by 1, to attribute the TupleOp use to
      // the current operation.
      use_count++;
    }
    DCHECK_LE(use_count, graph_->Get(value).saturated_use_count.Get());
    return (value_op.saturated_use_count.Get() == use_count) &&
           !value_op.saturated_use_count.IsSaturated();
  }

  id_t id(node_t node) const { return node.id(); }
  static bool valid(node_t node) { return node.valid(); }
  static bool valid(optional_node_t node) { return node.valid(); }
  static node_t value(optional_node_t node) {
    DCHECK(valid(node));
    return node.value();
  }

  node_t block_terminator(block_t block) const {
    return graph_->PreviousIndex(block->end());
  }
  node_t parent_frame_state(node_t node) const {
    const turboshaft::FrameStateOp& frame_state =
        graph_->Get(node).Cast<turboshaft::FrameStateOp>();
    return frame_state.parent_frame_state();
  }
  int parameter_index_of(node_t node) const {
    const turboshaft::ParameterOp& parameter =
        graph_->Get(node).Cast<turboshaft::ParameterOp>();
    return parameter.parameter_index;
  }
  bool is_projection(node_t node) const {
    return graph_->Get(node).Is<turboshaft::ProjectionOp>();
  }
  size_t projection_index_of(node_t node) const {
    DCHECK(is_projection(node));
    const turboshaft::ProjectionOp& projection =
        graph_->Get(node).Cast<turboshaft::ProjectionOp>();
    return projection.index;
  }
  int osr_value_index_of(node_t node) const {
    const turboshaft::OsrValueOp& osr_value =
        graph_->Get(node).Cast<turboshaft::OsrValueOp>();
    return osr_value.index;
  }

  bool is_truncate_word64_to_word32(node_t node) const {
    return graph_->Get(node).Is<turboshaft::Opmask::kTruncateWord64ToWord32>();
  }
  node_t remove_truncate_word64_to_word32(node_t node) const {
    if (const turboshaft::ChangeOp* change =
            graph_->Get(node)
                .TryCast<turboshaft::Opmask::kTruncateWord64ToWord32>()) {
      return change->input();
    }
    return node;
  }

  bool is_stack_slot(node_t node) const {
    return graph_->Get(node).Is<turboshaft::StackSlotOp>();
  }
  StackSlotRepresentation stack_slot_representation_of(node_t node) const {
    DCHECK(is_stack_slot(node));
    const turboshaft::StackSlotOp& stack_slot =
        graph_->Get(node).Cast<turboshaft::StackSlotOp>();
    return StackSlotRepresentation(stack_slot.size, stack_slot.alignment,
                                   stack_slot.is_tagged);
  }
  bool is_integer_constant(node_t node) const {
    if (const auto constant =
            graph_->Get(node).TryCast<turboshaft::ConstantOp>()) {
      return constant->kind == turboshaft::ConstantOp::Kind::kWord32 ||
             constant->kind == turboshaft::ConstantOp::Kind::kWord64;
    }
    return false;
  }
  int64_t integer_constant(node_t node) const {
    const turboshaft::ConstantOp* constant =
        graph_->Get(node).TryCast<turboshaft::ConstantOp>();
    DCHECK_NOT_NULL(constant);
    return constant->signed_integral();
  }

  bool IsRequiredWhenUnused(node_t node) const {
    return graph_->Get(node).IsRequiredWhenUnused();
  }
  bool IsCommutative(node_t node) const {
    const turboshaft::Operation& op = graph_->Get(node);
    if (const auto binop = op.TryCast<turboshaft::WordBinopOp>()) {
      return turboshaft::WordBinopOp::IsCommutative(binop->kind);
    } else if (const auto binop =
                   op.TryCast<turboshaft::OverflowCheckedBinopOp>()) {
      return turboshaft::OverflowCheckedBinopOp::IsCommutative(binop->kind);
    } else if (const auto binop = op.TryCast<turboshaft::FloatBinopOp>()) {
      return turboshaft::FloatBinopOp::IsCommutative(binop->kind);
    } else if (const auto comparison = op.TryCast<turboshaft::ComparisonOp>()) {
      return turboshaft::ComparisonOp::IsCommutative(comparison->kind);
    }
    return false;
  }

 private:
  turboshaft::Graph* graph_;
};

}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_BACKEND_INSTRUCTION_SELECTOR_ADAPTER_H_
