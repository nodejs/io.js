// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/bits.h"
#include "src/base/logging.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/backend/instruction-selector-impl.h"
#include "src/compiler/backend/riscv/instruction-selector-riscv.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/opmasks.h"

namespace v8 {
namespace internal {
namespace compiler {
template <typename Adapter>
int64_t RiscvOperandGeneratorT<Adapter>::GetIntegerConstantValue(Node* node) {
  if (node->opcode() == IrOpcode::kInt32Constant) {
    return OpParameter<int32_t>(node->op());
  } else if (node->opcode() == IrOpcode::kInt64Constant) {
    return OpParameter<int64_t>(node->op());
  }
  DCHECK_EQ(node->opcode(), IrOpcode::kNumberConstant);
  const double value = OpParameter<double>(node->op());
  DCHECK_EQ(base::bit_cast<int64_t>(value), 0);
  return base::bit_cast<int64_t>(value);
}

template <typename Adapter>
bool RiscvOperandGeneratorT<Adapter>::CanBeImmediate(int64_t value,
                                                     InstructionCode opcode) {
  switch (ArchOpcodeField::decode(opcode)) {
    case kRiscvShl32:
    case kRiscvSar32:
    case kRiscvShr32:
      return is_uint5(value);
    case kRiscvShl64:
    case kRiscvSar64:
    case kRiscvShr64:
      return is_uint6(value);
    case kRiscvAdd32:
    case kRiscvAnd32:
    case kRiscvAnd:
    case kRiscvAdd64:
    case kRiscvOr32:
    case kRiscvOr:
    case kRiscvTst64:
    case kRiscvTst32:
    case kRiscvXor:
      return is_int12(value);
    case kRiscvLb:
    case kRiscvLbu:
    case kRiscvSb:
    case kRiscvLh:
    case kRiscvLhu:
    case kRiscvSh:
    case kRiscvLw:
    case kRiscvSw:
    case kRiscvLd:
    case kRiscvSd:
    case kRiscvLoadFloat:
    case kRiscvStoreFloat:
    case kRiscvLoadDouble:
    case kRiscvStoreDouble:
      return is_int32(value);
    default:
      return is_int12(value);
  }
}

template <typename Adapter>
struct ExtendingLoadMatcher {
  ExtendingLoadMatcher(typename Adapter::node_t node,
                       InstructionSelectorT<Adapter>* selector)
      : matches_(false), selector_(selector), immediate_(0) {
    Initialize(node);
  }

  bool Matches() const { return matches_; }

  typename Adapter::node_t base() const {
    DCHECK(Matches());
    return base_;
  }
  int64_t immediate() const {
    DCHECK(Matches());
    return immediate_;
  }
  ArchOpcode opcode() const {
    DCHECK(Matches());
    return opcode_;
  }

 private:
  bool matches_;
  InstructionSelectorT<Adapter>* selector_;
  typename Adapter::node_t base_{};
  int64_t immediate_;
  ArchOpcode opcode_;

  void Initialize(Node* node) {
    Int64BinopMatcher m(node);
    // When loading a 64-bit value and shifting by 32, we should
    // just load and sign-extend the interesting 4 bytes instead.
    // This happens, for example, when we're loading and untagging SMIs.
    DCHECK(m.IsWord64Sar());
    if (m.left().IsLoad() && m.right().Is(32) &&
        selector_->CanCover(m.node(), m.left().node())) {
      DCHECK_EQ(selector_->GetEffectLevel(node),
                selector_->GetEffectLevel(m.left().node()));
      MachineRepresentation rep =
          LoadRepresentationOf(m.left().node()->op()).representation();
      DCHECK_EQ(3, ElementSizeLog2Of(rep));
      if (rep != MachineRepresentation::kTaggedSigned &&
          rep != MachineRepresentation::kTaggedPointer &&
          rep != MachineRepresentation::kTagged &&
          rep != MachineRepresentation::kWord64) {
        return;
      }

      RiscvOperandGeneratorT<Adapter> g(selector_);
      Node* load = m.left().node();
      Node* offset = load->InputAt(1);
      base_ = load->InputAt(0);
      opcode_ = kRiscvLw;
      if (g.CanBeImmediate(offset, opcode_)) {
#if defined(V8_TARGET_LITTLE_ENDIAN)
        immediate_ = g.GetIntegerConstantValue(offset) + 4;
#elif defined(V8_TARGET_BIG_ENDIAN)
        immediate_ = g.GetIntegerConstantValue(offset);
#endif
        matches_ = g.CanBeImmediate(immediate_, kRiscvLw);
      }
    }
  }

  void Initialize(turboshaft::OpIndex node) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ShiftOp& shift = selector_->Get(node).template Cast<ShiftOp>();
    DCHECK(shift.kind == ShiftOp::Kind::kShiftRightArithmetic ||
           shift.kind == ShiftOp::Kind::kShiftRightArithmeticShiftOutZeros);
    // When loading a 64-bit value and shifting by 32, we should
    // just load and sign-extend the interesting 4 bytes instead.
    // This happens, for example, when we're loading and untagging SMIs.
    const Operation& lhs = selector_->Get(shift.left());
    int64_t constant_rhs;

    if (lhs.Is<LoadOp>() &&
        selector_->MatchIntegralWord64Constant(shift.right(), &constant_rhs) &&
        constant_rhs == 32 && selector_->CanCover(node, shift.left())) {
      RiscvOperandGeneratorT<Adapter> g(selector_);
      const LoadOp& load = lhs.Cast<LoadOp>();
      base_ = load.base();
      opcode_ = kRiscvLw;
      if (load.index().has_value()) {
        int64_t index_constant;
        if (selector_->MatchIntegralWord64Constant(load.index().value(),
                                                   &index_constant)) {
          DCHECK_EQ(load.element_size_log2, 0);
          immediate_ = index_constant + 4;
          matches_ = g.CanBeImmediate(immediate_, kRiscvLw);
        }
      } else {
        immediate_ = load.offset + 4;
        matches_ = g.CanBeImmediate(immediate_, kRiscvLw);
      }
    }
  }
};

template <typename Adapter>
bool TryEmitExtendingLoad(InstructionSelectorT<Adapter>* selector,
                          typename Adapter::node_t node,
                          typename Adapter::node_t output_node) {
  ExtendingLoadMatcher<Adapter> m(node, selector);
  RiscvOperandGeneratorT<Adapter> g(selector);
  if (m.Matches()) {
    InstructionOperand inputs[2];
    inputs[0] = g.UseRegister(m.base());
    InstructionCode opcode =
        m.opcode() | AddressingModeField::encode(kMode_MRI);
    DCHECK(is_int32(m.immediate()));
    inputs[1] = g.TempImmediate(static_cast<int32_t>(m.immediate()));
    InstructionOperand outputs[] = {g.DefineAsRegister(output_node)};
    selector->Emit(opcode, arraysize(outputs), outputs, arraysize(inputs),
                   inputs);
    return true;
  }
  return false;
}

template <typename Adapter>
void EmitLoad(InstructionSelectorT<Adapter>* selector,
              typename Adapter::node_t node, InstructionCode opcode,
              typename Adapter::node_t output = typename Adapter::node_t{}) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  Node* base = selector->input_at(node, 0);
  Node* index = selector->input_at(node, 1);

  ExternalReferenceMatcher m(base);
  if (m.HasResolvedValue() && g.IsIntegerConstant(index) &&
      selector->CanAddressRelativeToRootsRegister(m.ResolvedValue())) {
    ptrdiff_t const delta =
        g.GetIntegerConstantValue(index) +
        MacroAssemblerBase::RootRegisterOffsetForExternalReference(
            selector->isolate(), m.ResolvedValue());
    // Check that the delta is a 32-bit integer due to the limitations of
    // immediate operands.
    if (is_int32(delta)) {
      opcode |= AddressingModeField::encode(kMode_Root);
      selector->Emit(opcode,
                     g.DefineAsRegister(output == nullptr ? node : output),
                     g.UseImmediate(static_cast<int32_t>(delta)));
      return;
    }
  }

  if (base != nullptr && base->opcode() == IrOpcode::kLoadRootRegister) {
    selector->Emit(opcode | AddressingModeField::encode(kMode_Root),
                   g.DefineAsRegister(output == nullptr ? node : output),
                   g.UseImmediate(index));
    return;
  }

  if (g.CanBeImmediate(index, opcode)) {
    selector->Emit(opcode | AddressingModeField::encode(kMode_MRI),
                   g.DefineAsRegister(output == nullptr ? node : output),
                   g.UseRegister(base), g.UseImmediate(index));
  } else {
    InstructionOperand addr_reg = g.TempRegister();
    selector->Emit(kRiscvAdd64 | AddressingModeField::encode(kMode_None),
                   addr_reg, g.UseRegister(index), g.UseRegister(base));
    // Emit desired load opcode, using temp addr_reg.
    selector->Emit(opcode | AddressingModeField::encode(kMode_MRI),
                   g.DefineAsRegister(output == nullptr ? node : output),
                   addr_reg, g.TempImmediate(0));
  }
}

template <>
void EmitLoad(InstructionSelectorT<TurboshaftAdapter>* selector,
              typename TurboshaftAdapter::node_t node, InstructionCode opcode,
              typename TurboshaftAdapter::node_t output) {
  RiscvOperandGeneratorT<TurboshaftAdapter> g(selector);
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Operation& op = selector->Get(node);
  const LoadOp& load = op.Cast<LoadOp>();

  // The LoadStoreSimplificationReducer transforms all loads into
  // *(base + index).
  OpIndex base = load.base();
  OpIndex index = load.index().value();
  DCHECK_EQ(load.offset, 0);
  DCHECK_EQ(load.element_size_log2, 0);

  InstructionOperand inputs[3];
  size_t input_count = 0;
  InstructionOperand output_op;

  // If output is valid, use that as the output register. This is used when we
  // merge a conversion into the load.
  output_op = g.DefineAsRegister(output.valid() ? output : node);

  const Operation& base_op = selector->Get(base);
  if (base_op.Is<Opmask::kExternalConstant>() &&
      selector->is_integer_constant(index)) {
    const ConstantOp& constant_base = base_op.Cast<ConstantOp>();
    if (selector->CanAddressRelativeToRootsRegister(
            constant_base.external_reference())) {
      ptrdiff_t const delta =
          selector->integer_constant(index) +
          MacroAssemblerBase::RootRegisterOffsetForExternalReference(
              selector->isolate(), constant_base.external_reference());
      input_count = 1;
      // Check that the delta is a 32-bit integer due to the limitations of
      // immediate operands.
      if (is_int32(delta)) {
        inputs[0] = g.UseImmediate(static_cast<int32_t>(delta));
        opcode |= AddressingModeField::encode(kMode_Root);
        selector->Emit(opcode, 1, &output_op, input_count, inputs);
        return;
      }
    }
  }

  if (base_op.Is<LoadRootRegisterOp>()) {
    DCHECK(selector->is_integer_constant(index));
    input_count = 1;
    inputs[0] = g.UseImmediate64(selector->integer_constant(index));
    opcode |= AddressingModeField::encode(kMode_Root);
    selector->Emit(opcode, 1, &output_op, input_count, inputs);
    return;
  }

  if (g.CanBeImmediate(index, opcode)) {
    selector->Emit(opcode | AddressingModeField::encode(kMode_MRI),
                   g.DefineAsRegister(output.valid() ? output : node),
                   g.UseRegister(base), g.UseImmediate(index));
  } else {
    InstructionOperand addr_reg = g.TempRegister();
    selector->Emit(kRiscvAdd64 | AddressingModeField::encode(kMode_None),
                   addr_reg, g.UseRegister(index), g.UseRegister(base));
    // Emit desired load opcode, using temp addr_reg.
    selector->Emit(opcode | AddressingModeField::encode(kMode_MRI),
                   g.DefineAsRegister(output.valid() ? output : node), addr_reg,
                   g.TempImmediate(0));
  }
}

template <typename Adapter>
void EmitS128Load(InstructionSelectorT<Adapter>* selector, Node* node,
                  InstructionCode opcode, VSew sew, Vlmul lmul) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  Node* base = selector->input_at(node, 0);
  Node* index = selector->input_at(node, 1);
  if (g.CanBeImmediate(index, opcode)) {
    selector->Emit(opcode | AddressingModeField::encode(kMode_MRI),
                   g.DefineAsRegister(node), g.UseRegister(base),
                   g.UseImmediate(index), g.UseImmediate(sew),
                   g.UseImmediate(lmul));
  } else {
    InstructionOperand addr_reg = g.TempRegister();
    selector->Emit(kRiscvAdd64 | AddressingModeField::encode(kMode_None),
                   addr_reg, g.UseRegister(index), g.UseRegister(base));
    // Emit desired load opcode, using temp addr_reg.
    selector->Emit(opcode | AddressingModeField::encode(kMode_MRI),
                   g.DefineAsRegister(node), addr_reg, g.TempImmediate(0),
                   g.UseImmediate(sew), g.UseImmediate(lmul));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStoreLane(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    StoreLaneParameters params = StoreLaneParametersOf(node->op());
    LoadStoreLaneParams f(params.rep, params.laneidx);
    InstructionCode opcode = kRiscvS128StoreLane;
    opcode |=
        LaneSizeField::encode(ElementSizeInBytes(params.rep) * kBitsPerByte);
    if (params.kind == MemoryAccessKind::kProtected) {
      opcode |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
    }
    RiscvOperandGeneratorT<Adapter> g(this);
    Node* base = this->input_at(node, 0);
    Node* index = this->input_at(node, 1);
    InstructionOperand addr_reg = g.TempRegister();
    Emit(kRiscvAdd64, addr_reg, g.UseRegister(base), g.UseRegister(index));
    InstructionOperand inputs[4] = {
        g.UseRegister(node->InputAt(2)),
        g.UseImmediate(f.laneidx),
        addr_reg,
        g.TempImmediate(0),
    };
    opcode |= AddressingModeField::encode(kMode_MRI);
    Emit(opcode, 0, nullptr, 4, inputs);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoadLane(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    LoadLaneParameters params = LoadLaneParametersOf(node->op());
    DCHECK(params.rep == MachineType::Int8() ||
           params.rep == MachineType::Int16() ||
           params.rep == MachineType::Int32() ||
           params.rep == MachineType::Int64());
    LoadStoreLaneParams f(params.rep.representation(), params.laneidx);
    InstructionCode opcode = kRiscvS128LoadLane;
    opcode |= LaneSizeField::encode(params.rep.MemSize() * kBitsPerByte);
    if (params.kind == MemoryAccessKind::kProtected) {
      opcode |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
    }
    RiscvOperandGeneratorT<Adapter> g(this);
    Node* base = this->input_at(node, 0);
    Node* index = this->input_at(node, 1);
    InstructionOperand addr_reg = g.TempRegister();
    Emit(kRiscvAdd64, addr_reg, g.UseRegister(base), g.UseRegister(index));
    opcode |= AddressingModeField::encode(kMode_MRI);
    Emit(opcode, g.DefineSameAsFirst(node), g.UseRegister(node->InputAt(2)),
         g.UseImmediate(params.laneidx), addr_reg, g.TempImmediate(0));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoad(node_t node) {
  auto load = this->load_view(node);
  LoadRepresentation load_rep = load.loaded_rep();

  InstructionCode opcode = kArchNop;
  switch (load_rep.representation()) {
    case MachineRepresentation::kFloat32:
      opcode = kRiscvLoadFloat;
      break;
    case MachineRepresentation::kFloat64:
      opcode = kRiscvLoadDouble;
      break;
    case MachineRepresentation::kBit:  // Fall through.
    case MachineRepresentation::kWord8:
      opcode = load_rep.IsUnsigned() ? kRiscvLbu : kRiscvLb;
      break;
    case MachineRepresentation::kWord16:
      opcode = load_rep.IsUnsigned() ? kRiscvLhu : kRiscvLh;
      break;
    case MachineRepresentation::kWord32:
      opcode = load_rep.IsUnsigned() ? kRiscvLwu : kRiscvLw;
      break;
#ifdef V8_COMPRESS_POINTERS
      case MachineRepresentation::kTaggedSigned:
        opcode = kRiscvLoadDecompressTaggedSigned;
        break;
      case MachineRepresentation::kTaggedPointer:
        opcode = kRiscvLoadDecompressTagged;
        break;
      case MachineRepresentation::kTagged:
        opcode = kRiscvLoadDecompressTagged;
        break;
#else
      case MachineRepresentation::kTaggedSigned:   // Fall through.
      case MachineRepresentation::kTaggedPointer:  // Fall through.
      case MachineRepresentation::kTagged:         // Fall through.
#endif
      case MachineRepresentation::kWord64:
        opcode = kRiscvLd;
        break;
      case MachineRepresentation::kSimd128:
        opcode = kRiscvRvvLd;
        break;
      case MachineRepresentation::kCompressedPointer:
      case MachineRepresentation::kCompressed:
#ifdef V8_COMPRESS_POINTERS
        opcode = kRiscvLw;
        break;
#else
#endif
      case MachineRepresentation::kProtectedPointer:
        CHECK(V8_ENABLE_SANDBOX_BOOL);
        opcode = kRiscvLoadDecompressProtected;
        break;
      case MachineRepresentation::kSandboxedPointer:
        opcode = kRiscvLoadDecodeSandboxedPointer;
        break;
      case MachineRepresentation::kSimd256:  // Fall through.
      case MachineRepresentation::kMapWord:  // Fall through.
      case MachineRepresentation::kIndirectPointer:  // Fall through.
      case MachineRepresentation::kFloat16:          // Fall through.
      case MachineRepresentation::kNone:
        UNREACHABLE();
    }
    bool traps_on_null;
    if (load.is_protected(&traps_on_null)) {
      if (traps_on_null) {
        opcode |=
            AccessModeField::encode(kMemoryAccessProtectedNullDereference);
      } else {
        opcode |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
      }
    }
    EmitLoad(this, node, opcode);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStorePair(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitProtectedLoad(node_t node) {
  VisitLoad(node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStore(typename Adapter::node_t node) {
  RiscvOperandGeneratorT<Adapter> g(this);
  typename Adapter::StoreView store_view = this->store_view(node);
  DCHECK_EQ(store_view.displacement(), 0);
  node_t base = store_view.base();
  node_t index = this->value(store_view.index());
  node_t value = store_view.value();

  WriteBarrierKind write_barrier_kind =
      store_view.stored_rep().write_barrier_kind();
  MachineRepresentation rep = store_view.stored_rep().representation();

  // TODO(riscv): I guess this could be done in a better way.
  if (write_barrier_kind != kNoWriteBarrier &&
      V8_LIKELY(!v8_flags.disable_write_barriers)) {
    DCHECK(CanBeTaggedOrCompressedOrIndirectPointer(rep));
    InstructionOperand inputs[4];
    size_t input_count = 0;
    inputs[input_count++] = g.UseUniqueRegister(base);
    inputs[input_count++] = g.UseUniqueRegister(index);
    inputs[input_count++] = g.UseUniqueRegister(value);
    RecordWriteMode record_write_mode =
        WriteBarrierKindToRecordWriteMode(write_barrier_kind);
    InstructionOperand temps[] = {g.TempRegister(), g.TempRegister()};
    size_t const temp_count = arraysize(temps);
    InstructionCode code;
    if (rep == MachineRepresentation::kIndirectPointer) {
      DCHECK_EQ(write_barrier_kind, kIndirectPointerWriteBarrier);
      // In this case we need to add the IndirectPointerTag as additional input.
      code = kArchStoreIndirectWithWriteBarrier;
      IndirectPointerTag tag = store_view.indirect_pointer_tag();
      inputs[input_count++] = g.UseImmediate64(static_cast<int64_t>(tag));
    } else {
      code = kArchStoreWithWriteBarrier;
    }
    code |= RecordWriteModeField::encode(record_write_mode);
    if (store_view.is_store_trap_on_null()) {
      code |= AccessModeField::encode(kMemoryAccessProtectedNullDereference);
    }
    Emit(code, 0, nullptr, input_count, inputs, temp_count, temps);
  } else {
    InstructionCode code;
    switch (rep) {
      case MachineRepresentation::kFloat32:
        code = kRiscvStoreFloat;
        break;
      case MachineRepresentation::kFloat64:
        code = kRiscvStoreDouble;
        break;
      case MachineRepresentation::kBit:  // Fall through.
      case MachineRepresentation::kWord8:
        code = kRiscvSb;
        break;
      case MachineRepresentation::kWord16:
        code = kRiscvSh;
        break;
      case MachineRepresentation::kWord32:
        code = kRiscvSw;
        break;
      case MachineRepresentation::kTaggedSigned:   // Fall through.
      case MachineRepresentation::kTaggedPointer:  // Fall through.
      case MachineRepresentation::kTagged:
#ifdef V8_COMPRESS_POINTERS
        code = kRiscvStoreCompressTagged;
        break;
#endif
      case MachineRepresentation::kWord64:
        code = kRiscvSd;
        break;
      case MachineRepresentation::kSimd128:
        code = kRiscvRvvSt;
        break;
      case MachineRepresentation::kCompressedPointer:  // Fall through.
      case MachineRepresentation::kCompressed:
#ifdef V8_COMPRESS_POINTERS
        code = kRiscvStoreCompressTagged;
        break;
#else
        UNREACHABLE();
#endif
      case MachineRepresentation::kSandboxedPointer:
        code = kRiscvStoreEncodeSandboxedPointer;
        break;
      case MachineRepresentation::kIndirectPointer:
        code = kRiscvStoreIndirectPointer;
        break;
      case MachineRepresentation::kSimd256:  // Fall through.
      case MachineRepresentation::kMapWord:  // Fall through.
      case MachineRepresentation::kNone:
      case MachineRepresentation::kProtectedPointer:
      case MachineRepresentation::kFloat16:
        UNREACHABLE();
    }

    if (this->is_load_root_register(base)) {
      Emit(code | AddressingModeField::encode(kMode_Root), g.NoOutput(),
           g.UseRegisterOrImmediateZero(value), g.UseImmediate(index));
      return;
    }

    if (store_view.is_store_trap_on_null()) {
      code |= AccessModeField::encode(kMemoryAccessProtectedNullDereference);
    } else if (store_view.access_kind() == MemoryAccessKind::kProtected) {
      code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
    }

    if (g.CanBeImmediate(index, code)) {
      Emit(code | AddressingModeField::encode(kMode_MRI), g.NoOutput(),
           g.UseRegisterOrImmediateZero(value), g.UseRegister(base),
           g.UseImmediate(index));
    } else {
      InstructionOperand addr_reg = g.TempRegister();
      Emit(kRiscvAdd64 | AddressingModeField::encode(kMode_None), addr_reg,
           g.UseRegister(index), g.UseRegister(base));
      // Emit desired store opcode, using temp addr_reg.
      Emit(code | AddressingModeField::encode(kMode_MRI), g.NoOutput(),
           g.UseRegisterOrImmediateZero(value), addr_reg, g.TempImmediate(0));
    }
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitProtectedStore(node_t node) {
  VisitStore(node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32And(node_t node) {
  VisitBinop<Adapter, Int32BinopMatcher>(this, node, kRiscvAnd32, true,
                                         kRiscvAnd32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64And(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    VisitBinop<Adapter, Int64BinopMatcher>(this, node, kRiscvAnd, true,
                                           kRiscvAnd);
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    Int64BinopMatcher m(node);
    if (m.left().IsWord64Shr() && CanCover(node, m.left().node()) &&
        m.right().HasResolvedValue()) {
      uint64_t mask = m.right().ResolvedValue();
      uint32_t mask_width = base::bits::CountPopulation(mask);
      uint32_t mask_msb = base::bits::CountLeadingZeros64(mask);
      if ((mask_width != 0) && (mask_msb + mask_width == 64)) {
        // The mask must be contiguous, and occupy the least-significant bits.
        DCHECK_EQ(0u, base::bits::CountTrailingZeros64(mask));

        // Select Dext for And(Shr(x, imm), mask) where the mask is in the least
        // significant bits.
        Int64BinopMatcher mleft(m.left().node());
        if (mleft.right().HasResolvedValue()) {
          // Any shift value can match; int64 shifts use `value % 64`.
          uint32_t lsb =
              static_cast<uint32_t>(mleft.right().ResolvedValue() & 0x3F);

          // Dext cannot extract bits past the register size, however since
          // shifting the original value would have introduced some zeros we can
          // still use Dext with a smaller mask and the remaining bits will be
          // zeros.
          if (lsb + mask_width > 64) mask_width = 64 - lsb;

          if (lsb == 0 && mask_width == 64) {
            Emit(kArchNop, g.DefineSameAsFirst(node),
                 g.Use(mleft.left().node()));
            return;
          }
        }
        // Other cases fall through to the normal And operation.
      }
    }
    VisitBinop<Adapter, Int64BinopMatcher>(this, node, kRiscvAnd, true,
                                           kRiscvAnd);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Or(node_t node) {
    VisitBinop<Adapter, Int32BinopMatcher>(this, node, kRiscvOr32, true,
                                           kRiscvOr32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Or(node_t node) {
    VisitBinop<Adapter, Int64BinopMatcher>(this, node, kRiscvOr, true,
                                           kRiscvOr);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Xor(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    VisitBinop<Adapter, Int32BinopMatcher>(this, node, kRiscvXor32, true,
                                           kRiscvXor32);
  } else {
    VisitBinop<Adapter, Int32BinopMatcher>(this, node, kRiscvXor32, true,
                                           kRiscvXor32);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Xor(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    VisitBinop<Adapter, Int64BinopMatcher>(this, node, kRiscvXor, true,
                                           kRiscvXor);
  } else {
    VisitBinop<Adapter, Int64BinopMatcher>(this, node, kRiscvXor, true,
                                           kRiscvXor);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Shl(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ShiftOp& shift_op = this->Get(node).template Cast<ShiftOp>();
    const Operation& lhs = this->Get(shift_op.left());
    const Operation& rhs = this->Get(shift_op.right());
    if ((lhs.Is<Opmask::kChangeInt32ToInt64>() ||
         lhs.Is<Opmask::kChangeUint32ToUint64>()) &&
        rhs.Is<Opmask::kWord32Constant>()) {
      int64_t shift_by = rhs.Cast<ConstantOp>().signed_integral();
      if (base::IsInRange(shift_by, 32, 63) &&
          CanCover(node, shift_op.left())) {
        RiscvOperandGeneratorT<Adapter> g(this);
        // There's no need to sign/zero-extend to 64-bit if we shift out the
        // upper 32 bits anyway.
        Emit(kRiscvShl64, g.DefineSameAsFirst(node),
             g.UseRegister(lhs.Cast<ChangeOp>().input()),
             g.UseImmediate64(shift_by));
        return;
      }
    }
    VisitRRO(this, kRiscvShl64, node);
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    Int64BinopMatcher m(node);
    if ((m.left().IsChangeInt32ToInt64() ||
         m.left().IsChangeUint32ToUint64()) &&
        m.right().IsInRange(32, 63) && CanCover(node, m.left().node())) {
      // There's no need to sign/zero-extend to 64-bit if we shift out the upper
      // 32 bits anyway.
      Emit(kRiscvShl64, g.DefineSameAsFirst(node),
           g.UseRegister(m.left().node()->InputAt(0)),
           g.UseImmediate(m.right().node()));
      return;
    }
    if (m.left().IsWord64And() && CanCover(node, m.left().node()) &&
        m.right().IsInRange(1, 63)) {
      // Match Word64Shl(Word64And(x, mask), imm) to Dshl where the mask is
      // contiguous, and the shift immediate non-zero.
      Int64BinopMatcher mleft(m.left().node());
      if (mleft.right().HasResolvedValue()) {
        uint64_t mask = mleft.right().ResolvedValue();
        uint32_t mask_width = base::bits::CountPopulation(mask);
        uint32_t mask_msb = base::bits::CountLeadingZeros64(mask);
        if ((mask_width != 0) && (mask_msb + mask_width == 64)) {
          uint64_t shift = m.right().ResolvedValue();
          DCHECK_EQ(0u, base::bits::CountTrailingZeros64(mask));
          DCHECK_NE(0u, shift);

          if ((shift + mask_width) >= 64) {
            // If the mask is contiguous and reaches or extends beyond the top
            // bit, only the shift is needed.
            Emit(kRiscvShl64, g.DefineAsRegister(node),
                 g.UseRegister(mleft.left().node()),
                 g.UseImmediate(m.right().node()));
            return;
          }
        }
      }
    }
    VisitRRO(this, kRiscvShl64, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Shr(node_t node) {
    VisitRRO(this, kRiscvShr64, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Sar(node_t node) {
    if (TryEmitExtendingLoad(this, node, node)) return;
    Int64BinopMatcher m(node);
    if (m.left().IsChangeInt32ToInt64() && m.right().HasResolvedValue() &&
        is_uint5(m.right().ResolvedValue()) &&
        CanCover(node, m.left().node())) {
      if ((m.left().InputAt(0)->opcode() != IrOpcode::kLoad &&
           m.left().InputAt(0)->opcode() != IrOpcode::kLoadImmutable) ||
          !CanCover(m.left().node(), m.left().InputAt(0))) {
        RiscvOperandGeneratorT<Adapter> g(this);
        Emit(kRiscvSar32, g.DefineAsRegister(node),
             g.UseRegister(m.left().node()->InputAt(0)),
             g.UseImmediate(m.right().node()));
        return;
      }
    }
    VisitRRO(this, kRiscvSar64, node);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord64Sar(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  if (TryEmitExtendingLoad(this, node, node)) return;
  // Select Sbfx(x, imm, 32-imm) for Word64Sar(ChangeInt32ToInt64(x), imm)
  // where possible
  const ShiftOp& shiftop = Get(node).Cast<ShiftOp>();
  const Operation& lhs = Get(shiftop.left());

  int64_t constant_rhs;
  if (lhs.Is<Opmask::kChangeInt32ToInt64>() &&
      MatchIntegralWord64Constant(shiftop.right(), &constant_rhs) &&
      is_uint5(constant_rhs) && CanCover(node, shiftop.left())) {
    // Don't select Sbfx here if Asr(Ldrsw(x), imm) can be selected for
    // Word64Sar(ChangeInt32ToInt64(Load(x)), imm)
    OpIndex input = lhs.Cast<ChangeOp>().input();
    if (!Get(input).Is<LoadOp>() || !CanCover(shiftop.left(), input)) {
      RiscvOperandGeneratorT<TurboshaftAdapter> g(this);
      int right = static_cast<int>(constant_rhs);
      Emit(kRiscvSar32, g.DefineAsRegister(node), g.UseRegister(input),
           g.UseImmediate(right));
      return;
    }
  }
  VisitRRO(this, kRiscvSar64, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Rol(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    UNREACHABLE();
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Rol(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Ror(node_t node) {
    VisitRRO(this, kRiscvRor32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32ReverseBits(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64ReverseBits(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64ReverseBytes(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    if (CpuFeatures::IsSupported(ZBB)) {
      Emit(kRiscvRev8, g.DefineAsRegister(node),
           g.UseRegister(this->input_at(node, 0)));
    } else {
      Emit(kRiscvByteSwap64, g.DefineAsRegister(node),
           g.UseRegister(this->input_at(node, 0)));
    }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32ReverseBytes(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    if (CpuFeatures::IsSupported(ZBB)) {
      InstructionOperand temp = g.TempRegister();
      Emit(kRiscvRev8, temp, g.UseRegister(this->input_at(node, 0)));
      Emit(kRiscvShr64, g.DefineAsRegister(node), temp, g.TempImmediate(32));
    } else {
      Emit(kRiscvByteSwap32, g.DefineAsRegister(node),
           g.UseRegister(this->input_at(node, 0)));
    }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSimd128ReverseBytes(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Ctz(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kRiscvCtz64, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Popcnt(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kRiscvPopcnt32, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Popcnt(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kRiscvPopcnt64, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Ror(node_t node) {
    VisitRRO(this, kRiscvRor64, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Clz(node_t node) {
    VisitRR(this, kRiscvClz64, node);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitInt32Add(node_t node) {
  VisitBinop<TurboshaftAdapter, Int32BinopMatcher>(this, node, kRiscvAdd32,
                                                   true, kRiscvAdd32);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitInt32Add(Node* node) {
  VisitBinop<TurbofanAdapter, Int32BinopMatcher>(this, node, kRiscvAdd32, true,
                                         kRiscvAdd32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64Add(node_t node) {
    VisitBinop<Adapter, Int64BinopMatcher>(this, node, kRiscvAdd64, true,
                                           kRiscvAdd64);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Sub(node_t node) {
    VisitBinop<Adapter, Int32BinopMatcher>(this, node, kRiscvSub32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64Sub(node_t node) {
  VisitBinop<Adapter, Int64BinopMatcher>(this, node, kRiscvSub64);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitInt32Mul(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  node_t left = this->input_at(node, 0);
  node_t right = this->input_at(node, 1);
  if (CanCover(node, left) && CanCover(node, right)) {
    const Operation& left_op = this->Get(left);
    const Operation& right_op = this->Get(right);
    if (left_op.Is<Opmask::kWord64ShiftRightLogical>() &&
        right_op.Is<Opmask::kWord64ShiftRightLogical>()) {
      RiscvOperandGeneratorT<TurboshaftAdapter> g(this);
      if (this->integer_constant(this->input_at(left, 1)) == 32 &&
          this->integer_constant(this->input_at(right, 1)) == 32) {
        // Combine untagging shifts with Dmul high.
        Emit(kRiscvMulHigh64, g.DefineSameAsFirst(node),
             g.UseRegister(this->input_at(left, 0)),
             g.UseRegister(this->input_at(right, 0)));
        return;
      }
    }
  }
  VisitRRR(this, kRiscvMul32, node);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitInt32Mul(Node* node) {
  RiscvOperandGeneratorT<TurbofanAdapter> g(this);
  Int32BinopMatcher m(node);
  if (m.right().HasResolvedValue() && m.right().ResolvedValue() > 0) {
    uint32_t value = static_cast<uint32_t>(m.right().ResolvedValue());
    if (base::bits::IsPowerOfTwo(value)) {
      Emit(kRiscvShl32 | AddressingModeField::encode(kMode_None),
           g.DefineAsRegister(node), g.UseRegister(m.left().node()),
           g.TempImmediate(base::bits::WhichPowerOfTwo(value)));
      return;
    }
    if (base::bits::IsPowerOfTwo(value + 1)) {
      InstructionOperand temp = g.TempRegister();
      Emit(kRiscvShl32 | AddressingModeField::encode(kMode_None), temp,
           g.UseRegister(m.left().node()),
           g.TempImmediate(base::bits::WhichPowerOfTwo(value + 1)));
      Emit(kRiscvSub32 | AddressingModeField::encode(kMode_None),
           g.DefineAsRegister(node), temp, g.UseRegister(m.left().node()));
      return;
    }
  }
  Node* left = this->input_at(node, 0);
  Node* right = this->input_at(node, 1);
  if (CanCover(node, left) && CanCover(node, right)) {
    if (left->opcode() == IrOpcode::kWord64Sar &&
        right->opcode() == IrOpcode::kWord64Sar) {
      Int64BinopMatcher leftInput(left), rightInput(right);
      if (leftInput.right().Is(32) && rightInput.right().Is(32)) {
        // Combine untagging shifts with Dmul high.
        Emit(kRiscvMulHigh64, g.DefineSameAsFirst(node),
             g.UseRegister(leftInput.left().node()),
             g.UseRegister(rightInput.left().node()));
        return;
      }
    }
  }
  VisitRRR(this, kRiscvMul32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32MulHigh(node_t node) {
    VisitRRR(this, kRiscvMulHigh32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64MulHigh(node_t node) {
    return VisitRRR(this, kRiscvMulHigh64, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32MulHigh(node_t node) {
    VisitRRR(this, kRiscvMulHighU32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint64MulHigh(node_t node) {
    VisitRRR(this, kRiscvMulHighU64, node);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitInt64Mul(node_t node) {
  VisitRRR(this, kRiscvMul64, node);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitInt64Mul(Node* node) {
  RiscvOperandGeneratorT<TurbofanAdapter> g(this);
  Int64BinopMatcher m(node);
  // TODO(dusmil): Add optimization for shifts larger than 32.
  if (m.right().HasResolvedValue() && m.right().ResolvedValue() > 0) {
    uint64_t value = static_cast<uint64_t>(m.right().ResolvedValue());
    if (base::bits::IsPowerOfTwo(value)) {
      Emit(kRiscvShl64 | AddressingModeField::encode(kMode_None),
           g.DefineAsRegister(node), g.UseRegister(m.left().node()),
           g.TempImmediate(base::bits::WhichPowerOfTwo(value)));
      return;
    }
    if (base::bits::IsPowerOfTwo(value + 1)) {
      InstructionOperand temp = g.TempRegister();
      Emit(kRiscvShl64 | AddressingModeField::encode(kMode_None), temp,
           g.UseRegister(m.left().node()),
           g.TempImmediate(base::bits::WhichPowerOfTwo(value + 1)));
      Emit(kRiscvSub64 | AddressingModeField::encode(kMode_None),
           g.DefineAsRegister(node), temp, g.UseRegister(m.left().node()));
      return;
    }
  }
  Emit(kRiscvMul64, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node()));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Div(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    VisitRRR(this, kRiscvDiv32, node,
             OperandGenerator::RegisterUseKind::kUseUniqueRegister);
  } else {
  RiscvOperandGeneratorT<Adapter> g(this);
  Int32BinopMatcher m(node);
  Node* left = this->input_at(node, 0);
  Node* right = this->input_at(node, 1);
  if (CanCover(node, left) && CanCover(node, right)) {
    if (left->opcode() == IrOpcode::kWord64Sar &&
        right->opcode() == IrOpcode::kWord64Sar) {
      Int64BinopMatcher rightInput(right), leftInput(left);
      if (rightInput.right().Is(32) && leftInput.right().Is(32)) {
        // Combine both shifted operands with Ddiv.
        Emit(kRiscvDiv64, g.DefineSameAsFirst(node),
             g.UseRegister(leftInput.left().node()),
             g.UseRegister(rightInput.left().node()));
        return;
      }
    }
  }
  Emit(kRiscvDiv32, g.DefineSameAsFirst(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node(),
                     OperandGenerator::RegisterUseKind::kUseUniqueRegister));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32Div(node_t node) {
  VisitRRR(this, kRiscvDivU32, node,
           OperandGenerator::RegisterUseKind::kUseUniqueRegister);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Mod(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    VisitRRR(this, kRiscvMod32, node);
  } else {
  RiscvOperandGeneratorT<Adapter> g(this);
  Int32BinopMatcher m(node);
  Node* left = this->input_at(node, 0);
  Node* right = this->input_at(node, 1);
  if (CanCover(node, left) && CanCover(node, right)) {
    if (left->opcode() == IrOpcode::kWord64Sar &&
        right->opcode() == IrOpcode::kWord64Sar) {
      Int64BinopMatcher rightInput(right), leftInput(left);
      if (rightInput.right().Is(32) && leftInput.right().Is(32)) {
        // Combine both shifted operands with Dmod.
        Emit(kRiscvMod64, g.DefineSameAsFirst(node),
             g.UseRegister(leftInput.left().node()),
             g.UseRegister(rightInput.left().node()));
        return;
      }
    }
  }
  Emit(kRiscvMod32, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node()));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32Mod(node_t node) {
  VisitRRR(this, kRiscvModU32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64Div(node_t node) {
  VisitRRR(this, kRiscvDiv64, node,
           OperandGenerator::RegisterUseKind::kUseUniqueRegister);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint64Div(node_t node) {
  VisitRRR(this, kRiscvDivU64, node,
           OperandGenerator::RegisterUseKind::kUseUniqueRegister);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64Mod(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    VisitRRR(this, kRiscvMod64, node);
  } else {
  RiscvOperandGeneratorT<Adapter> g(this);
  Int64BinopMatcher m(node);
  Emit(kRiscvMod64, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node()));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint64Mod(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    VisitRRR(this, kRiscvModU64, node);
  } else {
  RiscvOperandGeneratorT<Adapter> g(this);
  Int64BinopMatcher m(node);
  Emit(kRiscvModU64, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node()));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeFloat32ToFloat64(node_t node) {
    VisitRR(this, kRiscvCvtDS, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitRoundInt32ToFloat32(node_t node) {
    VisitRR(this, kRiscvCvtSW, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitRoundUint32ToFloat32(node_t node) {
    VisitRR(this, kRiscvCvtSUw, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeInt32ToFloat64(node_t node) {
  VisitRR(this, kRiscvCvtDW, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeInt64ToFloat64(node_t node) {
    VisitRR(this, kRiscvCvtDL, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeUint32ToFloat64(node_t node) {
    VisitRR(this, kRiscvCvtDUw, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat32ToInt32(node_t node) {
  RiscvOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Operation& op = this->Get(node);
    InstructionCode opcode = kRiscvTruncWS;
    opcode |= MiscField::encode(
        op.Is<Opmask::kTruncateFloat32ToInt32OverflowToMin>());
    Emit(opcode, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
  } else {
    InstructionCode opcode = kRiscvTruncWS;
    TruncateKind kind = OpParameter<TruncateKind>(node->op());
    if (kind == TruncateKind::kSetOverflowToMin) {
      opcode |= MiscField::encode(true);
    }
    Emit(opcode, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat32ToUint32(node_t node) {
  RiscvOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Operation& op = this->Get(node);
    InstructionCode opcode = kRiscvTruncUwS;
    if (op.Is<Opmask::kTruncateFloat32ToUint32OverflowToMin>()) {
      opcode |= MiscField::encode(true);
    }

    Emit(opcode, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
  } else {
    InstructionCode opcode = kRiscvTruncUwS;
    TruncateKind kind = OpParameter<TruncateKind>(node->op());
    if (kind == TruncateKind::kSetOverflowToMin) {
      opcode |= MiscField::encode(true);
    }
    Emit(opcode, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
  }
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitChangeFloat64ToInt32(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  RiscvOperandGeneratorT<TurboshaftAdapter> g(this);
  auto value = this->input_at(node, 0);
  if (CanCover(node, value)) {
    const Operation& op = this->Get(value);
    if (const FloatUnaryOp* load = op.TryCast<FloatUnaryOp>()) {
      DCHECK(load->rep == FloatRepresentation::Float64());
      switch (load->kind) {
        case FloatUnaryOp::Kind::kRoundDown:
          Emit(kRiscvFloorWD, g.DefineAsRegister(node),
               g.UseRegister(this->input_at(value, 0)));
          return;
        case FloatUnaryOp::Kind::kRoundUp:
          Emit(kRiscvCeilWD, g.DefineAsRegister(node),
               g.UseRegister(this->input_at(value, 0)));
          return;
        case FloatUnaryOp::Kind::kRoundToZero:
          Emit(kRiscvTruncWD, g.DefineAsRegister(node),
               g.UseRegister(this->input_at(value, 0)));
          return;
        case FloatUnaryOp::Kind::kRoundTiesEven:
          Emit(kRiscvRoundWD, g.DefineAsRegister(node),
               g.UseRegister(this->input_at(value, 0)));
          return;
        default:
          break;
      }
    }
    if (op.Is<ChangeOp>()) {
      const ChangeOp& change = op.Cast<ChangeOp>();
      using Rep = turboshaft::RegisterRepresentation;
      if (change.from == Rep::Float32() && change.to == Rep::Float64()) {
        auto next = this->input_at(value, 0);
        if (CanCover(value, next)) {
          const Operation& next_op = this->Get(next);
          if (const FloatUnaryOp* round = next_op.TryCast<FloatUnaryOp>()) {
            DCHECK(round->rep == FloatRepresentation::Float32());
            switch (round->kind) {
              case FloatUnaryOp::Kind::kRoundDown:
                Emit(kRiscvFloorWS, g.DefineAsRegister(node),
                     g.UseRegister(this->input_at(next, 0)));
                return;
              case FloatUnaryOp::Kind::kRoundUp:
                Emit(kRiscvCeilWS, g.DefineAsRegister(node),
                     g.UseRegister(this->input_at(next, 0)));
                return;
              case FloatUnaryOp::Kind::kRoundToZero:
                Emit(kRiscvTruncWS, g.DefineAsRegister(node),
                     g.UseRegister(this->input_at(next, 0)));
                return;
              case FloatUnaryOp::Kind::kRoundTiesEven:
                Emit(kRiscvRoundWS, g.DefineAsRegister(node),
                     g.UseRegister(this->input_at(next, 0)));
                return;
              default:
                break;
            }
          }
        }
        // Match float32 -> float64 -> int32 representation change path.
        Emit(kRiscvTruncWS, g.DefineAsRegister(node),
             g.UseRegister(this->input_at(value, 0)));
        return;
      }
    }
  }
  VisitRR(this, kRiscvTruncWD, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeFloat64ToInt32(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    Node* value = this->input_at(node, 0);
    // Match ChangeFloat64ToInt32(Float64Round##OP) to corresponding instruction
    // which does rounding and conversion to integer format.
    if (CanCover(node, value)) {
      switch (value->opcode()) {
        case IrOpcode::kFloat64RoundDown:
          Emit(kRiscvFloorWD, g.DefineAsRegister(node),
               g.UseRegister(value->InputAt(0)));
          return;
        case IrOpcode::kFloat64RoundUp:
          Emit(kRiscvCeilWD, g.DefineAsRegister(node),
               g.UseRegister(value->InputAt(0)));
          return;
        case IrOpcode::kFloat64RoundTiesEven:
          Emit(kRiscvRoundWD, g.DefineAsRegister(node),
               g.UseRegister(value->InputAt(0)));
          return;
        case IrOpcode::kFloat64RoundTruncate:
          Emit(kRiscvTruncWD, g.DefineAsRegister(node),
               g.UseRegister(value->InputAt(0)));
          return;
        default:
          break;
      }
      if (value->opcode() == IrOpcode::kChangeFloat32ToFloat64) {
        Node* next = value->InputAt(0);
        if (CanCover(value, next)) {
          // Match
          // ChangeFloat64ToInt32(ChangeFloat32ToFloat64(Float64Round##OP))
          switch (next->opcode()) {
            case IrOpcode::kFloat32RoundDown:
              Emit(kRiscvFloorWS, g.DefineAsRegister(node),
                   g.UseRegister(next->InputAt(0)));
              return;
            case IrOpcode::kFloat32RoundUp:
              Emit(kRiscvCeilWS, g.DefineAsRegister(node),
                   g.UseRegister(next->InputAt(0)));
              return;
            case IrOpcode::kFloat32RoundTiesEven:
              Emit(kRiscvRoundWS, g.DefineAsRegister(node),
                   g.UseRegister(next->InputAt(0)));
              return;
            case IrOpcode::kFloat32RoundTruncate:
              Emit(kRiscvTruncWS, g.DefineAsRegister(node),
                   g.UseRegister(next->InputAt(0)));
              return;
            default:
              Emit(kRiscvTruncWS, g.DefineAsRegister(node),
                   g.UseRegister(value->InputAt(0)));
              return;
          }
        } else {
          // Match float32 -> float64 -> int32 representation change path.
          Emit(kRiscvTruncWS, g.DefineAsRegister(node),
               g.UseRegister(value->InputAt(0)));
          return;
        }
      }
    }
    VisitRR(this, kRiscvTruncWD, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat64ToInt32(
    node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
    InstructionOperand outputs[2];
    size_t output_count = 0;
    outputs[output_count++] = g.DefineAsRegister(node);

    node_t success_output = FindProjection(node, 1);
    if (this->valid(success_output)) {
      outputs[output_count++] = g.DefineAsRegister(success_output);
    }

    this->Emit(kRiscvTruncWD, output_count, outputs, 1, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat64ToUint32(
    node_t node) {
  RiscvOperandGeneratorT<Adapter> g(this);
  InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
  InstructionOperand outputs[2];
  size_t output_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = FindProjection(node, 1);
  if (this->valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
  }

  Emit(kRiscvTruncUwD, output_count, outputs, 1, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeFloat64ToInt64(node_t node) {
    VisitRR(this, kRiscvTruncLD, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeFloat64ToUint32(node_t node) {
    VisitRR(this, kRiscvTruncUwD, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeFloat64ToUint64(node_t node) {
    VisitRR(this, kRiscvTruncUlD, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat64ToUint32(node_t node) {
  VisitRR(this, kRiscvTruncUwD, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat64ToInt64(node_t node) {
  RiscvOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    InstructionCode opcode = kRiscvTruncLD;
    const Operation& op = this->Get(node);
    if (op.Is<Opmask::kTruncateFloat64ToInt64OverflowToMin>()) {
      opcode |= MiscField::encode(true);
    }

    Emit(opcode, g.DefineAsRegister(node), g.UseRegister(op.input(0)));
  } else {
    InstructionCode opcode = kRiscvTruncLD;
    TruncateKind kind = OpParameter<TruncateKind>(node->op());
    if (kind == TruncateKind::kSetOverflowToMin) {
      opcode |= MiscField::encode(true);
    }
    Emit(opcode, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat32ToInt64(
    node_t node) {
  RiscvOperandGeneratorT<Adapter> g(this);
  InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
  InstructionOperand outputs[2];
  size_t output_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = FindProjection(node, 1);
  if (this->valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
  }

  this->Emit(kRiscvTruncLS, output_count, outputs, 1, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat64ToInt64(
    node_t node) {
  RiscvOperandGeneratorT<Adapter> g(this);
  InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
  InstructionOperand outputs[2];
  size_t output_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = FindProjection(node, 1);
  if (this->valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
  }

  Emit(kRiscvTruncLD, output_count, outputs, 1, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat32ToUint64(
    node_t node) {
  RiscvOperandGeneratorT<Adapter> g(this);
  InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
  InstructionOperand outputs[2];
  size_t output_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = FindProjection(node, 1);
  if (this->valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
  }

  Emit(kRiscvTruncUlS, output_count, outputs, 1, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat64ToUint64(
    node_t node) {
  RiscvOperandGeneratorT<Adapter> g(this);

  InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
  InstructionOperand outputs[2];
  size_t output_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = FindProjection(node, 1);
  if (this->valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
  }

  Emit(kRiscvTruncUlD, output_count, outputs, 1, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBitcastWord32ToWord64(node_t node) {
    DCHECK(SmiValuesAre31Bits());
    DCHECK(COMPRESS_POINTERS_BOOL);
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kRiscvZeroExtendWord, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void EmitSignExtendWord(InstructionSelectorT<Adapter>* selector,
                        typename Adapter::node_t node) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  typename Adapter::node_t value = selector->input_at(node, 0);
  selector->Emit(kRiscvSignExtendWord, g.DefineAsRegister(node),
                 g.UseRegister(value));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeInt32ToInt64(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ChangeOp& change_op = this->Get(node).template Cast<ChangeOp>();
    const Operation& input_op = this->Get(change_op.input());
    if (input_op.Is<LoadOp>() && CanCover(node, change_op.input())) {
      // Generate sign-extending load.
      LoadRepresentation load_rep =
          this->load_view(change_op.input()).loaded_rep();
      MachineRepresentation rep = load_rep.representation();
      InstructionCode opcode = kArchNop;
      switch (rep) {
        case MachineRepresentation::kBit:  // Fall through.
        case MachineRepresentation::kWord8:
          opcode = load_rep.IsUnsigned() ? kRiscvLbu : kRiscvLb;
          break;
        case MachineRepresentation::kWord16:
          opcode = load_rep.IsUnsigned() ? kRiscvLhu : kRiscvLh;
          break;
        case MachineRepresentation::kWord32:
        case MachineRepresentation::kWord64:
          // Since BitcastElider may remove nodes of
          // IrOpcode::kTruncateInt64ToInt32 and directly use the inputs, values
          // with kWord64 can also reach this line.
        case MachineRepresentation::kTaggedSigned:
        case MachineRepresentation::kTagged:
          opcode = kRiscvLw;
          break;
        default:
          UNREACHABLE();
      }
      EmitLoad(this, change_op.input(), opcode, node);
      return;
    }
    EmitSignExtendWord(this, node);
  } else {
    Node* value = this->input_at(node, 0);
    if ((value->opcode() == IrOpcode::kLoad ||
         value->opcode() == IrOpcode::kLoadImmutable) &&
        CanCover(node, value)) {
      // Generate sign-extending load.
      LoadRepresentation load_rep = LoadRepresentationOf(value->op());
      InstructionCode opcode = kArchNop;
      switch (load_rep.representation()) {
        case MachineRepresentation::kBit:  // Fall through.
        case MachineRepresentation::kWord8:
          opcode = load_rep.IsUnsigned() ? kRiscvLbu : kRiscvLb;
          break;
        case MachineRepresentation::kWord16:
          opcode = load_rep.IsUnsigned() ? kRiscvLhu : kRiscvLh;
          break;
        case MachineRepresentation::kWord32:
        case MachineRepresentation::kWord64:
          // Since BitcastElider may remove nodes of
          // IrOpcode::kTruncateInt64ToInt32 and directly use the inputs, values
          // with kWord64 can also reach this line.
          // For RV64, the lw loads a 32 bit value from memory and sign-extend
          // it to 64 bits before storing it in rd register
        case MachineRepresentation::kTaggedSigned:
        case MachineRepresentation::kTagged:
          opcode = kRiscvLw;
          break;
        default:
          UNREACHABLE();
      }
      EmitLoad(this, value, opcode, node);
    } else {
      EmitSignExtendWord(this, node);
      return;
    }
  }
}

template <typename Adapter>
bool InstructionSelectorT<Adapter>::ZeroExtendsWord32ToWord64NoPhis(
    node_t node) {
    DCHECK_NE(node->opcode(), IrOpcode::kPhi);
    if (node->opcode() == IrOpcode::kLoad ||
        node->opcode() == IrOpcode::kLoadImmutable) {
      LoadRepresentation load_rep = LoadRepresentationOf(node->op());
      if (load_rep.IsUnsigned()) {
        switch (load_rep.representation()) {
          case MachineRepresentation::kWord8:
          case MachineRepresentation::kWord16:
            return true;
          default:
            return false;
        }
      }
    }

    // All other 32-bit operations sign-extend to the upper 32 bits
    return false;
}

template <>
bool InstructionSelectorT<TurboshaftAdapter>::ZeroExtendsWord32ToWord64NoPhis(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  DCHECK(!this->Get(node).Is<PhiOp>());
  const Operation& op = this->Get(node);
  if (op.opcode == Opcode::kLoad) {
    auto load = this->load_view(node);
    LoadRepresentation load_rep = load.loaded_rep();
    if (load_rep.IsUnsigned()) {
      switch (load_rep.representation()) {
        case MachineRepresentation::kWord8:
        case MachineRepresentation::kWord16:
          return true;
        default:
          return false;
      }
    }
  }
  // All other 32-bit operations sign-extend to the upper 32 bits
  return false;
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeUint32ToUint64(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    node_t value = this->input_at(node, 0);
    if (ZeroExtendsWord32ToWord64(value)) {
      Emit(kArchNop, g.DefineSameAsFirst(node), g.Use(value));
      return;
    }
    Emit(kRiscvZeroExtendWord, g.DefineAsRegister(node), g.UseRegister(value));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateInt64ToInt32(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    Node* value = this->input_at(node, 0);
    if (CanCover(node, value)) {
      switch (value->opcode()) {
        case IrOpcode::kWord64Sar: {
          if (CanCover(value, value->InputAt(0)) &&
              TryEmitExtendingLoad(this, value, node)) {
            return;
          } else {
            Int64BinopMatcher m(value);
            if (m.right().IsInRange(32, 63)) {
              // After smi untagging no need for truncate. Combine sequence.
              Emit(kRiscvSar64, g.DefineSameAsFirst(node),
                   g.UseRegister(m.left().node()),
                   g.UseImmediate(m.right().node()));
              return;
            }
          }
          break;
        }
        default:
          break;
      }
    }
    // Semantics of this machine IR is not clear. For example, x86 zero-extend
    // the truncated value; arm treats it as nop thus the upper 32-bit as
    // undefined; Riscv emits ext instruction which zero-extend the 32-bit
    // value; for riscv, we do sign-extension of the truncated value
    Emit(kRiscvSignExtendWord, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)), g.TempImmediate(0));
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitTruncateInt64ToInt32(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  RiscvOperandGeneratorT<TurboshaftAdapter> g(this);
  auto value = input_at(node, 0);
  if (CanCover(node, value)) {
    if (Get(value).Is<Opmask::kWord64ShiftRightArithmetic>()) {
      if (CanCover(value, input_at(value, 0)) &&
          TryEmitExtendingLoad(this, value, node)) {
        return;
      } else {
        auto constant = constant_view(input_at(value, 1));
        if (constant.is_int64()) {
          if (constant.int64_value() <= 63 && constant.int64_value() >= 32) {
            // After smi untagging no need for truncate. Combine sequence.
            Emit(kRiscvSar64, g.DefineSameAsFirst(node),
                 g.UseRegister(input_at(value, 0)),
                 g.UseImmediate(input_at(value, 1)));
            return;
          }
        }
      }
    }
  }
  // Semantics of this machine IR is not clear. For example, x86 zero-extend
  // the truncated value; arm treats it as nop thus the upper 32-bit as
  // undefined; Riscv emits ext instruction which zero-extend the 32-bit
  // value; for riscv, we do sign-extension of the truncated value
  Emit(kRiscvSignExtendWord, g.DefineAsRegister(node),
       g.UseRegister(input_at(node, 0)), g.TempImmediate(0));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitRoundInt64ToFloat32(node_t node) {
    VisitRR(this, kRiscvCvtSL, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitRoundInt64ToFloat64(node_t node) {
    VisitRR(this, kRiscvCvtDL, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitRoundUint64ToFloat32(node_t node) {
    VisitRR(this, kRiscvCvtSUl, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitRoundUint64ToFloat64(node_t node) {
    VisitRR(this, kRiscvCvtDUl, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBitcastFloat32ToInt32(node_t node) {
    VisitRR(this, kRiscvBitcastFloat32ToInt32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBitcastFloat64ToInt64(node_t node) {
    VisitRR(this, kRiscvBitcastDL, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBitcastInt32ToFloat32(node_t node) {
    VisitRR(this, kRiscvBitcastInt32ToFloat32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBitcastInt64ToFloat64(node_t node) {
    VisitRR(this, kRiscvBitcastLD, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64RoundDown(node_t node) {
  VisitRR(this, kRiscvFloat64RoundDown, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32RoundUp(node_t node) {
  VisitRR(this, kRiscvFloat32RoundUp, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64RoundUp(node_t node) {
    VisitRR(this, kRiscvFloat64RoundUp, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32RoundTruncate(node_t node) {
  VisitRR(this, kRiscvFloat32RoundTruncate, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64RoundTruncate(node_t node) {
  VisitRR(this, kRiscvFloat64RoundTruncate, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64RoundTiesAway(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32RoundTiesEven(node_t node) {
  VisitRR(this, kRiscvFloat32RoundTiesEven, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64RoundTiesEven(node_t node) {
  VisitRR(this, kRiscvFloat64RoundTiesEven, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Neg(node_t node) {
  VisitRR(this, kRiscvNegS, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Neg(node_t node) {
  VisitRR(this, kRiscvNegD, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Ieee754Binop(
    node_t node, InstructionCode opcode) {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(opcode, g.DefineAsFixed(node, fa0),
         g.UseFixed(this->input_at(node, 0), fa0),
         g.UseFixed(this->input_at(node, 1), fa1))
        ->MarkAsCall();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Ieee754Unop(
    node_t node, InstructionCode opcode) {
  RiscvOperandGeneratorT<Adapter> g(this);
  Emit(opcode, g.DefineAsFixed(node, fa0),
       g.UseFixed(this->input_at(node, 0), fa1))
      ->MarkAsCall();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitPrepareArguments(
    ZoneVector<PushParameter>* arguments, const CallDescriptor* call_descriptor,
    node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);

    // Prepare for C function call.
    if (call_descriptor->IsCFunctionCall()) {
      int gp_param_count =
          static_cast<int>(call_descriptor->GPParameterCount());
      int fp_param_count =
          static_cast<int>(call_descriptor->FPParameterCount());
      Emit(kArchPrepareCallCFunction | ParamField::encode(gp_param_count) |
               FPParamField::encode(fp_param_count),
           0, nullptr, 0, nullptr);

      // Poke any stack arguments.
      int slot = kCArgSlotCount;
      for (PushParameter input : (*arguments)) {
        Emit(kRiscvStoreToStackSlot, g.NoOutput(), g.UseRegister(input.node),
             g.TempImmediate(slot << kSystemPointerSizeLog2));
        ++slot;
      }
    } else {
      int push_count = static_cast<int>(call_descriptor->ParameterSlotCount());
      if (push_count > 0) {
        // Calculate needed space
        int stack_size = 0;
        for (PushParameter input : (*arguments)) {
          if (this->valid(input.node)) {
            stack_size += input.location.GetSizeInPointers();
          }
        }
        Emit(kRiscvStackClaim, g.NoOutput(),
             g.TempImmediate(stack_size << kSystemPointerSizeLog2));
      }
      for (size_t n = 0; n < arguments->size(); ++n) {
        PushParameter input = (*arguments)[n];
        if (this->valid(input.node)) {
          Emit(kRiscvStoreToStackSlot, g.NoOutput(), g.UseRegister(input.node),
               g.TempImmediate(static_cast<int>(n << kSystemPointerSizeLog2)));
        }
      }
    }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUnalignedLoad(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    auto load = this->load_view(node);
    LoadRepresentation load_rep = load.loaded_rep();
    RiscvOperandGeneratorT<Adapter> g(this);
    Node* base = this->input_at(node, 0);
    Node* index = this->input_at(node, 1);

    InstructionCode opcode = kArchNop;
    switch (load_rep.representation()) {
      case MachineRepresentation::kFloat32:
        opcode = kRiscvULoadFloat;
        break;
      case MachineRepresentation::kFloat64:
        opcode = kRiscvULoadDouble;
        break;
      case MachineRepresentation::kWord8:
        opcode = load_rep.IsUnsigned() ? kRiscvLbu : kRiscvLb;
        break;
      case MachineRepresentation::kWord16:
        opcode = load_rep.IsUnsigned() ? kRiscvUlhu : kRiscvUlh;
        break;
      case MachineRepresentation::kWord32:
        opcode = kRiscvUlw;
        break;
      case MachineRepresentation::kTaggedSigned:   // Fall through.
      case MachineRepresentation::kTaggedPointer:  // Fall through.
      case MachineRepresentation::kTagged:         // Fall through.
      case MachineRepresentation::kWord64:
        opcode = kRiscvUld;
        break;
      case MachineRepresentation::kSimd128:
        opcode = kRiscvRvvLd;
        break;
      case MachineRepresentation::kSimd256:            // Fall through.
      case MachineRepresentation::kBit:                // Fall through.
      case MachineRepresentation::kCompressedPointer:  // Fall through.
      case MachineRepresentation::kCompressed:         // Fall through.
      case MachineRepresentation::kSandboxedPointer:   // Fall through.
      case MachineRepresentation::kMapWord:            // Fall through.
      case MachineRepresentation::kIndirectPointer:    // Fall through.
      case MachineRepresentation::kProtectedPointer:   // Fall through.
      case MachineRepresentation::kFloat16:            // Fall through.
      case MachineRepresentation::kNone:
        UNREACHABLE();
    }
    bool traps_on_null;
    if (load.is_protected(&traps_on_null)) {
      if (traps_on_null) {
        opcode |=
            AccessModeField::encode(kMemoryAccessProtectedNullDereference);
      } else {
        opcode |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
      }
    }
    if (g.CanBeImmediate(index, opcode)) {
      Emit(opcode | AddressingModeField::encode(kMode_MRI),
           g.DefineAsRegister(node), g.UseRegister(base),
           g.UseImmediate(index));
    } else {
      InstructionOperand addr_reg = g.TempRegister();
      Emit(kRiscvAdd64 | AddressingModeField::encode(kMode_None), addr_reg,
           g.UseRegister(index), g.UseRegister(base));
      // Emit desired load opcode, using temp addr_reg.
      Emit(opcode | AddressingModeField::encode(kMode_MRI),
           g.DefineAsRegister(node), addr_reg, g.TempImmediate(0));
    }
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUnalignedStore(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    Node* base = this->input_at(node, 0);
    Node* index = this->input_at(node, 1);
    Node* value = node->InputAt(2);

    UnalignedStoreRepresentation rep =
        UnalignedStoreRepresentationOf(node->op());
    ArchOpcode opcode;
    switch (rep) {
      case MachineRepresentation::kFloat32:
        opcode = kRiscvUStoreFloat;
        break;
      case MachineRepresentation::kFloat64:
        opcode = kRiscvUStoreDouble;
        break;
      case MachineRepresentation::kWord8:
        opcode = kRiscvSb;
        break;
      case MachineRepresentation::kWord16:
        opcode = kRiscvUsh;
        break;
      case MachineRepresentation::kWord32:
        opcode = kRiscvUsw;
        break;
      case MachineRepresentation::kTaggedSigned:   // Fall through.
      case MachineRepresentation::kTaggedPointer:  // Fall through.
      case MachineRepresentation::kTagged:         // Fall through.
      case MachineRepresentation::kWord64:
        opcode = kRiscvUsd;
        break;
      case MachineRepresentation::kSimd128:
        opcode = kRiscvRvvSt;
        break;
      case MachineRepresentation::kSimd256:            // Fall through.
      case MachineRepresentation::kBit:                // Fall through.
      case MachineRepresentation::kCompressedPointer:  // Fall through.
      case MachineRepresentation::kCompressed:         // Fall through.
      case MachineRepresentation::kSandboxedPointer:   // Fall through.
      case MachineRepresentation::kMapWord:            // Fall through.
      case MachineRepresentation::kIndirectPointer:    // Fall through.
      case MachineRepresentation::kProtectedPointer:   // Fall through.
      case MachineRepresentation::kFloat16:
      case MachineRepresentation::kNone:
        UNREACHABLE();
    }

    if (g.CanBeImmediate(index, opcode)) {
      Emit(opcode | AddressingModeField::encode(kMode_MRI), g.NoOutput(),
           g.UseRegister(base), g.UseImmediate(index),
           g.UseRegisterOrImmediateZero(value));
    } else {
      InstructionOperand addr_reg = g.TempRegister();
      Emit(kRiscvAdd64 | AddressingModeField::encode(kMode_None), addr_reg,
           g.UseRegister(index), g.UseRegister(base));
      // Emit desired store opcode, using temp addr_reg.
      Emit(opcode | AddressingModeField::encode(kMode_MRI), g.NoOutput(),
           addr_reg, g.TempImmediate(0), g.UseRegisterOrImmediateZero(value));
    }
  }
}

namespace {

#ifndef V8_COMPRESS_POINTERS
bool IsNodeUnsigned(Node* n) {
  NodeMatcher m(n);

  if (m.IsLoad() || m.IsUnalignedLoad() || m.IsProtectedLoad()) {
    LoadRepresentation load_rep = LoadRepresentationOf(n->op());
    return load_rep.IsUnsigned();
  } else if (m.IsWord32AtomicLoad() || m.IsWord64AtomicLoad()) {
    AtomicLoadParameters atomic_load_params = AtomicLoadParametersOf(n->op());
    LoadRepresentation load_rep = atomic_load_params.representation();
    return load_rep.IsUnsigned();
  } else {
    return m.IsUint32Div() || m.IsUint32LessThan() ||
           m.IsUint32LessThanOrEqual() || m.IsUint32Mod() ||
           m.IsUint32MulHigh() || m.IsChangeFloat64ToUint32() ||
           m.IsTruncateFloat64ToUint32() || m.IsTruncateFloat32ToUint32();
  }
}
#endif

// Shared routine for multiple word compare operations.
template <typename Adapter>
void VisitFullWord32Compare(InstructionSelectorT<Adapter>* selector,
                            typename Adapter::node_t node,
                            InstructionCode opcode,
                            FlagsContinuationT<Adapter>* cont) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  InstructionOperand leftOp = g.TempRegister();
  InstructionOperand rightOp = g.TempRegister();

  selector->Emit(kRiscvShl64, leftOp,
                 g.UseRegister(selector->input_at(node, 0)),
                 g.TempImmediate(32));
  selector->Emit(kRiscvShl64, rightOp,
                 g.UseRegister(selector->input_at(node, 1)),
                 g.TempImmediate(32));

  VisitCompare(selector, opcode, leftOp, rightOp, cont);
}

#ifndef V8_COMPRESS_POINTERS
template <typename Adapter>
void VisitOptimizedWord32Compare(InstructionSelectorT<Adapter>* selector,
                                 Node* node, InstructionCode opcode,
                                 FlagsContinuationT<Adapter>* cont) {
  if (v8_flags.debug_code) {
    RiscvOperandGeneratorT<Adapter> g(selector);
    InstructionOperand leftOp = g.TempRegister();
    InstructionOperand rightOp = g.TempRegister();
    InstructionOperand optimizedResult = g.TempRegister();
    InstructionOperand fullResult = g.TempRegister();
    FlagsCondition condition = cont->condition();
    InstructionCode testOpcode = opcode |
                                 FlagsConditionField::encode(condition) |
                                 FlagsModeField::encode(kFlags_set);

    selector->Emit(testOpcode, optimizedResult,
                   g.UseRegister(selector->input_at(node, 0)),
                   g.UseRegister(selector->input_at(node, 1)));

    selector->Emit(kRiscvShl64, leftOp,
                   g.UseRegister(selector->input_at(node, 0)),
                   g.TempImmediate(32));
    selector->Emit(kRiscvShl64, rightOp,
                   g.UseRegister(selector->input_at(node, 1)),
                   g.TempImmediate(32));
    selector->Emit(testOpcode, fullResult, leftOp, rightOp);

    selector->Emit(kRiscvAssertEqual, g.NoOutput(), optimizedResult, fullResult,
                   g.TempImmediate(static_cast<int>(
                       AbortReason::kUnsupportedNonPrimitiveCompare)));
  }

  VisitWordCompare(selector, node, opcode, cont, false);
}
#endif
template <typename Adapter>
void VisitWord32Compare(InstructionSelectorT<Adapter>* selector,
                        typename Adapter::node_t node,
                        FlagsContinuationT<Adapter>* cont) {
  if constexpr (Adapter::IsTurboshaft) {
    VisitFullWord32Compare(selector, node, kRiscvCmp, cont);
  } else {
    // RISC-V doesn't support Word32 compare instructions. Instead it relies
    // that the values in registers are correctly sign-extended and uses
    // Word64 comparison instead. This behavior is correct in most cases,
    // but doesn't work when comparing signed with unsigned operands.
    // We could simulate full Word32 compare in all cases but this would
    // create an unnecessary overhead since unsigned integers are rarely
    // used in JavaScript.
    // The solution proposed here tries to match a comparison of signed
    // with unsigned operand, and perform full Word32Compare only
    // in those cases. Unfortunately, the solution is not complete because
    // it might skip cases where Word32 full compare is needed, so
    // basically it is a hack.
    // When call to a host function in simulator, if the function return a
    // int32 value, the simulator do not sign-extended to int64 because in
    // simulator we do not know the function whether return a int32 or int64.
    // so we need do a full word32 compare in this case.
#ifndef V8_COMPRESS_POINTERS
#ifndef USE_SIMULATOR
    if (IsNodeUnsigned(selector->input_at(node, 0)) !=
        IsNodeUnsigned(node->InputAt(1))) {
#else
    if (IsNodeUnsigned(selector->input_at(node, 0)) !=
            IsNodeUnsigned(node->InputAt(1)) ||
        node->InputAt(0)->opcode() == IrOpcode::kCall ||
        node->InputAt(1)->opcode() == IrOpcode::kCall) {
#endif
      VisitFullWord32Compare(selector, node, kRiscvCmp, cont);
    } else {
      VisitOptimizedWord32Compare(selector, node, kRiscvCmp, cont);
    }
#else
    VisitFullWord32Compare(selector, node, kRiscvCmp, cont);
#endif
  }
}

template <typename Adapter>
void VisitWord64Compare(InstructionSelectorT<Adapter>* selector,
                        typename Adapter::node_t node,
                        FlagsContinuationT<Adapter>* cont) {
  VisitWordCompare(selector, node, kRiscvCmp, cont, false);
}

template <typename Adapter>
void VisitAtomicLoad(InstructionSelectorT<Adapter>* selector,
                     typename Adapter::node_t node, AtomicWidth width) {
  using node_t = typename Adapter::node_t;
  RiscvOperandGeneratorT<Adapter> g(selector);
  auto load = selector->load_view(node);
  node_t base = load.base();
  node_t index = load.index();

  // The memory order is ignored as both acquire and sequentially consistent
  // loads can emit LDAR.
  // https://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
  LoadRepresentation load_rep = load.loaded_rep();
  InstructionCode code;
  switch (load_rep.representation()) {
    case MachineRepresentation::kWord8:
      DCHECK_IMPLIES(load_rep.IsSigned(), width == AtomicWidth::kWord32);
      code = load_rep.IsSigned() ? kAtomicLoadInt8 : kAtomicLoadUint8;
      break;
    case MachineRepresentation::kWord16:
      DCHECK_IMPLIES(load_rep.IsSigned(), width == AtomicWidth::kWord32);
      code = load_rep.IsSigned() ? kAtomicLoadInt16 : kAtomicLoadUint16;
      break;
    case MachineRepresentation::kWord32:
      code = kAtomicLoadWord32;
      break;
    case MachineRepresentation::kWord64:
      code = kRiscvWord64AtomicLoadUint64;
      break;
#ifdef V8_COMPRESS_POINTERS
    case MachineRepresentation::kTaggedSigned:
      code = kRiscvAtomicLoadDecompressTaggedSigned;
      break;
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTagged:
      code = kRiscvAtomicLoadDecompressTagged;
      break;
#else
    case MachineRepresentation::kTaggedSigned:   // Fall through.
    case MachineRepresentation::kTaggedPointer:  // Fall through.
    case MachineRepresentation::kTagged:
      if (kTaggedSize == 8) {
        code = kRiscvWord64AtomicLoadUint64;
      } else {
        code = kAtomicLoadWord32;
      }
      break;
#endif
    case MachineRepresentation::kCompressedPointer:  // Fall through.
    case MachineRepresentation::kCompressed:
      DCHECK(COMPRESS_POINTERS_BOOL);
      code = kAtomicLoadWord32;
      break;
    default:
      UNREACHABLE();
  }

  bool traps_on_null;
  if (load.is_protected(&traps_on_null)) {
    // Atomic loads and null dereference are mutually exclusive. This might
    // change with multi-threaded wasm-gc in which case the access mode should
    // probably be kMemoryAccessProtectedNullDereference.
    DCHECK(!traps_on_null);
    code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }

  if (g.CanBeImmediate(index, code)) {
    selector->Emit(code | AddressingModeField::encode(kMode_MRI) |
                       AtomicWidthField::encode(width),
                   g.DefineAsRegister(node), g.UseRegister(base),
                   g.UseImmediate(index));
  } else {
    InstructionOperand addr_reg = g.TempRegister();
    selector->Emit(kRiscvAdd64 | AddressingModeField::encode(kMode_None),
                   addr_reg, g.UseRegister(index), g.UseRegister(base));
    // Emit desired load opcode, using temp addr_reg.
    selector->Emit(code | AddressingModeField::encode(kMode_MRI) |
                       AtomicWidthField::encode(width),
                   g.DefineAsRegister(node), addr_reg, g.TempImmediate(0));
  }
}

void VisitAtomicLoad(InstructionSelectorT<TurbofanAdapter>* selector,
                     Node* node, AtomicWidth width) {
  RiscvOperandGeneratorT<TurbofanAdapter> g(selector);
  Node* base = selector->input_at(node, 0);
  Node* index = selector->input_at(node, 1);

  // The memory order is ignored.
  AtomicLoadParameters atomic_load_params = AtomicLoadParametersOf(node->op());
  LoadRepresentation load_rep = atomic_load_params.representation();
  InstructionCode code;
  switch (load_rep.representation()) {
    case MachineRepresentation::kWord8:
      DCHECK_IMPLIES(load_rep.IsSigned(), width == AtomicWidth::kWord32);
      code = load_rep.IsSigned() ? kAtomicLoadInt8 : kAtomicLoadUint8;
      break;
    case MachineRepresentation::kWord16:
      DCHECK_IMPLIES(load_rep.IsSigned(), width == AtomicWidth::kWord32);
      code = load_rep.IsSigned() ? kAtomicLoadInt16 : kAtomicLoadUint16;
      break;
    case MachineRepresentation::kWord32:
      code = kAtomicLoadWord32;
      break;
    case MachineRepresentation::kWord64:
      code = kRiscvWord64AtomicLoadUint64;
      break;
#ifdef V8_COMPRESS_POINTERS
    case MachineRepresentation::kTaggedSigned:
      code = kRiscvAtomicLoadDecompressTaggedSigned;
      break;
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTagged:
      code = kRiscvAtomicLoadDecompressTagged;
      break;
#else
    case MachineRepresentation::kTaggedSigned:   // Fall through.
    case MachineRepresentation::kTaggedPointer:  // Fall through.
    case MachineRepresentation::kTagged:
      DCHECK_EQ(kTaggedSize, 8);
      code = kRiscvWord64AtomicLoadUint64;
      break;
#endif
    default:
      UNREACHABLE();
  }

  if (atomic_load_params.kind() == MemoryAccessKind::kProtected) {
    code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }

  if (g.CanBeImmediate(index, code)) {
    selector->Emit(code | AddressingModeField::encode(kMode_MRI) |
                       AtomicWidthField::encode(width),
                   g.DefineAsRegister(node), g.UseRegister(base),
                   g.UseImmediate(index));
  } else {
    InstructionOperand addr_reg = g.TempRegister();
    selector->Emit(kRiscvAdd64 | AddressingModeField::encode(kMode_None),
                   addr_reg, g.UseRegister(index), g.UseRegister(base));
    // Emit desired load opcode, using temp addr_reg.
    selector->Emit(code | AddressingModeField::encode(kMode_MRI) |
                       AtomicWidthField::encode(width),
                   g.DefineAsRegister(node), addr_reg, g.TempImmediate(0));
  }
}

template <typename Adapter>
AtomicStoreParameters AtomicStoreParametersOf(
    InstructionSelectorT<Adapter>* selector, typename Adapter::node_t node) {
  auto store = selector->store_view(node);
  return AtomicStoreParameters(store.stored_rep().representation(),
                               store.stored_rep().write_barrier_kind(),
                               store.memory_order().value(),
                               store.access_kind());
}

template <typename Adapter>
void VisitAtomicStore(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node, AtomicWidth width) {
  using node_t = typename Adapter::node_t;
  RiscvOperandGeneratorT<Adapter> g(selector);
  auto store = selector->store_view(node);
  node_t base = store.base();
  node_t index = selector->value(store.index());
  node_t value = store.value();
  DCHECK_EQ(store.displacement(), 0);

  // The memory order is ignored.
  AtomicStoreParameters store_params = AtomicStoreParametersOf(selector, node);
  WriteBarrierKind write_barrier_kind = store_params.write_barrier_kind();
  MachineRepresentation rep = store_params.representation();

  if (v8_flags.enable_unconditional_write_barriers &&
      CanBeTaggedOrCompressedPointer(rep)) {
    write_barrier_kind = kFullWriteBarrier;
  }

  InstructionCode code;

  if (write_barrier_kind != kNoWriteBarrier &&
      !v8_flags.disable_write_barriers) {
    DCHECK(CanBeTaggedPointer(rep));
    DCHECK_EQ(AtomicWidthSize(width), kTaggedSize);

    InstructionOperand inputs[3];
    size_t input_count = 0;
    inputs[input_count++] = g.UseUniqueRegister(base);
    inputs[input_count++] = g.UseUniqueRegister(index);
    inputs[input_count++] = g.UseUniqueRegister(value);
    RecordWriteMode record_write_mode =
        WriteBarrierKindToRecordWriteMode(write_barrier_kind);
    InstructionOperand temps[] = {g.TempRegister(), g.TempRegister()};
    size_t const temp_count = arraysize(temps);
    code = kArchAtomicStoreWithWriteBarrier;
    code |= RecordWriteModeField::encode(record_write_mode);
    selector->Emit(code, 0, nullptr, input_count, inputs, temp_count, temps);
  } else {
    switch (rep) {
      case MachineRepresentation::kWord8:
        code = kAtomicStoreWord8;
        break;
      case MachineRepresentation::kWord16:
        code = kAtomicStoreWord16;
        break;
      case MachineRepresentation::kWord32:
        code = kAtomicStoreWord32;
        break;
      case MachineRepresentation::kWord64:
        DCHECK_EQ(width, AtomicWidth::kWord64);
        code = kRiscvWord64AtomicStoreWord64;
        break;
      case MachineRepresentation::kTaggedSigned:   // Fall through.
      case MachineRepresentation::kTaggedPointer:  // Fall through.
      case MachineRepresentation::kTagged:
        DCHECK_EQ(AtomicWidthSize(width), kTaggedSize);
        code = kRiscvStoreCompressTagged;
        break;
      default:
        UNREACHABLE();
    }
    code |= AtomicWidthField::encode(width);

    if (store_params.kind() == MemoryAccessKind::kProtected) {
      code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
    }
    if (g.CanBeImmediate(index, code)) {
      selector->Emit(code | AddressingModeField::encode(kMode_MRI) |
                         AtomicWidthField::encode(width),
                     g.NoOutput(), g.UseRegisterOrImmediateZero(value),
                     g.UseRegister(base), g.UseImmediate(index));
    } else {
      InstructionOperand addr_reg = g.TempRegister();
      selector->Emit(kRiscvAdd64 | AddressingModeField::encode(kMode_None),
                     addr_reg, g.UseRegister(index), g.UseRegister(base));
      // Emit desired store opcode, using temp addr_reg.
      selector->Emit(code | AddressingModeField::encode(kMode_MRI) |
                         AtomicWidthField::encode(width),
                     g.NoOutput(), g.UseRegisterOrImmediateZero(value),
                     addr_reg, g.TempImmediate(0));
    }
  }
}

template <typename Adapter>
void VisitAtomicBinop(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node, ArchOpcode opcode,
                      AtomicWidth width, MemoryAccessKind access_kind) {
  using node_t = typename Adapter::node_t;
  RiscvOperandGeneratorT<Adapter> g(selector);
  auto atomic_op = selector->atomic_rmw_view(node);
  node_t base = atomic_op.base();
  node_t index = atomic_op.index();
  node_t value = atomic_op.value();

  AddressingMode addressing_mode = kMode_MRI;
  InstructionOperand inputs[3];
  size_t input_count = 0;
  inputs[input_count++] = g.UseUniqueRegister(base);
  inputs[input_count++] = g.UseUniqueRegister(index);
  inputs[input_count++] = g.UseUniqueRegister(value);
  InstructionOperand outputs[1];
  outputs[0] = g.UseUniqueRegister(node);
  InstructionOperand temps[4];
  temps[0] = g.TempRegister();
  temps[1] = g.TempRegister();
  temps[2] = g.TempRegister();
  temps[3] = g.TempRegister();
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode) |
                         AtomicWidthField::encode(width);
  if (access_kind == MemoryAccessKind::kProtected) {
    code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }
  selector->Emit(code, 1, outputs, input_count, inputs, 4, temps);
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStackPointerGreaterThan(
    node_t node, FlagsContinuationT<Adapter>* cont) {
  StackCheckKind kind;
  node_t value;
  if constexpr (Adapter::IsTurboshaft) {
    const auto& op =
        this->turboshaft_graph()
            ->Get(node)
            .template Cast<turboshaft::StackPointerGreaterThanOp>();
    kind = op.kind;
    value = op.stack_limit();
  } else {
    kind = StackCheckKindOf(node->op());
    value = this->input_at(node, 0);
  }
  InstructionCode opcode =
      kArchStackPointerGreaterThan | MiscField::encode(static_cast<int>(kind));

  RiscvOperandGeneratorT<Adapter> g(this);

  // No outputs.
  InstructionOperand* const outputs = nullptr;
  const int output_count = 0;

  // Applying an offset to this stack check requires a temp register. Offsets
  // are only applied to the first stack check. If applying an offset, we must
  // ensure the input and temp registers do not alias, thus kUniqueRegister.
  InstructionOperand temps[] = {g.TempRegister()};
  const int temp_count = (kind == StackCheckKind::kJSFunctionEntry ? 1 : 0);
  const auto register_mode = (kind == StackCheckKind::kJSFunctionEntry)
                                 ? OperandGenerator::kUniqueRegister
                                 : OperandGenerator::kRegister;

  InstructionOperand inputs[] = {g.UseRegisterWithMode(value, register_mode)};
  static constexpr int input_count = arraysize(inputs);

  EmitWithContinuation(opcode, output_count, outputs, input_count, inputs,
                       temp_count, temps, cont);
}

bool CanCoverTrap(Node* user, Node* value) {
  if (user->opcode() != IrOpcode::kTrapUnless &&
      user->opcode() != IrOpcode::kTrapIf)
    return true;
  if (value->opcode() == IrOpcode::kWord32Equal ||
      value->opcode() == IrOpcode::kInt32LessThan ||
      value->opcode() == IrOpcode::kInt32LessThanOrEqual ||
      value->opcode() == IrOpcode::kUint32LessThan ||
      value->opcode() == IrOpcode::kUint32LessThanOrEqual)
    return false;
  return true;
}
// Shared routine for word comparisons against zero.
template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWordCompareZero(
    node_t user, node_t value, FlagsContinuation* cont) {
  // Try to combine with comparisons against 0 by simply inverting the branch.
  while (CanCover(user, value)) {
    if (value->opcode() == IrOpcode::kWord32Equal) {
      Int32BinopMatcher m(value);
      if (!m.right().Is(0)) break;
      user = value;
      value = m.left().node();
    } else if (value->opcode() == IrOpcode::kWord64Equal) {
      Int64BinopMatcher m(value);
      if (!m.right().Is(0)) break;
      user = value;
      value = m.left().node();
    } else {
      break;
    }
    cont->Negate();
  }

  if (CanCoverTrap(user, value) && CanCover(user, value)) {
    switch (value->opcode()) {
      case IrOpcode::kWord32Equal:
        cont->OverwriteAndNegateIfEqual(kEqual);
        return VisitWord32Compare(this, value, cont);
      case IrOpcode::kInt32LessThan:
        cont->OverwriteAndNegateIfEqual(kSignedLessThan);
        return VisitWord32Compare(this, value, cont);
      case IrOpcode::kInt32LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kSignedLessThanOrEqual);
        return VisitWord32Compare(this, value, cont);
      case IrOpcode::kUint32LessThan:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThan);
        return VisitWord32Compare(this, value, cont);
      case IrOpcode::kUint32LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThanOrEqual);
        return VisitWord32Compare(this, value, cont);
      case IrOpcode::kWord64Equal:
        cont->OverwriteAndNegateIfEqual(kEqual);
        return VisitWord64Compare(this, value, cont);
      case IrOpcode::kInt64LessThan:
        cont->OverwriteAndNegateIfEqual(kSignedLessThan);
        return VisitWord64Compare(this, value, cont);
      case IrOpcode::kInt64LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kSignedLessThanOrEqual);
        return VisitWord64Compare(this, value, cont);
      case IrOpcode::kUint64LessThan:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThan);
        return VisitWord64Compare(this, value, cont);
      case IrOpcode::kUint64LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThanOrEqual);
        return VisitWord64Compare(this, value, cont);
      case IrOpcode::kFloat32Equal:
        cont->OverwriteAndNegateIfEqual(kEqual);
        return VisitFloat32Compare(this, value, cont);
      case IrOpcode::kFloat32LessThan:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThan);
        return VisitFloat32Compare(this, value, cont);
      case IrOpcode::kFloat32LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThanOrEqual);
        return VisitFloat32Compare(this, value, cont);
      case IrOpcode::kFloat64Equal:
        cont->OverwriteAndNegateIfEqual(kEqual);
        return VisitFloat64Compare(this, value, cont);
      case IrOpcode::kFloat64LessThan:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThan);
        return VisitFloat64Compare(this, value, cont);
      case IrOpcode::kFloat64LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThanOrEqual);
        return VisitFloat64Compare(this, value, cont);
      case IrOpcode::kProjection:
        // Check if this is the overflow output projection of an
        // <Operation>WithOverflow node.
        if (ProjectionIndexOf(value->op()) == 1u) {
          // We cannot combine the <Operation>WithOverflow with this branch
          // unless the 0th projection (the use of the actual value of the
          // <Operation> is either nullptr, which means there's no use of the
          // actual value, or was already defined, which means it is scheduled
          // *AFTER* this branch).
          Node* const node = value->InputAt(0);
          Node* const result = NodeProperties::FindProjection(node, 0);
          if (result == nullptr || IsDefined(result)) {
            switch (node->opcode()) {
              case IrOpcode::kInt32AddWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop<TurbofanAdapter, Int32BinopMatcher>(
                    this, node, kRiscvAdd64, cont);
              case IrOpcode::kInt32SubWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop<TurbofanAdapter, Int32BinopMatcher>(
                    this, node, kRiscvSub64, cont);
              case IrOpcode::kInt32MulWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop<TurbofanAdapter, Int32BinopMatcher>(
                    this, node, kRiscvMulOvf32, cont);
              case IrOpcode::kInt64AddWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop<TurbofanAdapter, Int32BinopMatcher>(
                    this, node, kRiscvAddOvf64, cont);
              case IrOpcode::kInt64SubWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop<TurbofanAdapter, Int32BinopMatcher>(
                    this, node, kRiscvSubOvf64, cont);
              default:
                break;
            }
          }
        }
        break;
      case IrOpcode::kWord32And:
#if V8_COMPRESS_POINTERS
          return VisitWordCompare(this, value, kRiscvTst32, cont, true);
#endif
        case IrOpcode::kWord64And:
          return VisitWordCompare(this, value, kRiscvTst64, cont, true);
        case IrOpcode::kStackPointerGreaterThan:
          cont->OverwriteAndNegateIfEqual(kStackPointerGreaterThanCondition);
          return VisitStackPointerGreaterThan(value, cont);
        default:
          break;
      }
    }

    // Continuation could not be combined with a compare, emit compare against
    // 0.
#ifdef V8_COMPRESS_POINTERS
    switch (user->opcode()) {
      case IrOpcode::kWord64Equal:
      case IrOpcode::kInt64LessThan:
      case IrOpcode::kInt64LessThanOrEqual:
      case IrOpcode::kUint64LessThan:
      case IrOpcode::kUint64LessThanOrEqual:
        return EmitWordCompareZero(this, value, cont);
      default:
        return EmitWord32CompareZero(this, value, cont);
    }
#else
    switch (user->opcode()) {
      case IrOpcode::kWord32Equal:
      case IrOpcode::kInt32LessThan:
      case IrOpcode::kInt32LessThanOrEqual:
      case IrOpcode::kUint32LessThan:
      case IrOpcode::kUint32LessThanOrEqual:
        return EmitWord32CompareZero(this, value, cont);
      default:
        return EmitWordCompareZero(this, value, cont);
    }
#endif
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWordCompareZero(
    node_t user, node_t value, FlagsContinuation* cont) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  // Try to combine with comparisons against 0 by simply inverting the branch.
  while (const ComparisonOp* equal =
             this->TryCast<Opmask::kWord32Equal>(value)) {
    if (!CanCover(user, value)) break;
    if (!MatchIntegralZero(equal->right())) break;

    user = value;
    value = equal->left();
    cont->Negate();
  }

  const Operation& value_op = Get(value);
  if (CanCover(user, value)) {
    if (const ComparisonOp* comparison = value_op.TryCast<ComparisonOp>()) {
      switch (comparison->rep.value()) {
        case RegisterRepresentation::Word32():
          cont->OverwriteAndNegateIfEqual(
              GetComparisonFlagCondition(*comparison));
          return VisitWord32Compare(this, value, cont);

        case RegisterRepresentation::Word64():
          cont->OverwriteAndNegateIfEqual(
              GetComparisonFlagCondition(*comparison));
          return VisitWord64Compare(this, value, cont);

        case RegisterRepresentation::Float32():
          switch (comparison->kind) {
            case ComparisonOp::Kind::kEqual:
              cont->OverwriteAndNegateIfEqual(kEqual);
              return VisitFloat32Compare(this, value, cont);
            case ComparisonOp::Kind::kSignedLessThan:
              cont->OverwriteAndNegateIfEqual(kFloatLessThan);
              return VisitFloat32Compare(this, value, cont);
            case ComparisonOp::Kind::kSignedLessThanOrEqual:
              cont->OverwriteAndNegateIfEqual(kFloatLessThanOrEqual);
              return VisitFloat32Compare(this, value, cont);
            default:
              UNREACHABLE();
          }
        case RegisterRepresentation::Float64():
          switch (comparison->kind) {
            case ComparisonOp::Kind::kEqual:
              cont->OverwriteAndNegateIfEqual(kEqual);
              return VisitFloat64Compare(this, value, cont);
            case ComparisonOp::Kind::kSignedLessThan:
              cont->OverwriteAndNegateIfEqual(kFloatLessThan);
              return VisitFloat64Compare(this, value, cont);
            case ComparisonOp::Kind::kSignedLessThanOrEqual:
              cont->OverwriteAndNegateIfEqual(kFloatLessThanOrEqual);
              return VisitFloat64Compare(this, value, cont);
            default:
              UNREACHABLE();
          }
        default:
          break;
      }
    } else if (const ProjectionOp* projection =
                   value_op.TryCast<ProjectionOp>()) {
      // Check if this is the overflow output projection of an
      // <Operation>WithOverflow node.
      if (projection->index == 1u) {
        // We cannot combine the <Operation>WithOverflow with this branch
        // unless the 0th projection (the use of the actual value of the
        // <Operation> is either nullptr, which means there's no use of the
        // actual value, or was already defined, which means it is scheduled
        // *AFTER* this branch).
        OpIndex node = projection->input();
        OpIndex result = FindProjection(node, 0);
        if (!result.valid() || IsDefined(result)) {
          if (const OverflowCheckedBinopOp* binop =
                  TryCast<OverflowCheckedBinopOp>(node)) {
            const bool is64 = binop->rep == WordRepresentation::Word64();
            switch (binop->kind) {
              case OverflowCheckedBinopOp::Kind::kSignedAdd:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop<TurboshaftAdapter, Int32BinopMatcher>(
                    this, node, is64 ? kRiscvAddOvf64 : kRiscvAdd64, cont);
              case OverflowCheckedBinopOp::Kind::kSignedSub:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop<TurboshaftAdapter, Int32BinopMatcher>(
                    this, node, is64 ? kRiscvSubOvf64 : kRiscvSub64, cont);
              case OverflowCheckedBinopOp::Kind::kSignedMul:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop<TurboshaftAdapter, Int32BinopMatcher>(
                    this, node, is64 ? kRiscvMulOvf64 : kRiscvMulOvf32, cont);
            }
          }
        }
      }
    }
  }

  // Continuation could not be combined with a compare, emit compare against
  // 0.
  const ComparisonOp* comparison = this->Get(user).TryCast<ComparisonOp>();
#ifdef V8_COMPRESS_POINTERS
  if (comparison &&
      comparison->rep.value() == RegisterRepresentation::Word64()) {
    return EmitWordCompareZero(this, value, cont);
  } else {
    return EmitWord32CompareZero(this, value, cont);
  }
#else
  if (comparison &&
      comparison->rep.value() == RegisterRepresentation::Word32()) {
    return EmitWord32CompareZero(this, value, cont);
  } else {
    return EmitWordCompareZero(this, value, cont);
  }
#endif
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Equal(node_t node) {
    Node* const user = node;
    FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
    Int32BinopMatcher m(user);
    if (m.right().Is(0)) {
      return VisitWordCompareZero(m.node(), m.left().node(), &cont);
    }
    if (isolate() && (V8_STATIC_ROOTS_BOOL ||
                      (COMPRESS_POINTERS_BOOL && !isolate()->bootstrapper()))) {
      RiscvOperandGeneratorT<Adapter> g(this);
      const RootsTable& roots_table = isolate()->roots_table();
      RootIndex root_index;
      Node* left = nullptr;
      Handle<HeapObject> right;
      // HeapConstants and CompressedHeapConstants can be treated the same when
      // using them as an input to a 32-bit comparison. Check whether either is
      // present.
      {
        CompressedHeapObjectBinopMatcher m(node);
        if (m.right().HasResolvedValue()) {
          left = m.left().node();
          right = m.right().ResolvedValue();
        } else {
          HeapObjectBinopMatcher m2(node);
          if (m2.right().HasResolvedValue()) {
            left = m2.left().node();
            right = m2.right().ResolvedValue();
          }
        }
      }
      if (!right.is_null() && roots_table.IsRootHandle(right, &root_index)) {
        DCHECK_NE(left, nullptr);
        if (RootsTable::IsReadOnly(root_index)) {
          Tagged_t ptr =
              MacroAssemblerBase::ReadOnlyRootPtr(root_index, isolate());
          if (g.CanBeImmediate(ptr, kRiscvCmp32)) {
            return VisitCompare(this, kRiscvCmp32, g.UseRegister(left),
                                g.TempImmediate(int32_t(ptr)), &cont);
          }
        }
      }
    }
    VisitWord32Compare(this, node, &cont);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord32Equal(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Operation& equal = Get(node);
  DCHECK(equal.Is<ComparisonOp>());
  OpIndex left = equal.input(0);
  OpIndex right = equal.input(1);
  OpIndex user = node;
  FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);

  if (MatchZero(right)) {
    return VisitWordCompareZero(user, left, &cont);
  }

  if (isolate() && (V8_STATIC_ROOTS_BOOL ||
                    (COMPRESS_POINTERS_BOOL && !isolate()->bootstrapper()))) {
    RiscvOperandGeneratorT<TurboshaftAdapter> g(this);
    const RootsTable& roots_table = isolate()->roots_table();
    RootIndex root_index;
    Handle<HeapObject> right;
    // HeapConstants and CompressedHeapConstants can be treated the same when
    // using them as an input to a 32-bit comparison. Check whether either is
    // present.
    if (MatchHeapConstant(node, &right) && !right.is_null() &&
        roots_table.IsRootHandle(right, &root_index)) {
      if (RootsTable::IsReadOnly(root_index)) {
        Tagged_t ptr =
            MacroAssemblerBase::ReadOnlyRootPtr(root_index, isolate());
        if (g.CanBeImmediate(ptr, kRiscvCmp32)) {
          return VisitCompare(this, kRiscvCmp32, g.UseRegister(left),
                              g.TempImmediate(int32_t(ptr)), &cont);
        }
      }
    }
  }
  VisitWord32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32LessThan(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kSignedLessThan, node);
  VisitWord32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kSignedLessThanOrEqual, node);
  VisitWord32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32LessThan(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kUnsignedLessThan, node);
  VisitWord32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kUnsignedLessThanOrEqual, node);
  VisitWord32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32AddWithOverflow(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    OpIndex ovf = FindProjection(node, 1);
    if (ovf.valid() && IsUsed(ovf)) {
      FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
      return VisitBinop<Adapter, Int32BinopMatcher>(this, node, kRiscvAdd64,
                                                    &cont);
    }
    FlagsContinuation cont;
    VisitBinop<Adapter, Int32BinopMatcher>(this, node, kRiscvAdd64, &cont);
  } else {
    if (Node* ovf = NodeProperties::FindProjection(node, 1)) {
      FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
      return VisitBinop<Adapter, Int32BinopMatcher>(this, node, kRiscvAdd64,
                                                    &cont);
    }
    FlagsContinuation cont;
    VisitBinop<Adapter, Int32BinopMatcher>(this, node, kRiscvAdd64, &cont);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32SubWithOverflow(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    OpIndex ovf = FindProjection(node, 1);
    if (ovf.valid()) {
      FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
      return VisitBinop<Adapter, Int32BinopMatcher>(this, node, kRiscvSub64,
                                                    &cont);
    }
    FlagsContinuation cont;
    VisitBinop<Adapter, Int32BinopMatcher>(this, node, kRiscvSub64, &cont);
  } else {
    if (Node* ovf = NodeProperties::FindProjection(node, 1)) {
      FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
      return VisitBinop<Adapter, Int32BinopMatcher>(this, node, kRiscvSub64,
                                                    &cont);
    }
    FlagsContinuation cont;
    VisitBinop<Adapter, Int32BinopMatcher>(this, node, kRiscvSub64, &cont);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32MulWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (this->valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop<Adapter, Int32BinopMatcher>(this, node, kRiscvMulOvf32,
                                                  &cont);
  }
  FlagsContinuation cont;
  VisitBinop<Adapter, Int32BinopMatcher>(this, node, kRiscvMulOvf32, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64AddWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (this->valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop<Adapter, Int64BinopMatcher>(this, node, kRiscvAddOvf64,
                                                  &cont);
  }
  FlagsContinuation cont;
  VisitBinop<Adapter, Int64BinopMatcher>(this, node, kRiscvAddOvf64, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64SubWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (this->valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop<Adapter, Int64BinopMatcher>(this, node, kRiscvSubOvf64,
                                                  &cont);
  }
    FlagsContinuation cont;
    VisitBinop<Adapter, Int64BinopMatcher>(this, node, kRiscvSubOvf64, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64MulWithOverflow(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    OpIndex ovf = FindProjection(node, 1);
    if (ovf.valid()) {
      FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
      return VisitBinop<Adapter, Int64BinopMatcher>(this, node, kRiscvMulOvf64,
                                                    &cont);
    }
  } else {
    if (Node* ovf = NodeProperties::FindProjection(node, 1)) {
      FlagsContinuation cont = FlagsContinuation::ForSet(kNotEqual, ovf);
      return VisitBinop<Adapter, Int64BinopMatcher>(this, node, kRiscvMulOvf64,
                                                    &cont);
    }
  }
    FlagsContinuation cont;
    VisitBinop<Adapter, Int64BinopMatcher>(this, node, kRiscvMulOvf64, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Equal(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
    const ComparisonOp& equal = this->Get(node).template Cast<ComparisonOp>();
    DCHECK_EQ(equal.kind, ComparisonOp::Kind::kEqual);
    if (this->MatchIntegralZero(equal.right())) {
      return VisitWordCompareZero(node, equal.left(), &cont);
    }
    VisitWord64Compare(this, node, &cont);
  } else {
    FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
    Int64BinopMatcher m(node);
    if (m.right().Is(0)) {
      return VisitWordCompareZero(m.node(), m.left().node(), &cont);
    }

    VisitWord64Compare(this, node, &cont);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64LessThan(node_t node) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kSignedLessThan, node);
    VisitWord64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64LessThanOrEqual(node_t node) {
    FlagsContinuation cont =
        FlagsContinuation::ForSet(kSignedLessThanOrEqual, node);
    VisitWord64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint64LessThan(node_t node) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kUnsignedLessThan, node);
    VisitWord64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint64LessThanOrEqual(node_t node) {
    FlagsContinuation cont =
        FlagsContinuation::ForSet(kUnsignedLessThanOrEqual, node);
    VisitWord64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicLoad(node_t node) {
    VisitAtomicLoad(this, node, AtomicWidth::kWord32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicStore(node_t node) {
    VisitAtomicStore(this, node, AtomicWidth::kWord32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64AtomicLoad(node_t node) {
    VisitAtomicLoad(this, node, AtomicWidth::kWord64);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64AtomicStore(node_t node) {
    VisitAtomicStore(this, node, AtomicWidth::kWord64);
}

template <typename Adapter>
void VisitAtomicExchange(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node, ArchOpcode opcode,
                         AtomicWidth width, MemoryAccessKind access_kind) {
  using node_t = typename Adapter::node_t;
  auto atomic_op = selector->atomic_rmw_view(node);
  RiscvOperandGeneratorT<Adapter> g(selector);
  node_t base = atomic_op.base();
  node_t index = atomic_op.index();
  node_t value = atomic_op.value();

  InstructionOperand inputs[3];
  size_t input_count = 0;
  inputs[input_count++] = g.UseUniqueRegister(base);
  inputs[input_count++] = g.UseUniqueRegister(index);
  inputs[input_count++] = g.UseUniqueRegister(value);
  InstructionOperand outputs[1];
  outputs[0] = g.UseUniqueRegister(node);
  InstructionOperand temp[3];
  temp[0] = g.TempRegister();
  temp[1] = g.TempRegister();
  temp[2] = g.TempRegister();

  InstructionCode code = opcode | AddressingModeField::encode(kMode_MRI) |
                         AtomicWidthField::encode(width);
  if (access_kind == MemoryAccessKind::kProtected) {
    code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }
  selector->Emit(code, 1, outputs, input_count, inputs, 3, temp);
}

template <typename Adapter>
void VisitAtomicCompareExchange(InstructionSelectorT<Adapter>* selector,
                                typename Adapter::node_t node,
                                ArchOpcode opcode, AtomicWidth width,
                                MemoryAccessKind access_kind) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  using node_t = typename Adapter::node_t;
  auto atomic_op = selector->atomic_rmw_view(node);
  node_t base = atomic_op.base();
  node_t index = atomic_op.index();
  node_t old_value = atomic_op.expected();
  node_t new_value = atomic_op.value();

  AddressingMode addressing_mode = kMode_MRI;
  InstructionOperand inputs[4];
  size_t input_count = 0;
  inputs[input_count++] = g.UseUniqueRegister(base);
  inputs[input_count++] = g.UseUniqueRegister(index);
  inputs[input_count++] = g.UseUniqueRegister(old_value);
  inputs[input_count++] = g.UseUniqueRegister(new_value);
  InstructionOperand outputs[1];
  outputs[0] = g.UseUniqueRegister(node);
  InstructionOperand temp[3];
  temp[0] = g.TempRegister();
  temp[1] = g.TempRegister();
  temp[2] = g.TempRegister();
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode) |
                         AtomicWidthField::encode(width);
  if (access_kind == MemoryAccessKind::kProtected) {
    code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }
  selector->Emit(code, 1, outputs, input_count, inputs, 3, temp);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord32AtomicExchange(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
  ArchOpcode opcode;
  if (atomic_op.memory_rep == MemoryRepresentation::Int8()) {
    opcode = kAtomicExchangeInt8;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
    opcode = kAtomicExchangeUint8;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Int16()) {
    opcode = kAtomicExchangeInt16;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
    opcode = kAtomicExchangeUint16;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Int32() ||
             atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
    opcode = kAtomicExchangeWord32;
  } else {
    UNREACHABLE();
  }
  VisitAtomicExchange(this, node, opcode, AtomicWidth::kWord32,
                      atomic_op.memory_access_kind);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord32AtomicExchange(
    Node* node) {
  ArchOpcode opcode;
  AtomicOpParameters params = AtomicOpParametersOf(node->op());
  if (params.type() == MachineType::Int8()) {
    opcode = kAtomicExchangeInt8;
  } else if (params.type() == MachineType::Uint8()) {
    opcode = kAtomicExchangeUint8;
  } else if (params.type() == MachineType::Int16()) {
    opcode = kAtomicExchangeInt16;
  } else if (params.type() == MachineType::Uint16()) {
    opcode = kAtomicExchangeUint16;
  } else if (params.type() == MachineType::Int32() ||
             params.type() == MachineType::Uint32()) {
    opcode = kAtomicExchangeWord32;
  } else {
    UNREACHABLE();
  }

  VisitAtomicExchange(this, node, opcode, AtomicWidth::kWord32, params.kind());
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord64AtomicExchange(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
  ArchOpcode opcode;
  if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
    opcode = kAtomicExchangeUint8;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
    opcode = kAtomicExchangeUint16;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
    opcode = kAtomicExchangeWord32;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Uint64()) {
    opcode = kRiscvWord64AtomicExchangeUint64;
  } else {
    UNREACHABLE();
  }
  VisitAtomicExchange(this, node, opcode, AtomicWidth::kWord64,
                      atomic_op.memory_access_kind);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord64AtomicExchange(
    Node* node) {
  ArchOpcode opcode;
  AtomicOpParameters params = AtomicOpParametersOf(node->op());
  if (params.type() == MachineType::Uint8()) {
    opcode = kAtomicExchangeUint8;
  } else if (params.type() == MachineType::Uint16()) {
    opcode = kAtomicExchangeUint16;
  } else if (params.type() == MachineType::Uint32()) {
    opcode = kAtomicExchangeWord32;
  } else if (params.type() == MachineType::Uint64()) {
    opcode = kRiscvWord64AtomicExchangeUint64;
  } else {
    UNREACHABLE();
  }
  VisitAtomicExchange(this, node, opcode, AtomicWidth::kWord64, params.kind());
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord32AtomicCompareExchange(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
  ArchOpcode opcode;
  if (atomic_op.memory_rep == MemoryRepresentation::Int8()) {
    opcode = kAtomicCompareExchangeInt8;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
    opcode = kAtomicCompareExchangeUint8;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Int16()) {
    opcode = kAtomicCompareExchangeInt16;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
    opcode = kAtomicCompareExchangeUint16;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Int32() ||
             atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
    opcode = kAtomicCompareExchangeWord32;
  } else {
    UNREACHABLE();
  }
  VisitAtomicCompareExchange(this, node, opcode, AtomicWidth::kWord32,
                             atomic_op.memory_access_kind);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord32AtomicCompareExchange(
    Node* node) {
  ArchOpcode opcode;
  AtomicOpParameters params = AtomicOpParametersOf(node->op());
  if (params.type() == MachineType::Int8()) {
    opcode = kAtomicCompareExchangeInt8;
  } else if (params.type() == MachineType::Uint8()) {
    opcode = kAtomicCompareExchangeUint8;
  } else if (params.type() == MachineType::Int16()) {
    opcode = kAtomicCompareExchangeInt16;
  } else if (params.type() == MachineType::Uint16()) {
    opcode = kAtomicCompareExchangeUint16;
  } else if (params.type() == MachineType::Int32() ||
             params.type() == MachineType::Uint32()) {
    opcode = kAtomicCompareExchangeWord32;
  } else {
    UNREACHABLE();
  }

  VisitAtomicCompareExchange(this, node, opcode, AtomicWidth::kWord32,
                             params.kind());
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord64AtomicCompareExchange(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
  ArchOpcode opcode;
  if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
    opcode = kAtomicCompareExchangeUint8;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
    opcode = kAtomicCompareExchangeUint16;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
    opcode = kAtomicCompareExchangeWord32;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Uint64()) {
    opcode = kRiscvWord64AtomicCompareExchangeUint64;
  } else {
    UNREACHABLE();
  }
  VisitAtomicCompareExchange(this, node, opcode, AtomicWidth::kWord64,
                             atomic_op.memory_access_kind);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord64AtomicCompareExchange(
    Node* node) {
  ArchOpcode opcode;
  AtomicOpParameters params = AtomicOpParametersOf(node->op());
  if (params.type() == MachineType::Uint8()) {
    opcode = kAtomicCompareExchangeUint8;
  } else if (params.type() == MachineType::Uint16()) {
    opcode = kAtomicCompareExchangeUint16;
  } else if (params.type() == MachineType::Uint32()) {
    opcode = kAtomicCompareExchangeWord32;
  } else if (params.type() == MachineType::Uint64()) {
    opcode = kRiscvWord64AtomicCompareExchangeUint64;
  } else {
    UNREACHABLE();
  }
  VisitAtomicCompareExchange(this, node, opcode, AtomicWidth::kWord64,
                             params.kind());
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicBinaryOperation(
    node_t node, ArchOpcode int8_op, ArchOpcode uint8_op, ArchOpcode int16_op,
    ArchOpcode uint16_op, ArchOpcode word32_op) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
    ArchOpcode opcode;
    if (atomic_op.memory_rep == MemoryRepresentation::Int8()) {
      opcode = int8_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
      opcode = uint8_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Int16()) {
      opcode = int16_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
      opcode = uint16_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Int32() ||
               atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
      opcode = word32_op;
    } else {
      UNREACHABLE();
    }
    VisitAtomicBinop(this, node, opcode, AtomicWidth::kWord32,
                     atomic_op.memory_access_kind);
  } else {
    ArchOpcode opcode;
    AtomicOpParameters params = AtomicOpParametersOf(node->op());
    if (params.type() == MachineType::Int8()) {
      opcode = int8_op;
    } else if (params.type() == MachineType::Uint8()) {
      opcode = uint8_op;
    } else if (params.type() == MachineType::Int16()) {
      opcode = int16_op;
    } else if (params.type() == MachineType::Uint16()) {
      opcode = uint16_op;
    } else if (params.type() == MachineType::Int32() ||
               params.type() == MachineType::Uint32()) {
      opcode = word32_op;
    } else {
      UNREACHABLE();
    }

    VisitAtomicBinop(this, node, opcode, AtomicWidth::kWord32, params.kind());
  }
}

#define VISIT_ATOMIC_BINOP(op)                                             \
  template <typename Adapter>                                              \
  void InstructionSelectorT<Adapter>::VisitWord32Atomic##op(node_t node) { \
      VisitWord32AtomicBinaryOperation(                                    \
          node, kAtomic##op##Int8, kAtomic##op##Uint8, kAtomic##op##Int16, \
          kAtomic##op##Uint16, kAtomic##op##Word32);                       \
  }
VISIT_ATOMIC_BINOP(Add)
VISIT_ATOMIC_BINOP(Sub)
VISIT_ATOMIC_BINOP(And)
VISIT_ATOMIC_BINOP(Or)
VISIT_ATOMIC_BINOP(Xor)
#undef VISIT_ATOMIC_BINOP

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64AtomicBinaryOperation(
    node_t node, ArchOpcode uint8_op, ArchOpcode uint16_op,
    ArchOpcode uint32_op, ArchOpcode uint64_op) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
    ArchOpcode opcode;
    if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
      opcode = uint8_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
      opcode = uint16_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
      opcode = uint32_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint64()) {
      opcode = uint64_op;
    } else {
      UNREACHABLE();
    }
    VisitAtomicBinop(this, node, opcode, AtomicWidth::kWord64,
                     atomic_op.memory_access_kind);
  } else {
    ArchOpcode opcode;
    AtomicOpParameters params = AtomicOpParametersOf(node->op());
    if (params.type() == MachineType::Uint8()) {
      opcode = uint8_op;
    } else if (params.type() == MachineType::Uint16()) {
      opcode = uint16_op;
    } else if (params.type() == MachineType::Uint32()) {
      opcode = uint32_op;
    } else if (params.type() == MachineType::Uint64()) {
      opcode = uint64_op;
    } else {
      UNREACHABLE();
    }
    VisitAtomicBinop(this, node, opcode, AtomicWidth::kWord64, params.kind());
  }
}

#define VISIT_ATOMIC_BINOP(op)                                                \
  template <typename Adapter>                                                 \
  void InstructionSelectorT<Adapter>::VisitWord64Atomic##op(node_t node) {    \
      VisitWord64AtomicBinaryOperation(                                       \
          node, kAtomic##op##Uint8, kAtomic##op##Uint16, kAtomic##op##Word32, \
          kRiscvWord64Atomic##op##Uint64);                                    \
  }
VISIT_ATOMIC_BINOP(Add)
VISIT_ATOMIC_BINOP(Sub)
VISIT_ATOMIC_BINOP(And)
VISIT_ATOMIC_BINOP(Or)
VISIT_ATOMIC_BINOP(Xor)
#undef VISIT_ATOMIC_BINOP

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32AbsWithOverflow(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64AbsWithOverflow(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord8ToInt64(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kRiscvSignExtendByte, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord16ToInt64(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kRiscvSignExtendShort, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord32ToInt64(node_t node) {
    EmitSignExtendWord(this, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Min(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp1 = g.TempFpRegister(v0);
    InstructionOperand temp2 = g.TempFpRegister(kSimd128ScratchReg);
    InstructionOperand mask_reg = g.TempFpRegister(v0);
    this->Emit(kRiscvVmfeqVv, temp1, g.UseRegister(this->input_at(node, 0)),
               g.UseRegister(this->input_at(node, 0)), g.UseImmediate(E64),
               g.UseImmediate(m1));
    this->Emit(kRiscvVmfeqVv, temp2, g.UseRegister(this->input_at(node, 1)),
               g.UseRegister(this->input_at(node, 1)), g.UseImmediate(E64),
               g.UseImmediate(m1));
    this->Emit(kRiscvVandVv, mask_reg, temp2, temp1, g.UseImmediate(E64),
               g.UseImmediate(m1));

    InstructionOperand NaN = g.TempFpRegister(kSimd128ScratchReg);
    InstructionOperand result = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVmv, NaN, g.UseImmediate64(0x7ff8000000000000L),
               g.UseImmediate(E64), g.UseImmediate(m1));
    this->Emit(kRiscvVfminVv, result, g.UseRegister(this->input_at(node, 1)),
               g.UseRegister(this->input_at(node, 0)), g.UseImmediate(E64),
               g.UseImmediate(m1), g.UseImmediate(MaskType::Mask));
    this->Emit(kRiscvVmv, g.DefineAsRegister(node), result, g.UseImmediate(E64),
               g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Max(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp1 = g.TempFpRegister(v0);
    InstructionOperand temp2 = g.TempFpRegister(kSimd128ScratchReg);
    InstructionOperand mask_reg = g.TempFpRegister(v0);
    this->Emit(kRiscvVmfeqVv, temp1, g.UseRegister(this->input_at(node, 0)),
               g.UseRegister(this->input_at(node, 0)), g.UseImmediate(E64),
               g.UseImmediate(m1));
    this->Emit(kRiscvVmfeqVv, temp2, g.UseRegister(this->input_at(node, 1)),
               g.UseRegister(this->input_at(node, 1)), g.UseImmediate(E64),
               g.UseImmediate(m1));
    this->Emit(kRiscvVandVv, mask_reg, temp2, temp1, g.UseImmediate(E64),
               g.UseImmediate(m1));

    InstructionOperand NaN = g.TempFpRegister(kSimd128ScratchReg);
    InstructionOperand result = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVmv, NaN, g.UseImmediate64(0x7ff8000000000000L),
               g.UseImmediate(E64), g.UseImmediate(m1));
    this->Emit(kRiscvVfmaxVv, result, g.UseRegister(this->input_at(node, 1)),
               g.UseRegister(this->input_at(node, 0)), g.UseImmediate(E64),
               g.UseImmediate(m1), g.UseImmediate(MaskType::Mask));
    this->Emit(kRiscvVmv, g.DefineAsRegister(node), result, g.UseImmediate(E64),
               g.UseImmediate(m1));
  }
}
// static
MachineOperatorBuilder::Flags
InstructionSelector::SupportedMachineOperatorFlags() {
  MachineOperatorBuilder::Flags flags = MachineOperatorBuilder::kNoFlags;
  return flags | MachineOperatorBuilder::kWord32Ctz |
         MachineOperatorBuilder::kWord64Ctz |
         MachineOperatorBuilder::kWord32Popcnt |
         MachineOperatorBuilder::kWord64Popcnt |
         MachineOperatorBuilder::kWord32ShiftIsSafe |
         MachineOperatorBuilder::kInt32DivIsSafe |
         MachineOperatorBuilder::kUint32DivIsSafe |
         MachineOperatorBuilder::kFloat64RoundDown |
         MachineOperatorBuilder::kFloat32RoundDown |
         MachineOperatorBuilder::kFloat64RoundUp |
         MachineOperatorBuilder::kFloat32RoundUp |
         MachineOperatorBuilder::kFloat64RoundTruncate |
         MachineOperatorBuilder::kFloat32RoundTruncate |
         MachineOperatorBuilder::kFloat64RoundTiesEven |
         MachineOperatorBuilder::kFloat32RoundTiesEven;
}
#undef TRACE_UNIMPL
#undef TRACE

template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    InstructionSelectorT<TurbofanAdapter>;
template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    InstructionSelectorT<TurboshaftAdapter>;
}  // namespace compiler
}  // namespace internal
}  // namespace v8
