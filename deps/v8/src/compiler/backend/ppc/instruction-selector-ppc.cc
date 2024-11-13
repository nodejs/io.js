// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "src/base/iterator.h"
#include "src/compiler/backend/instruction-selector-impl.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/turboshaft/opmasks.h"
#include "src/execution/ppc/frame-constants-ppc.h"
#include "src/roots/roots-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

enum ImmediateMode {
  kInt16Imm,
  kInt16Imm_Unsigned,
  kInt16Imm_Negate,
  kInt16Imm_4ByteAligned,
  kShift32Imm,
  kInt34Imm,
  kShift64Imm,
  kNoImmediate
};

// Adds PPC-specific methods for generating operands.
template <typename Adapter>
class PPCOperandGeneratorT final : public OperandGeneratorT<Adapter> {
 public:
  OPERAND_GENERATOR_T_BOILERPLATE(Adapter)

  explicit PPCOperandGeneratorT<Adapter>(
      InstructionSelectorT<Adapter>* selector)
      : super(selector) {}

  InstructionOperand UseOperand(node_t node, ImmediateMode mode) {
    if (CanBeImmediate(node, mode)) {
      return UseImmediate(node);
    }
    return UseRegister(node);
  }

  bool CanBeImmediate(node_t node, ImmediateMode mode) {
    if (!this->is_constant(node)) return false;
    auto constant = this->constant_view(node);
    if (constant.is_compressed_heap_object()) {
      if (!COMPRESS_POINTERS_BOOL) return false;
      // For builtin code we need static roots
      if (selector()->isolate()->bootstrapper() && !V8_STATIC_ROOTS_BOOL) {
        return false;
      }
      const RootsTable& roots_table = selector()->isolate()->roots_table();
      RootIndex root_index;
      Handle<HeapObject> value = constant.heap_object_value();
      if (roots_table.IsRootHandle(value, &root_index)) {
        if (!RootsTable::IsReadOnly(root_index)) return false;
        return CanBeImmediate(MacroAssemblerBase::ReadOnlyRootPtr(
                                  root_index, selector()->isolate()),
                              mode);
      }
      return false;
    }

    if (!selector()->is_integer_constant(node)) return false;
    int64_t value = selector()->integer_constant(node);
    return CanBeImmediate(value, mode);
  }

  bool CanBeImmediate(int64_t value, ImmediateMode mode) {
    switch (mode) {
      case kInt16Imm:
        return is_int16(value);
      case kInt16Imm_Unsigned:
        return is_uint16(value);
      case kInt16Imm_Negate:
        return is_int16(-value);
      case kInt16Imm_4ByteAligned:
        return is_int16(value) && !(value & 3);
      case kShift32Imm:
        return 0 <= value && value < 32;
      case kInt34Imm:
        return is_int34(value);
      case kShift64Imm:
        return 0 <= value && value < 64;
      case kNoImmediate:
        return false;
    }
    return false;
  }
};

namespace {

template <typename Adapter>
void VisitRR(InstructionSelectorT<Adapter>* selector, InstructionCode opcode,
             typename Adapter::node_t node) {
  PPCOperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)));
}

template <typename Adapter>
void VisitRRR(InstructionSelectorT<Adapter>* selector, InstructionCode opcode,
              typename Adapter::node_t node) {
  PPCOperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)),
                 g.UseRegister(selector->input_at(node, 1)));
}

template <typename Adapter>
void VisitRRO(InstructionSelectorT<Adapter>* selector, InstructionCode opcode,
              typename Adapter::node_t node, ImmediateMode operand_mode) {
  PPCOperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)),
                 g.UseOperand(selector->input_at(node, 1), operand_mode));
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void VisitTryTruncateDouble(InstructionSelectorT<Adapter>* selector,
                            InstructionCode opcode,
                            typename Adapter::node_t node) {
  using node_t = typename Adapter::node_t;
  PPCOperandGeneratorT<Adapter> g(selector);
  InstructionOperand inputs[] = {g.UseRegister(selector->input_at(node, 0))};
  InstructionOperand outputs[2];
  size_t output_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = selector->FindProjection(node, 1);
  if (selector->valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
  }

  selector->Emit(opcode, output_count, outputs, 1, inputs);
}
#endif

// Shared routine for multiple binary operations.
template <typename Adapter>
void VisitBinop(InstructionSelectorT<Adapter>* selector,
                typename Adapter::node_t node, InstructionCode opcode,
                ImmediateMode operand_mode, FlagsContinuationT<Adapter>* cont) {
  PPCOperandGeneratorT<Adapter> g(selector);
  InstructionOperand inputs[4];
  size_t input_count = 0;
  InstructionOperand outputs[2];
  size_t output_count = 0;

  inputs[input_count++] = g.UseRegister(selector->input_at(node, 0));
  inputs[input_count++] =
      g.UseOperand(selector->input_at(node, 1), operand_mode);

  if (cont->IsDeoptimize()) {
    // If we can deoptimize as a result of the binop, we need to make sure that
    // the deopt inputs are not overwritten by the binop result. One way
    // to achieve that is to declare the output register as same-as-first.
    outputs[output_count++] = g.DefineSameAsFirst(node);
  } else {
    outputs[output_count++] = g.DefineAsRegister(node);
  }

  DCHECK_NE(0u, input_count);
  DCHECK_NE(0u, output_count);
  DCHECK_GE(arraysize(inputs), input_count);
  DCHECK_GE(arraysize(outputs), output_count);

  selector->EmitWithContinuation(opcode, output_count, outputs, input_count,
                                 inputs, cont);
}

// Shared routine for multiple binary operations.
template <typename Adapter>
void VisitBinop(InstructionSelectorT<Adapter>* selector,
                typename Adapter::node_t node, InstructionCode opcode,
                ImmediateMode operand_mode) {
  FlagsContinuationT<Adapter> cont;
  VisitBinop<Adapter>(selector, node, opcode, operand_mode, &cont);
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStackSlot(node_t node) {
  StackSlotRepresentation rep = this->stack_slot_representation_of(node);
  int slot =
      frame_->AllocateSpillSlot(rep.size(), rep.alignment(), rep.is_tagged());
  OperandGenerator g(this);

  Emit(kArchStackSlot, g.DefineAsRegister(node),
       sequence()->AddImmediate(Constant(slot)), 0, nullptr);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitAbortCSADcheck(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    // This is currently not used by Turboshaft.
    UNIMPLEMENTED();
  } else {
    PPCOperandGeneratorT<Adapter> g(this);
    Emit(kArchAbortCSADcheck, g.NoOutput(), g.UseFixed(node->InputAt(0), r4));
  }
}

static void VisitLoadCommon(InstructionSelectorT<TurboshaftAdapter>* selector,
                            TurboshaftAdapter::node_t node,
                            LoadRepresentation load_rep) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  using node_t = TurboshaftAdapter::node_t;
  PPCOperandGeneratorT<TurboshaftAdapter> g(selector);
  auto load_view = selector->load_view(node);
  MemoryRepresentation loaded_rep = load_view.ts_loaded_rep();
  RegisterRepresentation result_rep = load_view.ts_result_rep();
  node_t base = load_view.base();
  node_t offset = load_view.index();

  InstructionCode opcode = kArchNop;
  ImmediateMode mode;
  if (CpuFeatures::IsSupported(PPC_10_PLUS)) {
    mode = kInt34Imm;
  } else {
    mode = kInt16Imm;
  }
  // NOTE: The meaning of `loaded_rep` = `MemoryRepresentation::AnyTagged()` is
  // we are loading a compressed tagged field, while `result_rep` =
  // `RegisterRepresentation::Tagged()` refers to an uncompressed tagged value.
  switch (loaded_rep) {
    case MemoryRepresentation::Int8():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word32());
      opcode = kPPC_LoadWordS8;
      break;
    case MemoryRepresentation::Uint8():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word32());
      opcode = kPPC_LoadWordU8;
      break;
    case MemoryRepresentation::Int16():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word32());
      opcode = kPPC_LoadWordS16;
      break;
    case MemoryRepresentation::Uint16():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word32());
      opcode = kPPC_LoadWordU16;
      break;
    case MemoryRepresentation::Int32():
    case MemoryRepresentation::Uint32():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word32());
      opcode = kPPC_LoadWordU32;
      break;
    case MemoryRepresentation::Int64():
    case MemoryRepresentation::Uint64():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word64());
      if (mode != kInt34Imm) mode = kInt16Imm_4ByteAligned;
      opcode = kPPC_LoadWord64;
      break;
    case MemoryRepresentation::Float16():
      UNIMPLEMENTED();
    case MemoryRepresentation::Float32():
      DCHECK_EQ(result_rep, RegisterRepresentation::Float32());
      opcode = kPPC_LoadFloat32;
      break;
    case MemoryRepresentation::Float64():
      DCHECK_EQ(result_rep, RegisterRepresentation::Float64());
      opcode = kPPC_LoadDouble;
      break;
#ifdef V8_COMPRESS_POINTERS
    case MemoryRepresentation::AnyTagged():
    case MemoryRepresentation::TaggedPointer():
      if (result_rep == RegisterRepresentation::Compressed()) {
        if (mode != kInt34Imm) mode = kInt16Imm_4ByteAligned;
        opcode = kPPC_LoadWordS32;
        break;
      }
      DCHECK_EQ(result_rep, RegisterRepresentation::Tagged());
      opcode = kPPC_LoadDecompressTagged;
      break;
    case MemoryRepresentation::TaggedSigned():
      if (result_rep == RegisterRepresentation::Compressed()) {
        if (mode != kInt34Imm) mode = kInt16Imm_4ByteAligned;
        opcode = kPPC_LoadWordS32;
        break;
      }
      DCHECK_EQ(result_rep, RegisterRepresentation::Tagged());
      opcode = kPPC_LoadDecompressTaggedSigned;
      break;
#else
      USE(result_rep);
    case MemoryRepresentation::AnyTagged():
    case MemoryRepresentation::TaggedPointer():
    case MemoryRepresentation::TaggedSigned():
      DCHECK_EQ(result_rep, RegisterRepresentation::Tagged());
      if (mode != kInt34Imm) mode = kInt16Imm_4ByteAligned;
      opcode = kPPC_LoadWord64;
      break;
#endif
    case MemoryRepresentation::AnyUncompressedTagged():
    case MemoryRepresentation::UncompressedTaggedPointer():
    case MemoryRepresentation::UncompressedTaggedSigned():
      DCHECK_EQ(result_rep, RegisterRepresentation::Tagged());
      if (mode != kInt34Imm) mode = kInt16Imm_4ByteAligned;
      opcode = kPPC_LoadWord64;
      break;
    case MemoryRepresentation::SandboxedPointer():
      opcode = kPPC_LoadDecodeSandboxedPointer;
      break;
    case MemoryRepresentation::Simd128():
      DCHECK_EQ(result_rep, RegisterRepresentation::Simd128());
      opcode = kPPC_LoadSimd128;
      // Vectors do not support MRI mode, only MRR is available.
      mode = kNoImmediate;
      break;
    case MemoryRepresentation::ProtectedPointer():
    case MemoryRepresentation::IndirectPointer():
    case MemoryRepresentation::Simd256():
      UNREACHABLE();
  }

  bool is_atomic = load_view.is_atomic();

  if (selector->is_load_root_register(base)) {
    selector->Emit(opcode |= AddressingModeField::encode(kMode_Root),
                   g.DefineAsRegister(node), g.UseRegister(offset),
                   g.UseImmediate(is_atomic));
  } else if (g.CanBeImmediate(offset, mode)) {
    selector->Emit(opcode | AddressingModeField::encode(kMode_MRI),
                   g.DefineAsRegister(node), g.UseRegister(base),
                   g.UseImmediate(offset), g.UseImmediate(is_atomic));
  } else if (g.CanBeImmediate(base, mode)) {
    selector->Emit(opcode | AddressingModeField::encode(kMode_MRI),
                   g.DefineAsRegister(node), g.UseRegister(offset),
                   g.UseImmediate(base), g.UseImmediate(is_atomic));
  } else {
    selector->Emit(opcode | AddressingModeField::encode(kMode_MRR),
                   g.DefineAsRegister(node), g.UseRegister(base),
                   g.UseRegister(offset), g.UseImmediate(is_atomic));
  }
}

static void VisitLoadCommon(InstructionSelectorT<TurbofanAdapter>* selector,
                            TurbofanAdapter::node_t node,
                            LoadRepresentation load_rep) {
  using node_t = TurbofanAdapter::node_t;
  PPCOperandGeneratorT<TurbofanAdapter> g(selector);
  auto load_view = selector->load_view(node);
  node_t base = load_view.base();
  node_t offset = load_view.index();

  InstructionCode opcode = kArchNop;
  ImmediateMode mode;
  if (CpuFeatures::IsSupported(PPC_10_PLUS)) {
    mode = kInt34Imm;
  } else {
    mode = kInt16Imm;
  }
  switch (load_rep.representation()) {
    case MachineRepresentation::kFloat32:
      opcode = kPPC_LoadFloat32;
      break;
    case MachineRepresentation::kFloat64:
      opcode = kPPC_LoadDouble;
      break;
    case MachineRepresentation::kBit:  // Fall through.
    case MachineRepresentation::kWord8:
      opcode = load_rep.IsSigned() ? kPPC_LoadWordS8 : kPPC_LoadWordU8;
      break;
    case MachineRepresentation::kWord16:
      opcode = load_rep.IsSigned() ? kPPC_LoadWordS16 : kPPC_LoadWordU16;
      break;
    case MachineRepresentation::kWord32:
      opcode = kPPC_LoadWordU32;
      break;
    case MachineRepresentation::kCompressedPointer:  // Fall through.
    case MachineRepresentation::kCompressed:
#ifdef V8_COMPRESS_POINTERS
      if (mode != kInt34Imm) mode = kInt16Imm_4ByteAligned;
      opcode = kPPC_LoadWordS32;
      break;
#else
      UNREACHABLE();
#endif
      case MachineRepresentation::kIndirectPointer:
        UNREACHABLE();
      case MachineRepresentation::kSandboxedPointer:
        opcode = kPPC_LoadDecodeSandboxedPointer;
        break;
#ifdef V8_COMPRESS_POINTERS
      case MachineRepresentation::kTaggedSigned:
        opcode = kPPC_LoadDecompressTaggedSigned;
        break;
      case MachineRepresentation::kTaggedPointer:
        opcode = kPPC_LoadDecompressTagged;
        break;
      case MachineRepresentation::kTagged:
        opcode = kPPC_LoadDecompressTagged;
        break;
#else
      case MachineRepresentation::kTaggedSigned:   // Fall through.
      case MachineRepresentation::kTaggedPointer:  // Fall through.
      case MachineRepresentation::kTagged:         // Fall through.
#endif
      case MachineRepresentation::kWord64:
        if (mode != kInt34Imm) mode = kInt16Imm_4ByteAligned;
        opcode = kPPC_LoadWord64;
        break;
      case MachineRepresentation::kSimd128:
        opcode = kPPC_LoadSimd128;
        // Vectors do not support MRI mode, only MRR is available.
        mode = kNoImmediate;
        break;
      case MachineRepresentation::kFloat16:
        UNIMPLEMENTED();
      case MachineRepresentation::kProtectedPointer:  // Fall through.
      case MachineRepresentation::kSimd256:  // Fall through.
      case MachineRepresentation::kMapWord:  // Fall through.
      case MachineRepresentation::kNone:
        UNREACHABLE();
  }

  bool is_atomic = load_view.is_atomic();

  if (selector->is_load_root_register(base)) {
    selector->Emit(opcode |= AddressingModeField::encode(kMode_Root),
                   g.DefineAsRegister(node), g.UseRegister(offset),
                   g.UseImmediate(is_atomic));
  } else if (g.CanBeImmediate(offset, mode)) {
    selector->Emit(opcode | AddressingModeField::encode(kMode_MRI),
                   g.DefineAsRegister(node), g.UseRegister(base),
                   g.UseImmediate(offset), g.UseImmediate(is_atomic));
  } else if (g.CanBeImmediate(base, mode)) {
    selector->Emit(opcode | AddressingModeField::encode(kMode_MRI),
                   g.DefineAsRegister(node), g.UseRegister(offset),
                   g.UseImmediate(base), g.UseImmediate(is_atomic));
  } else {
    selector->Emit(opcode | AddressingModeField::encode(kMode_MRR),
                   g.DefineAsRegister(node), g.UseRegister(base),
                   g.UseRegister(offset), g.UseImmediate(is_atomic));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoad(node_t node) {
  typename Adapter::LoadView load = this->load_view(node);
  LoadRepresentation load_rep = load.loaded_rep();
  VisitLoadCommon(this, node, load_rep);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitProtectedLoad(node_t node) {
  // TODO(eholk)
  UNIMPLEMENTED();
}

void VisitStoreCommon(InstructionSelectorT<TurboshaftAdapter>* selector,
                      TurboshaftAdapter::node_t node,
                      StoreRepresentation store_rep,
                      std::optional<AtomicMemoryOrder> atomic_order) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  using node_t = TurboshaftAdapter::node_t;
  PPCOperandGeneratorT<TurboshaftAdapter> g(selector);
  auto store_view = selector->store_view(node);
  node_t base = store_view.base();
  node_t offset = selector->value(store_view.index());
  node_t value = store_view.value();
  bool is_atomic = store_view.is_atomic();

  MachineRepresentation rep = store_rep.representation();
  WriteBarrierKind write_barrier_kind = kNoWriteBarrier;

  if (!is_atomic) {
    write_barrier_kind = store_rep.write_barrier_kind();
  }

  if (v8_flags.enable_unconditional_write_barriers &&
      CanBeTaggedOrCompressedOrIndirectPointer(rep)) {
    write_barrier_kind = kFullWriteBarrier;
  }

  if (write_barrier_kind != kNoWriteBarrier &&
      !v8_flags.disable_write_barriers) {
    DCHECK(CanBeTaggedOrCompressedOrIndirectPointer(rep));
    // Uncompressed stores should not happen if we need a write barrier.
    CHECK((store_view.ts_stored_rep() !=
           MemoryRepresentation::AnyUncompressedTagged()) &&
          (store_view.ts_stored_rep() !=
           MemoryRepresentation::UncompressedTaggedPointer()) &&
          (store_view.ts_stored_rep() !=
           MemoryRepresentation::UncompressedTaggedPointer()));
    AddressingMode addressing_mode;
    InstructionOperand inputs[4];
    size_t input_count = 0;
    inputs[input_count++] = g.UseUniqueRegister(base);
    // OutOfLineRecordWrite uses the offset in an 'add' instruction as well as
    // for the store itself, so we must check compatibility with both.
    if (g.CanBeImmediate(offset, kInt16Imm)
#if V8_TARGET_ARCH_PPC64
        && g.CanBeImmediate(offset, kInt16Imm_4ByteAligned)
#endif
    ) {
      inputs[input_count++] = g.UseImmediate(offset);
      addressing_mode = kMode_MRI;
    } else {
      inputs[input_count++] = g.UseUniqueRegister(offset);
      addressing_mode = kMode_MRR;
    }
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
      inputs[input_count++] = g.UseImmediate(static_cast<int64_t>(tag));
    } else {
      code = kArchStoreWithWriteBarrier;
    }
    code |= AddressingModeField::encode(addressing_mode);
    code |= RecordWriteModeField::encode(record_write_mode);
    CHECK_EQ(is_atomic, false);
    selector->Emit(code, 0, nullptr, input_count, inputs, temp_count, temps);
  } else {
    ArchOpcode opcode;
    ImmediateMode mode;
    if (CpuFeatures::IsSupported(PPC_10_PLUS)) {
      mode = kInt34Imm;
    } else {
      mode = kInt16Imm;
    }
    switch (store_view.ts_stored_rep()) {
      case MemoryRepresentation::Int8():
      case MemoryRepresentation::Uint8():
        opcode = kPPC_StoreWord8;
        break;
      case MemoryRepresentation::Int16():
      case MemoryRepresentation::Uint16():
        opcode = kPPC_StoreWord16;
        break;
      case MemoryRepresentation::Int32():
      case MemoryRepresentation::Uint32(): {
        opcode = kPPC_StoreWord32;
        const Operation& reverse_op = selector->Get(value);
        if (reverse_op.Is<Opmask::kWord32ReverseBytes>()) {
          opcode = kPPC_StoreByteRev32;
          value = selector->input_at(value, 0);
          mode = kNoImmediate;
        }
        break;
      }
      case MemoryRepresentation::Int64():
      case MemoryRepresentation::Uint64(): {
        if (mode != kInt34Imm) mode = kInt16Imm_4ByteAligned;
        opcode = kPPC_StoreWord64;
        const Operation& reverse_op = selector->Get(value);
        if (reverse_op.Is<Opmask::kWord64ReverseBytes>()) {
          opcode = kPPC_StoreByteRev64;
          value = selector->input_at(value, 0);
          mode = kNoImmediate;
        }
        break;
      }
      case MemoryRepresentation::Float16():
        UNIMPLEMENTED();
      case MemoryRepresentation::Float32():
        opcode = kPPC_StoreFloat32;
        break;
      case MemoryRepresentation::Float64():
        opcode = kPPC_StoreDouble;
        break;
      case MemoryRepresentation::AnyTagged():
      case MemoryRepresentation::TaggedPointer():
      case MemoryRepresentation::TaggedSigned():
        if (mode != kInt34Imm) mode = kInt16Imm;
        opcode = kPPC_StoreCompressTagged;
        break;
      case MemoryRepresentation::AnyUncompressedTagged():
      case MemoryRepresentation::UncompressedTaggedPointer():
      case MemoryRepresentation::UncompressedTaggedSigned():
        if (mode != kInt34Imm) mode = kInt16Imm_4ByteAligned;
        opcode = kPPC_StoreWord64;
        break;
      case MemoryRepresentation::ProtectedPointer():
        // We never store directly to protected pointers from generated code.
        UNREACHABLE();
      case MemoryRepresentation::IndirectPointer():
        if (mode != kInt34Imm) mode = kInt16Imm_4ByteAligned;
        opcode = kPPC_StoreIndirectPointer;
        break;
      case MemoryRepresentation::SandboxedPointer():
        if (mode != kInt34Imm) mode = kInt16Imm_4ByteAligned;
        opcode = kPPC_StoreEncodeSandboxedPointer;
        break;
      case MemoryRepresentation::Simd128():
        opcode = kPPC_StoreSimd128;
        // Vectors do not support MRI mode, only MRR is available.
        mode = kNoImmediate;
        break;
      case MemoryRepresentation::Simd256():
        UNREACHABLE();
    }

    if (selector->is_load_root_register(base)) {
      selector->Emit(opcode | AddressingModeField::encode(kMode_Root),
                     g.NoOutput(), g.UseRegister(offset), g.UseRegister(value),
                     g.UseImmediate(is_atomic));
    } else if (g.CanBeImmediate(offset, mode)) {
      selector->Emit(opcode | AddressingModeField::encode(kMode_MRI),
                     g.NoOutput(), g.UseRegister(base), g.UseImmediate(offset),
                     g.UseRegister(value), g.UseImmediate(is_atomic));
    } else if (g.CanBeImmediate(base, mode)) {
      selector->Emit(opcode | AddressingModeField::encode(kMode_MRI),
                     g.NoOutput(), g.UseRegister(offset), g.UseImmediate(base),
                     g.UseRegister(value), g.UseImmediate(is_atomic));
    } else {
      selector->Emit(opcode | AddressingModeField::encode(kMode_MRR),
                     g.NoOutput(), g.UseRegister(base), g.UseRegister(offset),
                     g.UseRegister(value), g.UseImmediate(is_atomic));
    }
  }
}

void VisitStoreCommon(InstructionSelectorT<TurbofanAdapter>* selector,
                      TurbofanAdapter::node_t node,
                      StoreRepresentation store_rep,
                      std::optional<AtomicMemoryOrder> atomic_order) {
  using node_t = TurbofanAdapter::node_t;
  PPCOperandGeneratorT<TurbofanAdapter> g(selector);
  auto store_view = selector->store_view(node);
  node_t base = store_view.base();
  node_t offset = selector->value(store_view.index());
  node_t value = store_view.value();
  bool is_atomic = store_view.is_atomic();

  MachineRepresentation rep = store_rep.representation();
  WriteBarrierKind write_barrier_kind = kNoWriteBarrier;

  if (!is_atomic) {
    write_barrier_kind = store_rep.write_barrier_kind();
  }

  if (v8_flags.enable_unconditional_write_barriers &&
      CanBeTaggedOrCompressedOrIndirectPointer(rep)) {
    write_barrier_kind = kFullWriteBarrier;
  }

  if (write_barrier_kind != kNoWriteBarrier &&
      !v8_flags.disable_write_barriers) {
    DCHECK(CanBeTaggedOrCompressedOrIndirectPointer(rep));
    AddressingMode addressing_mode;
    InstructionOperand inputs[4];
    size_t input_count = 0;
    inputs[input_count++] = g.UseUniqueRegister(base);
    // OutOfLineRecordWrite uses the offset in an 'add' instruction as well as
    // for the store itself, so we must check compatibility with both.
    if (g.CanBeImmediate(offset, kInt16Imm)
#if V8_TARGET_ARCH_PPC64
        && g.CanBeImmediate(offset, kInt16Imm_4ByteAligned)
#endif
            ) {
      inputs[input_count++] = g.UseImmediate(offset);
      addressing_mode = kMode_MRI;
    } else {
      inputs[input_count++] = g.UseUniqueRegister(offset);
      addressing_mode = kMode_MRR;
    }
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
      inputs[input_count++] = g.UseImmediate(static_cast<int64_t>(tag));
    } else {
      code = kArchStoreWithWriteBarrier;
    }
    code |= AddressingModeField::encode(addressing_mode);
    code |= RecordWriteModeField::encode(record_write_mode);
    CHECK_EQ(is_atomic, false);
    selector->Emit(code, 0, nullptr, input_count, inputs, temp_count, temps);
  } else {
    ArchOpcode opcode;
    ImmediateMode mode;
    if (CpuFeatures::IsSupported(PPC_10_PLUS)) {
      mode = kInt34Imm;
    } else {
      mode = kInt16Imm;
    }
    switch (rep) {
      case MachineRepresentation::kFloat32:
        opcode = kPPC_StoreFloat32;
        break;
      case MachineRepresentation::kFloat64:
        opcode = kPPC_StoreDouble;
        break;
      case MachineRepresentation::kBit:  // Fall through.
      case MachineRepresentation::kWord8:
        opcode = kPPC_StoreWord8;
        break;
      case MachineRepresentation::kWord16:
        opcode = kPPC_StoreWord16;
        break;
      case MachineRepresentation::kWord32: {
        opcode = kPPC_StoreWord32;
          NodeMatcher m(value);
          if (m.IsWord32ReverseBytes()) {
            opcode = kPPC_StoreByteRev32;
            value = selector->input_at(value, 0);
            mode = kNoImmediate;
          }
        break;
      }
      case MachineRepresentation::kCompressedPointer:  // Fall through.
      case MachineRepresentation::kCompressed:
#ifdef V8_COMPRESS_POINTERS
        if (mode != kInt34Imm) mode = kInt16Imm;
        opcode = kPPC_StoreCompressTagged;
#else
        UNREACHABLE();
#endif
        break;
      case MachineRepresentation::kIndirectPointer:
        if (mode != kInt34Imm) mode = kInt16Imm_4ByteAligned;
        opcode = kPPC_StoreIndirectPointer;
        break;
      case MachineRepresentation::kSandboxedPointer:
        if (mode != kInt34Imm) mode = kInt16Imm_4ByteAligned;
        opcode = kPPC_StoreEncodeSandboxedPointer;
        break;
      case MachineRepresentation::kTaggedSigned:   // Fall through.
      case MachineRepresentation::kTaggedPointer:  // Fall through.
      case MachineRepresentation::kTagged:
        if (mode != kInt34Imm) mode = kInt16Imm_4ByteAligned;
        opcode = kPPC_StoreCompressTagged;
        break;
      case MachineRepresentation::kWord64: {
        opcode = kPPC_StoreWord64;
        if (mode != kInt34Imm) {
          mode = kInt16Imm_4ByteAligned;
        }
        NodeMatcher m(value);
        if (m.IsWord64ReverseBytes()) {
          opcode = kPPC_StoreByteRev64;
          value = selector->input_at(value, 0);
          mode = kNoImmediate;
        }
        break;
      }
      case MachineRepresentation::kSimd128:
        opcode = kPPC_StoreSimd128;
        // Vectors do not support MRI mode, only MRR is available.
        mode = kNoImmediate;
        break;
      case MachineRepresentation::kFloat16:
        UNIMPLEMENTED();
      case MachineRepresentation::kProtectedPointer:  // Fall through.
      case MachineRepresentation::kSimd256:  // Fall through.
      case MachineRepresentation::kMapWord:  // Fall through.
      case MachineRepresentation::kNone:
        UNREACHABLE();
    }

    if (selector->is_load_root_register(base)) {
      selector->Emit(opcode | AddressingModeField::encode(kMode_Root),
                     g.NoOutput(), g.UseRegister(offset), g.UseRegister(value),
                     g.UseImmediate(is_atomic));
    } else if (g.CanBeImmediate(offset, mode)) {
      selector->Emit(opcode | AddressingModeField::encode(kMode_MRI),
                     g.NoOutput(), g.UseRegister(base), g.UseImmediate(offset),
                     g.UseRegister(value), g.UseImmediate(is_atomic));
    } else if (g.CanBeImmediate(base, mode)) {
      selector->Emit(opcode | AddressingModeField::encode(kMode_MRI),
                     g.NoOutput(), g.UseRegister(offset), g.UseImmediate(base),
                     g.UseRegister(value), g.UseImmediate(is_atomic));
    } else {
      selector->Emit(opcode | AddressingModeField::encode(kMode_MRR),
                     g.NoOutput(), g.UseRegister(base), g.UseRegister(offset),
                     g.UseRegister(value), g.UseImmediate(is_atomic));
    }
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStorePair(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStore(node_t node) {
  VisitStoreCommon(this, node, this->store_view(node).stored_rep(),
                   std::nullopt);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitProtectedStore(node_t node) {
  // TODO(eholk)
  UNIMPLEMENTED();
}

// Architecture supports unaligned access, therefore VisitLoad is used instead
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUnalignedLoad(node_t node) {
  UNREACHABLE();
}

// Architecture supports unaligned access, therefore VisitStore is used instead
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUnalignedStore(node_t node) {
  UNREACHABLE();
}

static void VisitLogical(InstructionSelectorT<TurboshaftAdapter>* selector,
                         turboshaft::OpIndex node, ArchOpcode opcode,
                         bool left_can_cover, bool right_can_cover,
                         ImmediateMode imm_mode) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  PPCOperandGeneratorT<TurboshaftAdapter> g(selector);
  const WordBinopOp& logical_op = selector->Get(node).Cast<WordBinopOp>();
  const Operation& lhs = selector->Get(logical_op.left());
  const Operation& rhs = selector->Get(logical_op.right());

  // Map instruction to equivalent operation with inverted right input.
  ArchOpcode inv_opcode = opcode;
  switch (opcode) {
    case kPPC_And:
      inv_opcode = kPPC_AndComplement;
      break;
    case kPPC_Or:
      inv_opcode = kPPC_OrComplement;
      break;
    default:
      UNREACHABLE();
  }

  // Select Logical(y, ~x) for Logical(Xor(x, -1), y).
  if (lhs.Is<Opmask::kBitwiseXor>() && left_can_cover) {
    const WordBinopOp& xor_op = lhs.Cast<WordBinopOp>();
    int64_t xor_rhs_val;
    if (selector->MatchSignedIntegralConstant(xor_op.right(), &xor_rhs_val) &&
        xor_rhs_val == -1) {
      // TODO(all): support shifted operand on right.
      selector->Emit(inv_opcode, g.DefineAsRegister(node),
                     g.UseRegister(logical_op.right()),
                     g.UseRegister(xor_op.left()));
      return;
    }
  }

  // Select Logical(x, ~y) for Logical(x, Xor(y, -1)).
  if (rhs.Is<Opmask::kBitwiseXor>() && right_can_cover) {
    const WordBinopOp& xor_op = rhs.Cast<WordBinopOp>();
    int64_t xor_rhs_val;
    if (selector->MatchSignedIntegralConstant(xor_op.right(), &xor_rhs_val) &&
        xor_rhs_val == -1) {
      // TODO(all): support shifted operand on right.
      selector->Emit(inv_opcode, g.DefineAsRegister(node),
                     g.UseRegister(logical_op.left()),
                     g.UseRegister(xor_op.left()));
      return;
    }
  }

  VisitBinop<TurboshaftAdapter>(selector, node, opcode, imm_mode);
}

template <typename Adapter, typename Matcher>
static void VisitLogical(InstructionSelectorT<Adapter>* selector, Node* node,
                         Matcher* m, ArchOpcode opcode, bool left_can_cover,
                         bool right_can_cover, ImmediateMode imm_mode) {
  PPCOperandGeneratorT<Adapter> g(selector);

  // Map instruction to equivalent operation with inverted right input.
  ArchOpcode inv_opcode = opcode;
  switch (opcode) {
    case kPPC_And:
      inv_opcode = kPPC_AndComplement;
      break;
    case kPPC_Or:
      inv_opcode = kPPC_OrComplement;
      break;
    default:
      UNREACHABLE();
  }

  // Select Logical(y, ~x) for Logical(Xor(x, -1), y).
  if ((m->left().IsWord32Xor() || m->left().IsWord64Xor()) && left_can_cover) {
    Matcher mleft(m->left().node());
    if (mleft.right().Is(-1)) {
      selector->Emit(inv_opcode, g.DefineAsRegister(node),
                     g.UseRegister(m->right().node()),
                     g.UseRegister(mleft.left().node()));
      return;
    }
  }

  // Select Logical(x, ~y) for Logical(x, Xor(y, -1)).
  if ((m->right().IsWord32Xor() || m->right().IsWord64Xor()) &&
      right_can_cover) {
    Matcher mright(m->right().node());
    if (mright.right().Is(-1)) {
      // TODO(all): support shifted operand on right.
      selector->Emit(inv_opcode, g.DefineAsRegister(node),
                     g.UseRegister(m->left().node()),
                     g.UseRegister(mright.left().node()));
      return;
    }
  }

  VisitBinop<Adapter>(selector, node, opcode, imm_mode);
}

static inline bool IsContiguousMask32(uint32_t value, int* mb, int* me) {
  int mask_width = base::bits::CountPopulation(value);
  int mask_msb = base::bits::CountLeadingZeros32(value);
  int mask_lsb = base::bits::CountTrailingZeros32(value);
  if ((mask_width == 0) || (mask_msb + mask_width + mask_lsb != 32))
    return false;
  *mb = mask_lsb + mask_width - 1;
  *me = mask_lsb;
  return true;
}

#if V8_TARGET_ARCH_PPC64
static inline bool IsContiguousMask64(uint64_t value, int* mb, int* me) {
  int mask_width = base::bits::CountPopulation(value);
  int mask_msb = base::bits::CountLeadingZeros64(value);
  int mask_lsb = base::bits::CountTrailingZeros64(value);
  if ((mask_width == 0) || (mask_msb + mask_width + mask_lsb != 64))
    return false;
  *mb = mask_lsb + mask_width - 1;
  *me = mask_lsb;
  return true;
}
#endif

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord32And(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  PPCOperandGeneratorT<TurboshaftAdapter> g(this);

  const WordBinopOp& bitwise_and = Get(node).Cast<WordBinopOp>();
  int mb = 0;
  int me = 0;
  if (is_integer_constant(bitwise_and.right()) &&
      IsContiguousMask32(integer_constant(bitwise_and.right()), &mb, &me)) {
    int sh = 0;
    node_t left = bitwise_and.left();
    const Operation& lhs = Get(left);
    if ((lhs.Is<Opmask::kWord32ShiftRightLogical>() ||
         lhs.Is<Opmask::kWord32ShiftLeft>()) &&
        CanCover(node, left)) {
      // Try to absorb left/right shift into rlwinm
      int32_t shift_by;
      const ShiftOp& shift_op = lhs.Cast<ShiftOp>();
      if (MatchIntegralWord32Constant(shift_op.right(), &shift_by) &&
          base::IsInRange(shift_by, 0, 31)) {
        left = shift_op.left();
        sh = integer_constant(shift_op.right());
        if (lhs.Is<Opmask::kWord32ShiftRightLogical>()) {
          // Adjust the mask such that it doesn't include any rotated bits.
          if (mb > 31 - sh) mb = 31 - sh;
          sh = (32 - sh) & 0x1F;
        } else {
          // Adjust the mask such that it doesn't include any rotated bits.
          if (me < sh) me = sh;
        }
      }
    }
    if (mb >= me) {
      Emit(kPPC_RotLeftAndMask32, g.DefineAsRegister(node), g.UseRegister(left),
           g.TempImmediate(sh), g.TempImmediate(mb), g.TempImmediate(me));
      return;
    }
  }
  VisitLogical(this, node, kPPC_And, CanCover(node, bitwise_and.left()),
               CanCover(node, bitwise_and.right()), kInt16Imm_Unsigned);
}

// TODO(mbrandy): Absorb rotate-right into rlwinm?
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32And(node_t node) {
  PPCOperandGeneratorT<Adapter> g(this);
  Int32BinopMatcher m(node);
  int mb = 0;
  int me = 0;
  if (m.right().HasResolvedValue() &&
      IsContiguousMask32(m.right().ResolvedValue(), &mb, &me)) {
    int sh = 0;
    Node* left = m.left().node();
    if ((m.left().IsWord32Shr() || m.left().IsWord32Shl()) &&
        CanCover(node, left)) {
      // Try to absorb left/right shift into rlwinm
      Int32BinopMatcher mleft(m.left().node());
      if (mleft.right().IsInRange(0, 31)) {
        left = mleft.left().node();
        sh = mleft.right().ResolvedValue();
        if (m.left().IsWord32Shr()) {
          // Adjust the mask such that it doesn't include any rotated bits.
          if (mb > 31 - sh) mb = 31 - sh;
          sh = (32 - sh) & 0x1F;
        } else {
          // Adjust the mask such that it doesn't include any rotated bits.
          if (me < sh) me = sh;
        }
      }
    }
    if (mb >= me) {
      Emit(kPPC_RotLeftAndMask32, g.DefineAsRegister(node), g.UseRegister(left),
           g.TempImmediate(sh), g.TempImmediate(mb), g.TempImmediate(me));
      return;
    }
  }
    VisitLogical<Adapter, Int32BinopMatcher>(
        this, node, &m, kPPC_And, CanCover(node, m.left().node()),
        CanCover(node, m.right().node()), kInt16Imm_Unsigned);
}

#if V8_TARGET_ARCH_PPC64

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord64And(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  PPCOperandGeneratorT<TurboshaftAdapter> g(this);

  const WordBinopOp& bitwise_and = Get(node).Cast<WordBinopOp>();
  int mb = 0;
  int me = 0;
  if (is_integer_constant(bitwise_and.right()) &&
      IsContiguousMask64(integer_constant(bitwise_and.right()), &mb, &me)) {
    int sh = 0;
    node_t left = bitwise_and.left();
    const Operation& lhs = Get(left);
    if ((lhs.Is<Opmask::kWord64ShiftRightLogical>() ||
         lhs.Is<Opmask::kWord64ShiftLeft>()) &&
        CanCover(node, left)) {
      // Try to absorb left/right shift into rldic
      int64_t shift_by;
      const ShiftOp& shift_op = lhs.Cast<ShiftOp>();
      if (MatchIntegralWord64Constant(shift_op.right(), &shift_by) &&
          base::IsInRange(shift_by, 0, 63)) {
        left = shift_op.left();
        sh = integer_constant(shift_op.right());
        if (lhs.Is<Opmask::kWord64ShiftRightLogical>()) {
          // Adjust the mask such that it doesn't include any rotated bits.
          if (mb > 63 - sh) mb = 63 - sh;
          sh = (64 - sh) & 0x3F;
        } else {
          // Adjust the mask such that it doesn't include any rotated bits.
          if (me < sh) me = sh;
        }
      }
    }
    if (mb >= me) {
      bool match = false;
      ArchOpcode opcode;
      int mask;
      if (me == 0) {
        match = true;
        opcode = kPPC_RotLeftAndClearLeft64;
        mask = mb;
      } else if (mb == 63) {
        match = true;
        opcode = kPPC_RotLeftAndClearRight64;
        mask = me;
      } else if (sh && me <= sh && lhs.Is<Opmask::kWord64ShiftLeft>()) {
        match = true;
        opcode = kPPC_RotLeftAndClear64;
        mask = mb;
      }
      if (match) {
        Emit(opcode, g.DefineAsRegister(node), g.UseRegister(left),
             g.TempImmediate(sh), g.TempImmediate(mask));
        return;
      }
    }
  }
  VisitLogical(this, node, kPPC_And, CanCover(node, bitwise_and.left()),
               CanCover(node, bitwise_and.right()), kInt16Imm_Unsigned);
}

// TODO(mbrandy): Absorb rotate-right into rldic?
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64And(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    Int64BinopMatcher m(node);
    int mb = 0;
    int me = 0;
    if (m.right().HasResolvedValue() &&
        IsContiguousMask64(m.right().ResolvedValue(), &mb, &me)) {
      int sh = 0;
      Node* left = m.left().node();
      if ((m.left().IsWord64Shr() || m.left().IsWord64Shl()) &&
          CanCover(node, left)) {
        // Try to absorb left/right shift into rldic
        Int64BinopMatcher mleft(m.left().node());
        if (mleft.right().IsInRange(0, 63)) {
          left = mleft.left().node();
          sh = mleft.right().ResolvedValue();
          if (m.left().IsWord64Shr()) {
            // Adjust the mask such that it doesn't include any rotated bits.
            if (mb > 63 - sh) mb = 63 - sh;
            sh = (64 - sh) & 0x3F;
          } else {
            // Adjust the mask such that it doesn't include any rotated bits.
            if (me < sh) me = sh;
          }
        }
      }
      if (mb >= me) {
        bool match = false;
        ArchOpcode opcode;
        int mask;
        if (me == 0) {
          match = true;
          opcode = kPPC_RotLeftAndClearLeft64;
          mask = mb;
        } else if (mb == 63) {
          match = true;
          opcode = kPPC_RotLeftAndClearRight64;
          mask = me;
        } else if (sh && me <= sh && m.left().IsWord64Shl()) {
          match = true;
          opcode = kPPC_RotLeftAndClear64;
          mask = mb;
        }
        if (match) {
          Emit(opcode, g.DefineAsRegister(node), g.UseRegister(left),
               g.TempImmediate(sh), g.TempImmediate(mask));
          return;
        }
      }
    }
    VisitLogical<Adapter, Int64BinopMatcher>(
        this, node, &m, kPPC_And, CanCover(node, m.left().node()),
        CanCover(node, m.right().node()), kInt16Imm_Unsigned);
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Or(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const WordBinopOp& op = this->Get(node).template Cast<WordBinopOp>();
    VisitLogical(this, node, kPPC_Or, CanCover(node, op.left()),
                 CanCover(node, op.right()), kInt16Imm_Unsigned);
  } else {
    Int32BinopMatcher m(node);
    VisitLogical<Adapter, Int32BinopMatcher>(
        this, node, &m, kPPC_Or, CanCover(node, m.left().node()),
        CanCover(node, m.right().node()), kInt16Imm_Unsigned);
  }
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Or(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const WordBinopOp& op = this->Get(node).template Cast<WordBinopOp>();
    VisitLogical(this, node, kPPC_Or, CanCover(node, op.left()),
                 CanCover(node, op.right()), kInt16Imm_Unsigned);
  } else {
    Int64BinopMatcher m(node);
    VisitLogical<Adapter, Int64BinopMatcher>(
        this, node, &m, kPPC_Or, CanCover(node, m.left().node()),
        CanCover(node, m.right().node()), kInt16Imm_Unsigned);
  }
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Xor(node_t node) {
  PPCOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const WordBinopOp& bitwise_xor =
        this->Get(node).template Cast<WordBinopOp>();
    int32_t mask;
    if (this->MatchIntegralWord32Constant(bitwise_xor.right(), &mask) &&
        mask == -1) {
      Emit(kPPC_Not, g.DefineAsRegister(node),
           g.UseRegister(bitwise_xor.left()));
    } else {
      VisitBinop<Adapter>(this, node, kPPC_Xor, kInt16Imm_Unsigned);
    }
  } else {
    Int32BinopMatcher m(node);
    if (m.right().Is(-1)) {
      Emit(kPPC_Not, g.DefineAsRegister(node), g.UseRegister(m.left().node()));
    } else {
      VisitBinop<Adapter>(this, node, kPPC_Xor, kInt16Imm_Unsigned);
    }
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStackPointerGreaterThan(
    node_t node, FlagsContinuation* cont) {
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
    value = node->InputAt(0);
  }
  InstructionCode opcode =
      kArchStackPointerGreaterThan | MiscField::encode(static_cast<int>(kind));

  PPCOperandGeneratorT<Adapter> g(this);

  // No outputs.
  InstructionOperand* const outputs = nullptr;
  const int output_count = 0;

  // Applying an offset to this stack check requires a temp register. Offsets
  // are only applied to the first stack check. If applying an offset, we must
  // ensure the input and temp registers do not alias, thus kUniqueRegister.
  InstructionOperand temps[] = {g.TempRegister()};
  const int temp_count = (kind == StackCheckKind::kJSFunctionEntry) ? 1 : 0;
  const auto register_mode = (kind == StackCheckKind::kJSFunctionEntry)
                                 ? OperandGenerator::kUniqueRegister
                                 : OperandGenerator::kRegister;

  InstructionOperand inputs[] = {g.UseRegisterWithMode(value, register_mode)};
  static constexpr int input_count = arraysize(inputs);

  EmitWithContinuation(opcode, output_count, outputs, input_count, inputs,
                       temp_count, temps, cont);
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Xor(node_t node) {
  PPCOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const WordBinopOp& bitwise_xor =
        this->Get(node).template Cast<WordBinopOp>();
    int64_t mask;
    if (this->MatchIntegralWord64Constant(bitwise_xor.right(), &mask) &&
        mask == -1) {
      Emit(kPPC_Not, g.DefineAsRegister(node),
           g.UseRegister(bitwise_xor.left()));
    } else {
      VisitBinop<Adapter>(this, node, kPPC_Xor, kInt16Imm_Unsigned);
    }
  } else {
    Int64BinopMatcher m(node);
    if (m.right().Is(-1)) {
      Emit(kPPC_Not, g.DefineAsRegister(node), g.UseRegister(m.left().node()));
    } else {
      VisitBinop<Adapter>(this, node, kPPC_Xor, kInt16Imm_Unsigned);
    }
  }
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Shl(node_t node) {
  PPCOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ShiftOp& shl = this->Get(node).template Cast<ShiftOp>();
    const Operation& lhs = this->Get(shl.left());
    if (lhs.Is<Opmask::kWord32BitwiseAnd>() &&
        this->is_integer_constant(shl.right()) &&
        base::IsInRange(this->integer_constant(shl.right()), 0, 31)) {
      int sh = this->integer_constant(shl.right());
      int mb;
      int me;
      const WordBinopOp& bitwise_and = lhs.Cast<WordBinopOp>();
      if (this->is_integer_constant(bitwise_and.right()) &&
          IsContiguousMask32(this->integer_constant(bitwise_and.right()) << sh,
                             &mb, &me)) {
        // Adjust the mask such that it doesn't include any rotated bits.
        if (me < sh) me = sh;
        if (mb >= me) {
          Emit(kPPC_RotLeftAndMask32, g.DefineAsRegister(node),
               g.UseRegister(bitwise_and.left()), g.TempImmediate(sh),
               g.TempImmediate(mb), g.TempImmediate(me));
          return;
        }
      }
    }
    VisitRRO(this, kPPC_ShiftLeft32, node, kShift32Imm);
  } else {
    Int32BinopMatcher m(node);
    if (m.left().IsWord32And() && m.right().IsInRange(0, 31)) {
      // Try to absorb logical-and into rlwinm
      Int32BinopMatcher mleft(m.left().node());
      int sh = m.right().ResolvedValue();
      int mb;
      int me;
      if (mleft.right().HasResolvedValue() &&
          IsContiguousMask32(mleft.right().ResolvedValue() << sh, &mb, &me)) {
        // Adjust the mask such that it doesn't include any rotated bits.
        if (me < sh) me = sh;
        if (mb >= me) {
          Emit(kPPC_RotLeftAndMask32, g.DefineAsRegister(node),
               g.UseRegister(mleft.left().node()), g.TempImmediate(sh),
               g.TempImmediate(mb), g.TempImmediate(me));
          return;
        }
      }
    }
    VisitRRO(this, kPPC_ShiftLeft32, node, kShift32Imm);
  }
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Shl(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    if constexpr (Adapter::IsTurboshaft) {
      using namespace turboshaft;  // NOLINT(build/namespaces)
      const ShiftOp& shl = this->Get(node).template Cast<ShiftOp>();
      const Operation& lhs = this->Get(shl.left());
      if (lhs.Is<Opmask::kWord64BitwiseAnd>() &&
          this->is_integer_constant(shl.right()) &&
          base::IsInRange(this->integer_constant(shl.right()), 0, 63)) {
        int sh = this->integer_constant(shl.right());
        int mb;
        int me;
        const WordBinopOp& bitwise_and = lhs.Cast<WordBinopOp>();
        if (this->is_integer_constant(bitwise_and.right()) &&
            IsContiguousMask64(
                this->integer_constant(bitwise_and.right()) << sh, &mb, &me)) {
          // Adjust the mask such that it doesn't include any rotated bits.
          if (me < sh) me = sh;
          if (mb >= me) {
            bool match = false;
            ArchOpcode opcode;
            int mask;
            if (me == 0) {
              match = true;
              opcode = kPPC_RotLeftAndClearLeft64;
              mask = mb;
            } else if (mb == 63) {
              match = true;
              opcode = kPPC_RotLeftAndClearRight64;
              mask = me;
            } else if (sh && me <= sh) {
              match = true;
              opcode = kPPC_RotLeftAndClear64;
              mask = mb;
            }
            if (match) {
              Emit(opcode, g.DefineAsRegister(node),
                   g.UseRegister(bitwise_and.left()), g.TempImmediate(sh),
                   g.TempImmediate(mask));
              return;
            }
          }
        }
      }
      VisitRRO(this, kPPC_ShiftLeft64, node, kShift64Imm);
    } else {
      Int64BinopMatcher m(node);
      // TODO(mbrandy): eliminate left sign extension if right >= 32
      if (m.left().IsWord64And() && m.right().IsInRange(0, 63)) {
        // Try to absorb logical-and into rldic
        Int64BinopMatcher mleft(m.left().node());
        int sh = m.right().ResolvedValue();
        int mb;
        int me;
        if (mleft.right().HasResolvedValue() &&
            IsContiguousMask64(mleft.right().ResolvedValue() << sh, &mb, &me)) {
          // Adjust the mask such that it doesn't include any rotated bits.
          if (me < sh) me = sh;
          if (mb >= me) {
            bool match = false;
            ArchOpcode opcode;
            int mask;
            if (me == 0) {
              match = true;
              opcode = kPPC_RotLeftAndClearLeft64;
              mask = mb;
            } else if (mb == 63) {
              match = true;
              opcode = kPPC_RotLeftAndClearRight64;
              mask = me;
            } else if (sh && me <= sh) {
              match = true;
              opcode = kPPC_RotLeftAndClear64;
              mask = mb;
            }
            if (match) {
              Emit(opcode, g.DefineAsRegister(node),
                   g.UseRegister(mleft.left().node()), g.TempImmediate(sh),
                   g.TempImmediate(mask));
              return;
            }
          }
        }
      }
      VisitRRO(this, kPPC_ShiftLeft64, node, kShift64Imm);
    }
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Shr(node_t node) {
  PPCOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ShiftOp& shr = this->Get(node).template Cast<ShiftOp>();
    const Operation& lhs = this->Get(shr.left());
    if (lhs.Is<Opmask::kWord32BitwiseAnd>() &&
        this->is_integer_constant(shr.right()) &&
        base::IsInRange(this->integer_constant(shr.right()), 0, 31)) {
      int sh = this->integer_constant(shr.right());
      int mb;
      int me;
      const WordBinopOp& bitwise_and = lhs.Cast<WordBinopOp>();
      if (this->is_integer_constant(bitwise_and.right()) &&
          IsContiguousMask32(
              static_cast<uint32_t>(
                  this->integer_constant(bitwise_and.right()) >> sh),
              &mb, &me)) {
        // Adjust the mask such that it doesn't include any rotated bits.
        if (mb > 31 - sh) mb = 31 - sh;
        sh = (32 - sh) & 0x1F;
        if (mb >= me) {
          Emit(kPPC_RotLeftAndMask32, g.DefineAsRegister(node),
               g.UseRegister(bitwise_and.left()), g.TempImmediate(sh),
               g.TempImmediate(mb), g.TempImmediate(me));
          return;
        }
      }
    }
    VisitRRO(this, kPPC_ShiftRight32, node, kShift32Imm);
  } else {
    Int32BinopMatcher m(node);
    if (m.left().IsWord32And() && m.right().IsInRange(0, 31)) {
      // Try to absorb logical-and into rlwinm
      Int32BinopMatcher mleft(m.left().node());
      int sh = m.right().ResolvedValue();
      int mb;
      int me;
      if (mleft.right().HasResolvedValue() &&
          IsContiguousMask32((uint32_t)(mleft.right().ResolvedValue()) >> sh,
                             &mb, &me)) {
        // Adjust the mask such that it doesn't include any rotated bits.
        if (mb > 31 - sh) mb = 31 - sh;
        sh = (32 - sh) & 0x1F;
        if (mb >= me) {
          Emit(kPPC_RotLeftAndMask32, g.DefineAsRegister(node),
               g.UseRegister(mleft.left().node()), g.TempImmediate(sh),
               g.TempImmediate(mb), g.TempImmediate(me));
          return;
        }
      }
    }
    VisitRRO(this, kPPC_ShiftRight32, node, kShift32Imm);
  }
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Shr(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    if constexpr (Adapter::IsTurboshaft) {
      using namespace turboshaft;  // NOLINT(build/namespaces)
      const ShiftOp& shr = this->Get(node).template Cast<ShiftOp>();
      const Operation& lhs = this->Get(shr.left());
      if (lhs.Is<Opmask::kWord64BitwiseAnd>() &&
          this->is_integer_constant(shr.right()) &&
          base::IsInRange(this->integer_constant(shr.right()), 0, 63)) {
        int sh = this->integer_constant(shr.right());
        int mb;
        int me;
        const WordBinopOp& bitwise_and = lhs.Cast<WordBinopOp>();
        if (this->is_integer_constant(bitwise_and.right()) &&
            IsContiguousMask64(
                static_cast<uint64_t>(
                    this->integer_constant(bitwise_and.right()) >> sh),
                &mb, &me)) {
          // Adjust the mask such that it doesn't include any rotated bits.
          if (mb > 63 - sh) mb = 63 - sh;
          sh = (64 - sh) & 0x3F;
          if (mb >= me) {
            bool match = false;
            ArchOpcode opcode;
            int mask;
            if (me == 0) {
              match = true;
              opcode = kPPC_RotLeftAndClearLeft64;
              mask = mb;
            } else if (mb == 63) {
              match = true;
              opcode = kPPC_RotLeftAndClearRight64;
              mask = me;
            }
            if (match) {
              Emit(opcode, g.DefineAsRegister(node),
                   g.UseRegister(bitwise_and.left()), g.TempImmediate(sh),
                   g.TempImmediate(mask));
              return;
            }
          }
        }
      }
      VisitRRO(this, kPPC_ShiftRight64, node, kShift64Imm);
    } else {
      Int64BinopMatcher m(node);
      if (m.left().IsWord64And() && m.right().IsInRange(0, 63)) {
        // Try to absorb logical-and into rldic
        Int64BinopMatcher mleft(m.left().node());
        int sh = m.right().ResolvedValue();
        int mb;
        int me;
        if (mleft.right().HasResolvedValue() &&
            IsContiguousMask64((uint64_t)(mleft.right().ResolvedValue()) >> sh,
                               &mb, &me)) {
          // Adjust the mask such that it doesn't include any rotated bits.
          if (mb > 63 - sh) mb = 63 - sh;
          sh = (64 - sh) & 0x3F;
          if (mb >= me) {
            bool match = false;
            ArchOpcode opcode;
            int mask;
            if (me == 0) {
              match = true;
              opcode = kPPC_RotLeftAndClearLeft64;
              mask = mb;
            } else if (mb == 63) {
              match = true;
              opcode = kPPC_RotLeftAndClearRight64;
              mask = me;
            }
            if (match) {
              Emit(opcode, g.DefineAsRegister(node),
                   g.UseRegister(mleft.left().node()), g.TempImmediate(sh),
                   g.TempImmediate(mask));
              return;
            }
          }
        }
      }
      VisitRRO(this, kPPC_ShiftRight64, node, kShift64Imm);
    }
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Sar(node_t node) {
  PPCOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ShiftOp& sar = this->Get(node).template Cast<ShiftOp>();
    const Operation& lhs = this->Get(sar.left());
    if (CanCover(node, sar.left()) && lhs.Is<Opmask::kWord32ShiftLeft>()) {
      const ShiftOp& shl = lhs.Cast<ShiftOp>();
      if (this->is_integer_constant(sar.right()) &&
          this->is_integer_constant(shl.right())) {
        uint32_t sar_by = this->integer_constant(sar.right());
        uint32_t shl_by = this->integer_constant(shl.right());
        if ((sar_by == shl_by) && (sar_by == 16)) {
          Emit(kPPC_ExtendSignWord16, g.DefineAsRegister(node),
               g.UseRegister(shl.left()));
          return;
        } else if ((sar_by == shl_by) && (sar_by == 24)) {
          Emit(kPPC_ExtendSignWord8, g.DefineAsRegister(node),
               g.UseRegister(shl.left()));
          return;
        }
      }
    }
    VisitRRO(this, kPPC_ShiftRightAlg32, node, kShift32Imm);
  } else {
    Int32BinopMatcher m(node);
    // Replace with sign extension for (x << K) >> K where K is 16 or 24.
    if (CanCover(node, m.left().node()) && m.left().IsWord32Shl()) {
      Int32BinopMatcher mleft(m.left().node());
      if (mleft.right().Is(16) && m.right().Is(16)) {
        Emit(kPPC_ExtendSignWord16, g.DefineAsRegister(node),
             g.UseRegister(mleft.left().node()));
        return;
      } else if (mleft.right().Is(24) && m.right().Is(24)) {
        Emit(kPPC_ExtendSignWord8, g.DefineAsRegister(node),
             g.UseRegister(mleft.left().node()));
        return;
      }
    }
    VisitRRO(this, kPPC_ShiftRightAlg32, node, kShift32Imm);
  }
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Sar(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    if constexpr (Adapter::IsTurboshaft) {
      using namespace turboshaft;  // NOLINT(build/namespaces)
      DCHECK(this->Get(node).template Cast<ShiftOp>().IsRightShift());
      const ShiftOp& shift = this->Get(node).template Cast<ShiftOp>();
      const Operation& lhs = this->Get(shift.left());
      int64_t constant_rhs;

      if (lhs.Is<LoadOp>() &&
          this->MatchIntegralWord64Constant(shift.right(), &constant_rhs) &&
          constant_rhs == 32 && this->CanCover(node, shift.left())) {
        // Just load and sign-extend the interesting 4 bytes instead. This
        // happens, for example, when we're loading and untagging SMIs.
        const LoadOp& load = lhs.Cast<LoadOp>();
        int64_t offset = 0;
        if (load.index().has_value()) {
          int64_t index_constant;
          if (this->MatchIntegralWord64Constant(load.index().value(),
                                                &index_constant)) {
            DCHECK_EQ(load.element_size_log2, 0);
            offset = index_constant;
          }
        } else {
          offset = load.offset;
        }
        offset = SmiWordOffset(offset);
        if (g.CanBeImmediate(offset, kInt16Imm_4ByteAligned)) {
          Emit(kPPC_LoadWordS32 | AddressingModeField::encode(kMode_MRI),
               g.DefineAsRegister(node), g.UseRegister(load.base()),
               g.TempImmediate(offset), g.UseImmediate(0));
          return;
        }
      }
    } else {
      Int64BinopMatcher m(node);
      if (CanCover(m.node(), m.left().node()) && m.left().IsLoad() &&
          m.right().Is(32)) {
        // Just load and sign-extend the interesting 4 bytes instead. This
        // happens, for example, when we're loading and untagging SMIs.
        BaseWithIndexAndDisplacement64Matcher mleft(m.left().node(),
                                                    AddressOption::kAllowAll);
        if (mleft.matches() && mleft.index() == nullptr) {
          int64_t offset = 0;
          Node* displacement = mleft.displacement();
          if (displacement != nullptr) {
            Int64Matcher mdisplacement(displacement);
            DCHECK(mdisplacement.HasResolvedValue());
            offset = mdisplacement.ResolvedValue();
          }
          offset = SmiWordOffset(offset);
          if (g.CanBeImmediate(offset, kInt16Imm_4ByteAligned)) {
            Emit(kPPC_LoadWordS32 | AddressingModeField::encode(kMode_MRI),
                 g.DefineAsRegister(node), g.UseRegister(mleft.base()),
                 g.TempImmediate(offset), g.UseImmediate(0));
            return;
          }
        }
      }
    }
    VisitRRO(this, kPPC_ShiftRightAlg64, node, kShift64Imm);
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Rol(node_t node) {
    UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Rol(node_t node) {
  UNREACHABLE();
}

// TODO(mbrandy): Absorb logical-and into rlwinm?
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Ror(node_t node) {
    VisitRRO(this, kPPC_RotRight32, node, kShift32Imm);
}

#if V8_TARGET_ARCH_PPC64
// TODO(mbrandy): Absorb logical-and into rldic?
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Ror(node_t node) {
    VisitRRO(this, kPPC_RotRight64, node, kShift64Imm);
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Clz(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    Emit(kPPC_Cntlz32, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Clz(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    Emit(kPPC_Cntlz64, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Popcnt(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    Emit(kPPC_Popcnt32, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Popcnt(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    Emit(kPPC_Popcnt64, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Ctz(node_t node) {
  UNREACHABLE();
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Ctz(node_t node) {
  UNREACHABLE();
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32ReverseBits(node_t node) {
  UNREACHABLE();
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64ReverseBits(node_t node) {
  UNREACHABLE();
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64ReverseBytes(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    PPCOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp[] = {g.TempRegister()};
    node_t input = this->Get(node).input(0);
    const Operation& input_op = this->Get(input);
    if (CanCover(node, input) && input_op.Is<LoadOp>()) {
      auto load = this->load_view(input);
      LoadRepresentation load_rep = load.loaded_rep();
      if (load_rep.representation() == MachineRepresentation::kWord64) {
        node_t base = load.base();
        node_t offset = load.index();
        bool is_atomic = load.is_atomic();
        Emit(kPPC_LoadByteRev64 | AddressingModeField::encode(kMode_MRR),
             g.DefineAsRegister(node), g.UseRegister(base),
             g.UseRegister(offset), g.UseImmediate(is_atomic));
        return;
      }
    }
    Emit(kPPC_ByteRev64, g.DefineAsRegister(node),
         g.UseUniqueRegister(this->input_at(node, 0)), 1, temp);
  } else {
    PPCOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp[] = {g.TempRegister()};
    NodeMatcher input(node->InputAt(0));
    if (CanCover(node, input.node()) && input.IsLoad()) {
      LoadRepresentation load_rep = LoadRepresentationOf(input.node()->op());
      if (load_rep.representation() == MachineRepresentation::kWord64) {
        Node* load_op = input.node();
        Node* base = load_op->InputAt(0);
        Node* offset = load_op->InputAt(1);
        bool is_atomic = (load_op->opcode() == IrOpcode::kWord32AtomicLoad ||
                          load_op->opcode() == IrOpcode::kWord64AtomicLoad);
        Emit(kPPC_LoadByteRev64 | AddressingModeField::encode(kMode_MRR),
             g.DefineAsRegister(node), g.UseRegister(base),
             g.UseRegister(offset), g.UseImmediate(is_atomic));
        return;
      }
    }
    Emit(kPPC_ByteRev64, g.DefineAsRegister(node),
         g.UseUniqueRegister(node->InputAt(0)), 1, temp);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32ReverseBytes(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    PPCOperandGeneratorT<Adapter> g(this);
    node_t input = this->Get(node).input(0);
    const Operation& input_op = this->Get(input);
    if (CanCover(node, input) && input_op.Is<LoadOp>()) {
      auto load = this->load_view(input);
      LoadRepresentation load_rep = load.loaded_rep();
      if (load_rep.representation() == MachineRepresentation::kWord32) {
        node_t base = load.base();
        node_t offset = load.index();
        bool is_atomic = load.is_atomic();
        Emit(kPPC_LoadByteRev32 | AddressingModeField::encode(kMode_MRR),
             g.DefineAsRegister(node), g.UseRegister(base),
             g.UseRegister(offset), g.UseImmediate(is_atomic));
        return;
      }
    }
    Emit(kPPC_ByteRev32, g.DefineAsRegister(node),
         g.UseUniqueRegister(this->input_at(node, 0)));
  } else {
    PPCOperandGeneratorT<Adapter> g(this);
    NodeMatcher input(node->InputAt(0));
    if (CanCover(node, input.node()) && input.IsLoad()) {
      LoadRepresentation load_rep = LoadRepresentationOf(input.node()->op());
      if (load_rep.representation() == MachineRepresentation::kWord32) {
        Node* load_op = input.node();
        Node* base = load_op->InputAt(0);
        Node* offset = load_op->InputAt(1);
        bool is_atomic = (load_op->opcode() == IrOpcode::kWord32AtomicLoad ||
                          load_op->opcode() == IrOpcode::kWord64AtomicLoad);
        Emit(kPPC_LoadByteRev32 | AddressingModeField::encode(kMode_MRR),
             g.DefineAsRegister(node), g.UseRegister(base),
             g.UseRegister(offset), g.UseImmediate(is_atomic));
        return;
      }
    }
    Emit(kPPC_ByteRev32, g.DefineAsRegister(node),
         g.UseRegister(node->InputAt(0)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSimd128ReverseBytes(node_t node) {
  PPCOperandGeneratorT<Adapter> g(this);
  Emit(kPPC_LoadReverseSimd128RR, g.DefineAsRegister(node),
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Add(node_t node) {
  VisitBinop<Adapter>(this, node, kPPC_Add32, kInt16Imm);
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64Add(node_t node) {
  VisitBinop<Adapter>(this, node, kPPC_Add64, kInt16Imm);
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Sub(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    if constexpr (Adapter::IsTurboshaft) {
      using namespace turboshaft;  // NOLINT(build/namespaces)
      const WordBinopOp& sub = this->Get(node).template Cast<WordBinopOp>();
      if (this->MatchIntegralZero(sub.left())) {
        Emit(kPPC_Neg, g.DefineAsRegister(node), g.UseRegister(sub.right()));
      } else {
        VisitBinop<Adapter>(this, node, kPPC_Sub, kInt16Imm_Negate);
      }
    } else {
      Int32BinopMatcher m(node);
      if (m.left().Is(0)) {
        Emit(kPPC_Neg, g.DefineAsRegister(node),
             g.UseRegister(m.right().node()));
      } else {
        VisitBinop<Adapter>(this, node, kPPC_Sub, kInt16Imm_Negate);
      }
    }
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64Sub(node_t node) {
  PPCOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const WordBinopOp& sub = this->Get(node).template Cast<WordBinopOp>();
    if (this->MatchIntegralZero(sub.left())) {
      Emit(kPPC_Neg, g.DefineAsRegister(node), g.UseRegister(sub.right()));
    } else {
      VisitBinop<Adapter>(this, node, kPPC_Sub, kInt16Imm_Negate);
    }
  } else {
    PPCOperandGeneratorT<Adapter> g(this);
    Int64BinopMatcher m(node);
    if (m.left().Is(0)) {
      Emit(kPPC_Neg, g.DefineAsRegister(node), g.UseRegister(m.right().node()));
    } else {
      VisitBinop<Adapter>(this, node, kPPC_Sub, kInt16Imm_Negate);
    }
  }
}
#endif

namespace {

template <typename Adapter>
void VisitCompare(InstructionSelectorT<Adapter>* selector,
                  InstructionCode opcode, InstructionOperand left,
                  InstructionOperand right, FlagsContinuationT<Adapter>* cont);
template <typename Adapter>
void EmitInt32MulWithOverflow(InstructionSelectorT<Adapter>* selector,
                              typename Adapter::node_t node,
                              FlagsContinuationT<Adapter>* cont) {
  PPCOperandGeneratorT<Adapter> g(selector);
  typename Adapter::node_t lhs = selector->input_at(node, 0);
  typename Adapter::node_t rhs = selector->input_at(node, 1);
  InstructionOperand result_operand = g.DefineAsRegister(node);
  InstructionOperand high32_operand = g.TempRegister();
  InstructionOperand temp_operand = g.TempRegister();
  {
    InstructionOperand outputs[] = {result_operand, high32_operand};
    InstructionOperand inputs[] = {g.UseRegister(lhs), g.UseRegister(rhs)};
    selector->Emit(kPPC_Mul32WithHigh32, 2, outputs, 2, inputs);
  }
  {
    InstructionOperand shift_31 = g.UseImmediate(31);
    InstructionOperand outputs[] = {temp_operand};
    InstructionOperand inputs[] = {result_operand, shift_31};
    selector->Emit(kPPC_ShiftRightAlg32, 1, outputs, 2, inputs);
  }

  VisitCompare(selector, kPPC_Cmp32, high32_operand, temp_operand, cont);
}

template <typename Adapter>
void EmitInt64MulWithOverflow(InstructionSelectorT<Adapter>* selector,
                              typename Adapter::node_t node,
                              FlagsContinuationT<Adapter>* cont) {
  PPCOperandGeneratorT<Adapter> g(selector);
  typename Adapter::node_t lhs = selector->input_at(node, 0);
  typename Adapter::node_t rhs = selector->input_at(node, 1);
  InstructionOperand result = g.DefineAsRegister(node);
  InstructionOperand left = g.UseRegister(lhs);
  InstructionOperand high = g.TempRegister();
  InstructionOperand result_sign = g.TempRegister();
  InstructionOperand right = g.UseRegister(rhs);
  selector->Emit(kPPC_Mul64, result, left, right);
  selector->Emit(kPPC_MulHighS64, high, left, right);
  selector->Emit(kPPC_ShiftRightAlg64, result_sign, result,
                 g.TempImmediate(63));
  // Test whether {high} is a sign-extension of {result}.
  selector->EmitWithContinuation(kPPC_Cmp64, high, result_sign, cont);
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Mul(node_t node) {
    VisitRRR(this, kPPC_Mul32, node);
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64Mul(node_t node) {
    VisitRRR(this, kPPC_Mul64, node);
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32MulHigh(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    Emit(kPPC_MulHigh32, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32MulHigh(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    Emit(kPPC_MulHighU32, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64MulHigh(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    Emit(kPPC_MulHighS64, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint64MulHigh(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    Emit(kPPC_MulHighU64, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Div(node_t node) {
    VisitRRR(this, kPPC_Div32, node);
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64Div(node_t node) {
    VisitRRR(this, kPPC_Div64, node);
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32Div(node_t node) {
    VisitRRR(this, kPPC_DivU32, node);
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint64Div(node_t node) {
    VisitRRR(this, kPPC_DivU64, node);
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Mod(node_t node) {
    VisitRRR(this, kPPC_Mod32, node);
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64Mod(node_t node) {
    VisitRRR(this, kPPC_Mod64, node);
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32Mod(node_t node) {
    VisitRRR(this, kPPC_ModU32, node);
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint64Mod(node_t node) {
    VisitRRR(this, kPPC_ModU64, node);
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeFloat32ToFloat64(node_t node) {
    VisitRR(this, kPPC_Float32ToDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitRoundInt32ToFloat32(node_t node) {
    VisitRR(this, kPPC_Int32ToFloat32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitRoundUint32ToFloat32(node_t node) {
    VisitRR(this, kPPC_Uint32ToFloat32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeInt32ToFloat64(node_t node) {
    VisitRR(this, kPPC_Int32ToDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeUint32ToFloat64(node_t node) {
    VisitRR(this, kPPC_Uint32ToDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeFloat64ToInt32(node_t node) {
    VisitRR(this, kPPC_DoubleToInt32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeFloat64ToUint32(node_t node) {
    VisitRR(this, kPPC_DoubleToUint32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat64ToUint32(node_t node) {
    VisitRR(this, kPPC_DoubleToUint32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord8ToInt32(node_t node) {
    // TODO(mbrandy): inspect input to see if nop is appropriate.
    VisitRR(this, kPPC_ExtendSignWord8, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord16ToInt32(node_t node) {
    // TODO(mbrandy): inspect input to see if nop is appropriate.
    VisitRR(this, kPPC_ExtendSignWord16, node);
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat32ToInt64(
    node_t node) {
    VisitTryTruncateDouble(this, kPPC_DoubleToInt64, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat64ToInt64(
    node_t node) {
    VisitTryTruncateDouble(this, kPPC_DoubleToInt64, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat64ToInt64(node_t node) {
    VisitRR(this, kPPC_DoubleToInt64, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat32ToUint64(
    node_t node) {
    VisitTryTruncateDouble(this, kPPC_DoubleToUint64, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat64ToUint64(
    node_t node) {
    VisitTryTruncateDouble(this, kPPC_DoubleToUint64, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat64ToInt32(
    node_t node) {
    VisitTryTruncateDouble(this, kPPC_DoubleToInt32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat64ToUint32(
    node_t node) {
    VisitTryTruncateDouble(this, kPPC_DoubleToUint32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBitcastWord32ToWord64(node_t node) {
    DCHECK(SmiValuesAre31Bits());
    DCHECK(COMPRESS_POINTERS_BOOL);
    EmitIdentity(node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeInt32ToInt64(node_t node) {
    // TODO(mbrandy): inspect input to see if nop is appropriate.
    VisitRR(this, kPPC_ExtendSignWord32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord8ToInt64(node_t node) {
    // TODO(mbrandy): inspect input to see if nop is appropriate.
    VisitRR(this, kPPC_ExtendSignWord8, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord16ToInt64(node_t node) {
    // TODO(mbrandy): inspect input to see if nop is appropriate.
    VisitRR(this, kPPC_ExtendSignWord16, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord32ToInt64(node_t node) {
    // TODO(mbrandy): inspect input to see if nop is appropriate.
    VisitRR(this, kPPC_ExtendSignWord32, node);
}

template <typename Adapter>
bool InstructionSelectorT<Adapter>::ZeroExtendsWord32ToWord64NoPhis(
    node_t node) {
  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeUint32ToUint64(node_t node) {
    // TODO(mbrandy): inspect input to see if nop is appropriate.
    VisitRR(this, kPPC_Uint32ToUint64, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeFloat64ToUint64(node_t node) {
    VisitRR(this, kPPC_DoubleToUint64, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeFloat64ToInt64(node_t node) {
    VisitRR(this, kPPC_DoubleToInt64, node);
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat64ToFloat32(node_t node) {
    VisitRR(this, kPPC_DoubleToFloat32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat64ToWord32(node_t node) {
  VisitRR(this, kArchTruncateDoubleToI, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitRoundFloat64ToInt32(node_t node) {
    VisitRR(this, kPPC_DoubleToInt32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat32ToInt32(node_t node) {
  PPCOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Operation& op = this->Get(node);
    InstructionCode opcode = kPPC_Float32ToInt32;
    if (op.Is<Opmask::kTruncateFloat32ToInt32OverflowToMin>()) {
      opcode |= MiscField::encode(true);
    }
    Emit(opcode, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
  } else {
    InstructionCode opcode = kPPC_Float32ToInt32;
    TruncateKind kind = OpParameter<TruncateKind>(node->op());
    if (kind == TruncateKind::kSetOverflowToMin) {
      opcode |= MiscField::encode(true);
    }

    Emit(opcode, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat32ToUint32(node_t node) {
  PPCOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Operation& op = this->Get(node);
    InstructionCode opcode = kPPC_Float32ToUint32;
    if (op.Is<Opmask::kTruncateFloat32ToUint32OverflowToMin>()) {
      opcode |= MiscField::encode(true);
    }

    Emit(opcode, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
  } else {
    InstructionCode opcode = kPPC_Float32ToUint32;
    TruncateKind kind = OpParameter<TruncateKind>(node->op());
    if (kind == TruncateKind::kSetOverflowToMin) {
      opcode |= MiscField::encode(true);
    }

    Emit(opcode, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
  }
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateInt64ToInt32(node_t node) {
    // TODO(mbrandy): inspect input to see if nop is appropriate.
    VisitRR(this, kPPC_Int64ToInt32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitRoundInt64ToFloat32(node_t node) {
    VisitRR(this, kPPC_Int64ToFloat32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitRoundInt64ToFloat64(node_t node) {
    VisitRR(this, kPPC_Int64ToDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeInt64ToFloat64(node_t node) {
    VisitRR(this, kPPC_Int64ToDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitRoundUint64ToFloat32(node_t node) {
    VisitRR(this, kPPC_Uint64ToFloat32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitRoundUint64ToFloat64(node_t node) {
    VisitRR(this, kPPC_Uint64ToDouble, node);
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBitcastFloat32ToInt32(node_t node) {
  VisitRR(this, kPPC_BitcastFloat32ToInt32, node);
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBitcastFloat64ToInt64(node_t node) {
  VisitRR(this, kPPC_BitcastDoubleToInt64, node);
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBitcastInt32ToFloat32(node_t node) {
    VisitRR(this, kPPC_BitcastInt32ToFloat32, node);
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBitcastInt64ToFloat64(node_t node) {
    VisitRR(this, kPPC_BitcastInt64ToDouble, node);
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Add(node_t node) {
    VisitRRR(this, kPPC_AddDouble | MiscField::encode(1), node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Add(node_t node) {
    // TODO(mbrandy): detect multiply-add
    VisitRRR(this, kPPC_AddDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Sub(node_t node) {
    VisitRRR(this, kPPC_SubDouble | MiscField::encode(1), node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Sub(node_t node) {
    // TODO(mbrandy): detect multiply-subtract
    VisitRRR(this, kPPC_SubDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Mul(node_t node) {
    VisitRRR(this, kPPC_MulDouble | MiscField::encode(1), node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Mul(node_t node) {
    // TODO(mbrandy): detect negate
    VisitRRR(this, kPPC_MulDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Div(node_t node) {
    VisitRRR(this, kPPC_DivDouble | MiscField::encode(1), node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Div(node_t node) {
    VisitRRR(this, kPPC_DivDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Mod(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    Emit(kPPC_ModDouble, g.DefineAsFixed(node, d1),
         g.UseFixed(this->input_at(node, 0), d1),
         g.UseFixed(this->input_at(node, 1), d2))
        ->MarkAsCall();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Max(node_t node) {
    VisitRRR(this, kPPC_MaxDouble | MiscField::encode(1), node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Max(node_t node) {
    VisitRRR(this, kPPC_MaxDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64SilenceNaN(node_t node) {
    VisitRR(this, kPPC_Float64SilenceNaN, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Min(node_t node) {
    VisitRRR(this, kPPC_MinDouble | MiscField::encode(1), node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Min(node_t node) {
    VisitRRR(this, kPPC_MinDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Abs(node_t node) {
    VisitRR(this, kPPC_AbsDouble | MiscField::encode(1), node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Abs(node_t node) {
    VisitRR(this, kPPC_AbsDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Sqrt(node_t node) {
    VisitRR(this, kPPC_SqrtDouble | MiscField::encode(1), node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Ieee754Unop(
    node_t node, InstructionCode opcode) {
  PPCOperandGeneratorT<Adapter> g(this);
  Emit(opcode, g.DefineAsFixed(node, d1),
       g.UseFixed(this->input_at(node, 0), d1))
      ->MarkAsCall();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Ieee754Binop(
    node_t node, InstructionCode opcode) {
    PPCOperandGeneratorT<Adapter> g(this);
    Emit(opcode, g.DefineAsFixed(node, d1),
         g.UseFixed(this->input_at(node, 0), d1),
         g.UseFixed(this->input_at(node, 1), d2))
        ->MarkAsCall();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Sqrt(node_t node) {
    VisitRR(this, kPPC_SqrtDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32RoundDown(node_t node) {
    VisitRR(this, kPPC_FloorDouble | MiscField::encode(1), node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64RoundDown(node_t node) {
    VisitRR(this, kPPC_FloorDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32RoundUp(node_t node) {
    VisitRR(this, kPPC_CeilDouble | MiscField::encode(1), node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64RoundUp(node_t node) {
    VisitRR(this, kPPC_CeilDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32RoundTruncate(node_t node) {
    VisitRR(this, kPPC_TruncateDouble | MiscField::encode(1), node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64RoundTruncate(node_t node) {
    VisitRR(this, kPPC_TruncateDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64RoundTiesAway(node_t node) {
    VisitRR(this, kPPC_RoundDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Neg(node_t node) {
    VisitRR(this, kPPC_NegDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Neg(node_t node) {
    VisitRR(this, kPPC_NegDouble, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32AddWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (this->valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop<Adapter>(this, node, kPPC_AddWithOverflow32, kInt16Imm,
                               &cont);
  }
    FlagsContinuation cont;
    VisitBinop<Adapter>(this, node, kPPC_AddWithOverflow32, kInt16Imm, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32SubWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (this->valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop<Adapter>(this, node, kPPC_SubWithOverflow32,
                               kInt16Imm_Negate, &cont);
  }
    FlagsContinuation cont;
    VisitBinop<Adapter>(this, node, kPPC_SubWithOverflow32, kInt16Imm_Negate,
                        &cont);
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64AddWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (this->valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop<Adapter>(this, node, kPPC_Add64, kInt16Imm, &cont);
  }
    FlagsContinuation cont;
    VisitBinop<Adapter>(this, node, kPPC_Add64, kInt16Imm, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64SubWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (this->valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop<Adapter>(this, node, kPPC_Sub, kInt16Imm_Negate, &cont);
  }
    FlagsContinuation cont;
    VisitBinop<Adapter>(this, node, kPPC_Sub, kInt16Imm_Negate, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64MulWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (this->valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kNotEqual, ovf);
    return EmitInt64MulWithOverflow(this, node, &cont);
  }
    FlagsContinuation cont;
    EmitInt64MulWithOverflow(this, node, &cont);
}
#endif

template <typename Adapter>
static bool CompareLogical(FlagsContinuationT<Adapter>* cont) {
    switch (cont->condition()) {
      case kUnsignedLessThan:
      case kUnsignedGreaterThanOrEqual:
      case kUnsignedLessThanOrEqual:
      case kUnsignedGreaterThan:
        return true;
      default:
        return false;
    }
    UNREACHABLE();
}

namespace {

// Shared routine for multiple compare operations.
template <typename Adapter>
void VisitCompare(InstructionSelectorT<Adapter>* selector,
                  InstructionCode opcode, InstructionOperand left,
                  InstructionOperand right, FlagsContinuationT<Adapter>* cont) {
  selector->EmitWithContinuation(opcode, left, right, cont);
}

// Shared routine for multiple word compare operations.
template <typename Adapter>
void VisitWordCompare(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node, InstructionCode opcode,
                      FlagsContinuationT<Adapter>* cont, bool commutative,
                      ImmediateMode immediate_mode) {
    PPCOperandGeneratorT<Adapter> g(selector);
    typename Adapter::node_t lhs = selector->input_at(node, 0);
    typename Adapter::node_t rhs = selector->input_at(node, 1);

    // Match immediates on left or right side of comparison.
    if (g.CanBeImmediate(rhs, immediate_mode)) {
      VisitCompare(selector, opcode, g.UseRegister(lhs), g.UseImmediate(rhs),
                   cont);
    } else if (g.CanBeImmediate(lhs, immediate_mode)) {
      if (!commutative) cont->Commute();
      VisitCompare(selector, opcode, g.UseRegister(rhs), g.UseImmediate(lhs),
                   cont);
    } else {
      VisitCompare(selector, opcode, g.UseRegister(lhs), g.UseRegister(rhs),
                   cont);
    }
}

template <typename Adapter>
void VisitWord32Compare(InstructionSelectorT<Adapter>* selector,
                        typename Adapter::node_t node,
                        FlagsContinuationT<Adapter>* cont) {
    ImmediateMode mode =
        (CompareLogical(cont) ? kInt16Imm_Unsigned : kInt16Imm);
    VisitWordCompare(selector, node, kPPC_Cmp32, cont, false, mode);
}

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void VisitWord64Compare(InstructionSelectorT<Adapter>* selector,
                        typename Adapter::node_t node,
                        FlagsContinuationT<Adapter>* cont) {
  ImmediateMode mode = (CompareLogical(cont) ? kInt16Imm_Unsigned : kInt16Imm);
  VisitWordCompare(selector, node, kPPC_Cmp64, cont, false, mode);
}
#endif

// Shared routine for multiple float32 compare operations.
template <typename Adapter>
void VisitFloat32Compare(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node,
                         FlagsContinuationT<Adapter>* cont) {
    PPCOperandGeneratorT<Adapter> g(selector);
    typename Adapter::node_t lhs = selector->input_at(node, 0);
    typename Adapter::node_t rhs = selector->input_at(node, 1);
    VisitCompare(selector, kPPC_CmpDouble, g.UseRegister(lhs),
                 g.UseRegister(rhs), cont);
}

// Shared routine for multiple float64 compare operations.
template <typename Adapter>
void VisitFloat64Compare(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node,
                         FlagsContinuationT<Adapter>* cont) {
    PPCOperandGeneratorT<Adapter> g(selector);
    typename Adapter::node_t lhs = selector->input_at(node, 0);
    typename Adapter::node_t rhs = selector->input_at(node, 1);
    VisitCompare(selector, kPPC_CmpDouble, g.UseRegister(lhs),
                 g.UseRegister(rhs), cont);
}

}  // namespace

// Shared routine for word comparisons against zero.
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWordCompareZero(
    node_t user, node_t value, FlagsContinuation* cont) {
    // Try to combine with comparisons against 0 by simply inverting the branch.
    while (value->opcode() == IrOpcode::kWord32Equal && CanCover(user, value)) {
      Int32BinopMatcher m(value);
      if (!m.right().Is(0)) break;

      user = value;
      value = m.left().node();
      cont->Negate();
    }

    if (CanCover(user, value)) {
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
#if V8_TARGET_ARCH_PPC64
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
#endif
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
                return VisitBinop<Adapter>(this, node, kPPC_AddWithOverflow32,
                                           kInt16Imm, cont);
              case IrOpcode::kInt32SubWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop<Adapter>(this, node, kPPC_SubWithOverflow32,
                                           kInt16Imm_Negate, cont);
              case IrOpcode::kInt32MulWithOverflow:
                cont->OverwriteAndNegateIfEqual(kNotEqual);
                return EmitInt32MulWithOverflow(this, node, cont);
#if V8_TARGET_ARCH_PPC64
              case IrOpcode::kInt64AddWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop<Adapter>(this, node, kPPC_Add64, kInt16Imm,
                                           cont);
              case IrOpcode::kInt64SubWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop<Adapter>(this, node, kPPC_Sub,
                                           kInt16Imm_Negate, cont);
              case IrOpcode::kInt64MulWithOverflow:
                cont->OverwriteAndNegateIfEqual(kNotEqual);
                return EmitInt64MulWithOverflow(this, node, cont);
#endif
              default:
                break;
            }
          }
        }
        break;
      case IrOpcode::kInt32Sub:
        return VisitWord32Compare(this, value, cont);
      case IrOpcode::kWord32And:
        // TODO(mbandy): opportunity for rlwinm?
        return VisitWordCompare(this, value, kPPC_Tst32, cont, true,
                                kInt16Imm_Unsigned);
// TODO(mbrandy): Handle?
// case IrOpcode::kInt32Add:
// case IrOpcode::kWord32Or:
// case IrOpcode::kWord32Xor:
// case IrOpcode::kWord32Sar:
// case IrOpcode::kWord32Shl:
// case IrOpcode::kWord32Shr:
// case IrOpcode::kWord32Ror:
#if V8_TARGET_ARCH_PPC64
      case IrOpcode::kInt64Sub:
        return VisitWord64Compare(this, value, cont);
      case IrOpcode::kWord64And:
        // TODO(mbandy): opportunity for rldic?
        return VisitWordCompare(this, value, kPPC_Tst64, cont, true,
                                kInt16Imm_Unsigned);
// TODO(mbrandy): Handle?
// case IrOpcode::kInt64Add:
// case IrOpcode::kWord64Or:
// case IrOpcode::kWord64Xor:
// case IrOpcode::kWord64Sar:
// case IrOpcode::kWord64Shl:
// case IrOpcode::kWord64Shr:
// case IrOpcode::kWord64Ror:
#endif
      case IrOpcode::kStackPointerGreaterThan:
        cont->OverwriteAndNegateIfEqual(kStackPointerGreaterThanCondition);
        return VisitStackPointerGreaterThan(value, cont);
      default:
        break;
      }
    }

  // Branch could not be combined with a compare, emit compare against 0.
  PPCOperandGeneratorT<Adapter> g(this);
  VisitCompare(this, kPPC_Cmp32, g.UseRegister(value), g.TempImmediate(0),
               cont);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWordCompareZero(
    node_t user, node_t value, FlagsContinuation* cont) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  // Try to combine with comparisons against 0 by simply inverting the branch.
  ConsumeEqualZero(&user, &value, cont);

  if (CanCover(user, value)) {
    const Operation& value_op = Get(value);
    if (const ComparisonOp* comparison = value_op.TryCast<ComparisonOp>()) {
      switch (comparison->rep.MapTaggedToWord().value()) {
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
              cont->OverwriteAndNegateIfEqual(kUnsignedLessThan);
              return VisitFloat32Compare(this, value, cont);
            case ComparisonOp::Kind::kSignedLessThanOrEqual:
              cont->OverwriteAndNegateIfEqual(kUnsignedLessThanOrEqual);
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
              cont->OverwriteAndNegateIfEqual(kUnsignedLessThan);
              return VisitFloat64Compare(this, value, cont);
            case ComparisonOp::Kind::kSignedLessThanOrEqual:
              cont->OverwriteAndNegateIfEqual(kUnsignedLessThanOrEqual);
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
                return VisitBinop(this, node,
                                  is64 ? kPPC_Add64 : kPPC_AddWithOverflow32,
                                  kInt16Imm, cont);
              case OverflowCheckedBinopOp::Kind::kSignedSub:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop(this, node,
                                  is64 ? kPPC_Sub : kPPC_SubWithOverflow32,
                                  kInt16Imm_Negate, cont);
              case OverflowCheckedBinopOp::Kind::kSignedMul:
                if (is64) {
                  cont->OverwriteAndNegateIfEqual(kNotEqual);
                  return EmitInt64MulWithOverflow(this, node, cont);
                } else {
                  cont->OverwriteAndNegateIfEqual(kNotEqual);
                  return EmitInt32MulWithOverflow(this, node, cont);
                }
            }
          }
        }
      }
    } else if (value_op.Is<Opmask::kWord32Sub>()) {
      return VisitWord32Compare(this, value, cont);
    } else if (value_op.Is<Opmask::kWord32BitwiseAnd>()) {
      return VisitWordCompare(this, value, kPPC_Tst32, cont, true,
                              kInt16Imm_Unsigned);
    } else if (value_op.Is<Opmask::kWord64Sub>()) {
      return VisitWord64Compare(this, value, cont);
    } else if (value_op.Is<Opmask::kWord64BitwiseAnd>()) {
      return VisitWordCompare(this, value, kPPC_Tst64, cont, true,
                              kInt16Imm_Unsigned);
    } else if (value_op.Is<StackPointerGreaterThanOp>()) {
      cont->OverwriteAndNegateIfEqual(kStackPointerGreaterThanCondition);
      return VisitStackPointerGreaterThan(value, cont);
    }
  }

  // Branch could not be combined with a compare, emit compare against 0.
  PPCOperandGeneratorT<TurboshaftAdapter> g(this);
  VisitCompare(this, kPPC_Cmp32, g.UseRegister(value), g.TempImmediate(0),
               cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSwitch(node_t node,
                                                const SwitchInfo& sw) {
  PPCOperandGeneratorT<Adapter> g(this);
  InstructionOperand value_operand = g.UseRegister(this->input_at(node, 0));

  // Emit either ArchTableSwitch or ArchBinarySearchSwitch.
  if (enable_switch_jump_table_ ==
      InstructionSelector::kEnableSwitchJumpTable) {
  static const size_t kMaxTableSwitchValueRange = 2 << 16;
  size_t table_space_cost = 4 + sw.value_range();
  size_t table_time_cost = 3;
  size_t lookup_space_cost = 3 + 2 * sw.case_count();
  size_t lookup_time_cost = sw.case_count();
  if (sw.case_count() > 0 &&
      table_space_cost + 3 * table_time_cost <=
          lookup_space_cost + 3 * lookup_time_cost &&
      sw.min_value() > std::numeric_limits<int32_t>::min() &&
      sw.value_range() <= kMaxTableSwitchValueRange) {
      InstructionOperand index_operand = value_operand;
      if (sw.min_value()) {
      index_operand = g.TempRegister();
      Emit(kPPC_Sub, index_operand, value_operand,
           g.TempImmediate(sw.min_value()));
      }
      // Zero extend, because we use it as 64-bit index into the jump table.
      InstructionOperand index_operand_zero_ext = g.TempRegister();
      Emit(kPPC_Uint32ToUint64, index_operand_zero_ext, index_operand);
      index_operand = index_operand_zero_ext;
      // Generate a table lookup.
      return EmitTableSwitch(sw, index_operand);
  }
  }

  // Generate a tree of conditional jumps.
  return EmitBinarySearchSwitch(sw, value_operand);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord32Equal(
    node_t const node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
  if (isolate() && (V8_STATIC_ROOTS_BOOL ||
                    (COMPRESS_POINTERS_BOOL && !isolate()->bootstrapper()))) {
    PPCOperandGeneratorT<TurbofanAdapter> g(this);
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
      Tagged_t ptr = MacroAssemblerBase::ReadOnlyRootPtr(root_index, isolate());
      if (g.CanBeImmediate(ptr, kInt16Imm)) {
          return VisitCompare(this, kPPC_Cmp32, g.UseRegister(left),
                              g.TempImmediate(ptr), &cont);
      }
      }
  }
  }
  VisitWord32Compare(this, node, &cont);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord32Equal(
    node_t const node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Operation& equal = Get(node);
  DCHECK(equal.Is<ComparisonOp>());
  OpIndex left = equal.input(0);
  FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
  if (isolate() && (V8_STATIC_ROOTS_BOOL ||
                    (COMPRESS_POINTERS_BOOL && !isolate()->bootstrapper()))) {
    PPCOperandGeneratorT<TurboshaftAdapter> g(this);
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
        if (g.CanBeImmediate(ptr, kInt16Imm)) {
          return VisitCompare(this, kPPC_Cmp32, g.UseRegister(left),
                              g.TempImmediate(ptr), &cont);
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

#if V8_TARGET_ARCH_PPC64
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Equal(node_t const node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
  VisitWord64Compare(this, node, &cont);
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
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32MulWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (this->valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kNotEqual, ovf);
    return EmitInt32MulWithOverflow(this, node, &cont);
  }
  FlagsContinuation cont;
  EmitInt32MulWithOverflow(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Equal(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
  VisitFloat32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32LessThan(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kUnsignedLessThan, node);
  VisitFloat32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kUnsignedLessThanOrEqual, node);
  VisitFloat32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Equal(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
  VisitFloat64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64LessThan(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kUnsignedLessThan, node);
  VisitFloat64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kUnsignedLessThanOrEqual, node);
  VisitFloat64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitMoveParamToFPR(node_t node, int index) {
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitMoveFPRToParam(
    InstructionOperand* op, LinkageLocation location) {}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitPrepareArguments(
    ZoneVector<PushParameter>* arguments, const CallDescriptor* call_descriptor,
    node_t node) {
  PPCOperandGeneratorT<Adapter> g(this);

  // Prepare for C function call.
  if (call_descriptor->IsCFunctionCall()) {
    Emit(kArchPrepareCallCFunction | MiscField::encode(static_cast<int>(
                                         call_descriptor->ParameterCount())),
         0, nullptr, 0, nullptr);

    // Poke any stack arguments.
    int slot = kStackFrameExtraParamSlot;
    for (PushParameter input : (*arguments)) {
      if (!this->valid(input.node)) continue;
      Emit(kPPC_StoreToStackSlot, g.NoOutput(), g.UseRegister(input.node),
           g.TempImmediate(slot));
      ++slot;
    }
  } else {
    // Push any stack arguments.
    int stack_decrement = 0;
    for (PushParameter input : base::Reversed(*arguments)) {
      stack_decrement += kSystemPointerSize;
      // Skip any alignment holes in pushed nodes.
      if (!this->valid(input.node)) continue;
      InstructionOperand decrement = g.UseImmediate(stack_decrement);
      stack_decrement = 0;
      Emit(kPPC_Push, g.NoOutput(), decrement, g.UseRegister(input.node));
    }
  }
}

template <typename Adapter>
bool InstructionSelectorT<Adapter>::IsTailCallAddressImmediate() {
  return false;
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64ExtractLowWord32(node_t node) {
  PPCOperandGeneratorT<Adapter> g(this);
  Emit(kPPC_DoubleExtractLowWord32, g.DefineAsRegister(node),
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64ExtractHighWord32(node_t node) {
  PPCOperandGeneratorT<Adapter> g(this);
  Emit(kPPC_DoubleExtractHighWord32, g.DefineAsRegister(node),
       g.UseRegister(this->input_at(node, 0)));
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitBitcastWord32PairToFloat64(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  PPCOperandGeneratorT<TurboshaftAdapter> g(this);
  const auto& bitcast = this->Cast<BitcastWord32PairToFloat64Op>(node);
  node_t hi = bitcast.high_word32();
  node_t lo = bitcast.low_word32();

  InstructionOperand temps[] = {g.TempRegister()};
  Emit(kPPC_DoubleFromWord32Pair, g.DefineAsRegister(node), g.UseRegister(hi),
       g.UseRegister(lo), arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64InsertLowWord32(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
  UNIMPLEMENTED();
  } else {
  PPCOperandGeneratorT<Adapter> g(this);
  Node* left = node->InputAt(0);
  Node* right = node->InputAt(1);
  if (left->opcode() == IrOpcode::kFloat64InsertHighWord32 &&
      CanCover(node, left)) {
    left = left->InputAt(1);
    Emit(kPPC_DoubleConstruct, g.DefineAsRegister(node), g.UseRegister(left),
         g.UseRegister(right));
    return;
  }
  Emit(kPPC_DoubleInsertLowWord32, g.DefineSameAsFirst(node),
       g.UseRegister(left), g.UseRegister(right));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64InsertHighWord32(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
  UNIMPLEMENTED();
  } else {
  PPCOperandGeneratorT<Adapter> g(this);
  Node* left = node->InputAt(0);
  Node* right = node->InputAt(1);
  if (left->opcode() == IrOpcode::kFloat64InsertLowWord32 &&
      CanCover(node, left)) {
    left = left->InputAt(1);
    Emit(kPPC_DoubleConstruct, g.DefineAsRegister(node), g.UseRegister(right),
         g.UseRegister(left));
    return;
  }
  Emit(kPPC_DoubleInsertHighWord32, g.DefineSameAsFirst(node),
       g.UseRegister(left), g.UseRegister(right));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitMemoryBarrier(node_t node) {
  PPCOperandGeneratorT<Adapter> g(this);
  Emit(kPPC_Sync, g.NoOutput());
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicLoad(node_t node) {
  auto load = this->load_view(node);
  LoadRepresentation load_rep = load.loaded_rep();
  VisitLoadCommon(this, node, load_rep);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64AtomicLoad(node_t node) {
  auto load = this->load_view(node);
  LoadRepresentation load_rep = load.loaded_rep();
  VisitLoadCommon(this, node, load_rep);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicStore(node_t node) {
  auto store = this->store_view(node);
  AtomicStoreParameters store_params(store.stored_rep().representation(),
                                     store.stored_rep().write_barrier_kind(),
                                     store.memory_order().value(),
                                     store.access_kind());
  VisitStoreCommon(this, node, store_params.store_representation(),
                   store_params.order());
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64AtomicStore(node_t node) {
  auto store = this->store_view(node);
  AtomicStoreParameters store_params(store.stored_rep().representation(),
                                     store.stored_rep().write_barrier_kind(),
                                     store.memory_order().value(),
                                     store.access_kind());
  VisitStoreCommon(this, node, store_params.store_representation(),
                   store_params.order());
}

template <typename Adapter>
void VisitAtomicExchange(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node, ArchOpcode opcode) {
  using node_t = typename Adapter::node_t;
  PPCOperandGeneratorT<Adapter> g(selector);
  auto atomic_op = selector->atomic_rmw_view(node);
  node_t base = atomic_op.base();
  node_t index = atomic_op.index();
  node_t value = atomic_op.value();

  AddressingMode addressing_mode = kMode_MRR;
  InstructionOperand inputs[3];
  size_t input_count = 0;
  inputs[input_count++] = g.UseUniqueRegister(base);
  inputs[input_count++] = g.UseUniqueRegister(index);
  inputs[input_count++] = g.UseUniqueRegister(value);
  InstructionOperand outputs[1];
  outputs[0] = g.UseUniqueRegister(node);
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode);
  selector->Emit(code, 1, outputs, input_count, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicExchange(node_t node) {
  ArchOpcode opcode;
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
    if (atomic_op.memory_rep == MemoryRepresentation::Int8()) {
      opcode = kAtomicExchangeInt8;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
      opcode = kPPC_AtomicExchangeUint8;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Int16()) {
      opcode = kAtomicExchangeInt16;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
      opcode = kPPC_AtomicExchangeUint16;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Int32() ||
               atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
      opcode = kPPC_AtomicExchangeWord32;
    } else {
      UNREACHABLE();
    }
  } else {
    MachineType type = AtomicOpType(node->op());
    if (type == MachineType::Int8()) {
      opcode = kAtomicExchangeInt8;
    } else if (type == MachineType::Uint8()) {
      opcode = kPPC_AtomicExchangeUint8;
    } else if (type == MachineType::Int16()) {
      opcode = kAtomicExchangeInt16;
    } else if (type == MachineType::Uint16()) {
      opcode = kPPC_AtomicExchangeUint16;
    } else if (type == MachineType::Int32() || type == MachineType::Uint32()) {
      opcode = kPPC_AtomicExchangeWord32;
    } else {
      UNREACHABLE();
    }
  }
  VisitAtomicExchange(this, node, opcode);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64AtomicExchange(node_t node) {
  ArchOpcode opcode;
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
    if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
      opcode = kPPC_AtomicExchangeUint8;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
      opcode = kPPC_AtomicExchangeUint16;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
      opcode = kPPC_AtomicExchangeWord32;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint64()) {
      opcode = kPPC_AtomicExchangeWord64;
    } else {
      UNREACHABLE();
    }
  } else {
    MachineType type = AtomicOpType(node->op());
    if (type == MachineType::Uint8()) {
      opcode = kPPC_AtomicExchangeUint8;
    } else if (type == MachineType::Uint16()) {
      opcode = kPPC_AtomicExchangeUint16;
    } else if (type == MachineType::Uint32()) {
      opcode = kPPC_AtomicExchangeWord32;
    } else if (type == MachineType::Uint64()) {
      opcode = kPPC_AtomicExchangeWord64;
    } else {
      UNREACHABLE();
    }
  }
  VisitAtomicExchange(this, node, opcode);
}

template <typename Adapter>
void VisitAtomicCompareExchange(InstructionSelectorT<Adapter>* selector,
                                typename Adapter::node_t node,
                                ArchOpcode opcode) {
  using node_t = typename Adapter::node_t;
  PPCOperandGeneratorT<Adapter> g(selector);
  auto atomic_op = selector->atomic_rmw_view(node);
  node_t base = atomic_op.base();
  node_t index = atomic_op.index();
  node_t old_value = atomic_op.expected();
  node_t new_value = atomic_op.value();

  AddressingMode addressing_mode = kMode_MRR;
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode);

  InstructionOperand inputs[4];
  size_t input_count = 0;
  inputs[input_count++] = g.UseUniqueRegister(base);
  inputs[input_count++] = g.UseUniqueRegister(index);
  inputs[input_count++] = g.UseUniqueRegister(old_value);
  inputs[input_count++] = g.UseUniqueRegister(new_value);

  InstructionOperand outputs[1];
  size_t output_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  selector->Emit(code, output_count, outputs, input_count, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicCompareExchange(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  ArchOpcode opcode;
  if constexpr (Adapter::IsTurboshaft) {
    const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
    if (atomic_op.memory_rep == MemoryRepresentation::Int8()) {
      opcode = kAtomicCompareExchangeInt8;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
      opcode = kPPC_AtomicCompareExchangeUint8;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Int16()) {
      opcode = kAtomicCompareExchangeInt16;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
      opcode = kPPC_AtomicCompareExchangeUint16;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Int32() ||
               atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
      opcode = kPPC_AtomicCompareExchangeWord32;
    } else {
      UNREACHABLE();
    }
  } else {
    MachineType type = AtomicOpType(node->op());
    if (type == MachineType::Int8()) {
      opcode = kAtomicCompareExchangeInt8;
    } else if (type == MachineType::Uint8()) {
      opcode = kPPC_AtomicCompareExchangeUint8;
    } else if (type == MachineType::Int16()) {
      opcode = kAtomicCompareExchangeInt16;
    } else if (type == MachineType::Uint16()) {
      opcode = kPPC_AtomicCompareExchangeUint16;
    } else if (type == MachineType::Int32() || type == MachineType::Uint32()) {
      opcode = kPPC_AtomicCompareExchangeWord32;
    } else {
      UNREACHABLE();
    }
  }
  VisitAtomicCompareExchange(this, node, opcode);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64AtomicCompareExchange(
    node_t node) {
  ArchOpcode opcode;
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
    if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
      opcode = kPPC_AtomicCompareExchangeUint8;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
      opcode = kPPC_AtomicCompareExchangeUint16;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
      opcode = kPPC_AtomicCompareExchangeWord32;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint64()) {
      opcode = kPPC_AtomicCompareExchangeWord64;
    } else {
      UNREACHABLE();
    }
  } else {
    MachineType type = AtomicOpType(node->op());
    if (type == MachineType::Uint8()) {
      opcode = kPPC_AtomicCompareExchangeUint8;
    } else if (type == MachineType::Uint16()) {
      opcode = kPPC_AtomicCompareExchangeUint16;
    } else if (type == MachineType::Uint32()) {
      opcode = kPPC_AtomicCompareExchangeWord32;
    } else if (type == MachineType::Uint64()) {
      opcode = kPPC_AtomicCompareExchangeWord64;
    } else {
      UNREACHABLE();
    }
  }
  VisitAtomicCompareExchange(this, node, opcode);
}

template <typename Adapter>
void VisitAtomicBinaryOperation(InstructionSelectorT<Adapter>* selector,
                                typename Adapter::node_t node,
                                ArchOpcode int8_op, ArchOpcode uint8_op,
                                ArchOpcode int16_op, ArchOpcode uint16_op,
                                ArchOpcode int32_op, ArchOpcode uint32_op,
                                ArchOpcode int64_op, ArchOpcode uint64_op) {
  using node_t = typename Adapter::node_t;
  PPCOperandGeneratorT<Adapter> g(selector);
  auto atomic_op = selector->atomic_rmw_view(node);
  node_t base = atomic_op.base();
  node_t index = atomic_op.index();
  node_t value = atomic_op.value();

  ArchOpcode opcode;
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const AtomicRMWOp& atomic_op =
        selector->Get(node).template Cast<AtomicRMWOp>();
    if (atomic_op.memory_rep == MemoryRepresentation::Int8()) {
      opcode = int8_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
      opcode = uint8_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Int16()) {
      opcode = int16_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
      opcode = uint16_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Int32()) {
      opcode = int32_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
      opcode = uint32_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Int64()) {
      opcode = int64_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint64()) {
      opcode = uint64_op;
    } else {
      UNREACHABLE();
    }
  } else {
    MachineType type = AtomicOpType(node->op());
    if (type == MachineType::Int8()) {
      opcode = int8_op;
    } else if (type == MachineType::Uint8()) {
      opcode = uint8_op;
    } else if (type == MachineType::Int16()) {
      opcode = int16_op;
    } else if (type == MachineType::Uint16()) {
      opcode = uint16_op;
    } else if (type == MachineType::Int32()) {
      opcode = int32_op;
    } else if (type == MachineType::Uint32()) {
      opcode = uint32_op;
    } else if (type == MachineType::Int64()) {
      opcode = int64_op;
    } else if (type == MachineType::Uint64()) {
      opcode = uint64_op;
    } else {
      UNREACHABLE();
    }
  }

  AddressingMode addressing_mode = kMode_MRR;
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode);
  InstructionOperand inputs[3];

  size_t input_count = 0;
  inputs[input_count++] = g.UseUniqueRegister(base);
  inputs[input_count++] = g.UseUniqueRegister(index);
  inputs[input_count++] = g.UseUniqueRegister(value);

  InstructionOperand outputs[1];
  size_t output_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  selector->Emit(code, output_count, outputs, input_count, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicBinaryOperation(
    node_t node, ArchOpcode int8_op, ArchOpcode uint8_op, ArchOpcode int16_op,
    ArchOpcode uint16_op, ArchOpcode word32_op) {
  // Unused
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64AtomicBinaryOperation(
    node_t node, ArchOpcode uint8_op, ArchOpcode uint16_op,
    ArchOpcode uint32_op, ArchOpcode uint64_op) {
  // Unused
  UNREACHABLE();
}

#define VISIT_ATOMIC_BINOP(op)                                             \
  template <typename Adapter>                                              \
  void InstructionSelectorT<Adapter>::VisitWord32Atomic##op(node_t node) { \
      VisitAtomicBinaryOperation(                                          \
          this, node, kPPC_Atomic##op##Int8, kPPC_Atomic##op##Uint8,       \
          kPPC_Atomic##op##Int16, kPPC_Atomic##op##Uint16,                 \
          kPPC_Atomic##op##Int32, kPPC_Atomic##op##Uint32,                 \
          kPPC_Atomic##op##Int64, kPPC_Atomic##op##Uint64);                \
  }                                                                        \
  template <typename Adapter>                                              \
  void InstructionSelectorT<Adapter>::VisitWord64Atomic##op(node_t node) { \
      VisitAtomicBinaryOperation(                                          \
          this, node, kPPC_Atomic##op##Int8, kPPC_Atomic##op##Uint8,       \
          kPPC_Atomic##op##Int16, kPPC_Atomic##op##Uint16,                 \
          kPPC_Atomic##op##Int32, kPPC_Atomic##op##Uint32,                 \
          kPPC_Atomic##op##Int64, kPPC_Atomic##op##Uint64);                \
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

#define SIMD_TYPES(V) \
  V(F64x2)            \
  V(F32x4)            \
  V(I64x2)            \
  V(I32x4)            \
  V(I16x8)            \
  V(I8x16)

#define SIMD_BINOP_LIST(V) \
  V(F64x2Add)              \
  V(F64x2Sub)              \
  V(F64x2Mul)              \
  V(F64x2Eq)               \
  V(F64x2Ne)               \
  V(F64x2Le)               \
  V(F64x2Lt)               \
  V(F64x2Div)              \
  V(F64x2Min)              \
  V(F64x2Max)              \
  V(F64x2Pmin)             \
  V(F64x2Pmax)             \
  V(F32x4Add)              \
  V(F32x4Sub)              \
  V(F32x4Mul)              \
  V(F32x4Eq)               \
  V(F32x4Ne)               \
  V(F32x4Lt)               \
  V(F32x4Le)               \
  V(F32x4Div)              \
  V(F32x4Min)              \
  V(F32x4Max)              \
  V(F32x4Pmin)             \
  V(F32x4Pmax)             \
  V(I64x2Add)              \
  V(I64x2Sub)              \
  V(I64x2Mul)              \
  V(I64x2Eq)               \
  V(I64x2Ne)               \
  V(I64x2ExtMulLowI32x4S)  \
  V(I64x2ExtMulHighI32x4S) \
  V(I64x2ExtMulLowI32x4U)  \
  V(I64x2ExtMulHighI32x4U) \
  V(I64x2GtS)              \
  V(I64x2GeS)              \
  V(I64x2Shl)              \
  V(I64x2ShrS)             \
  V(I64x2ShrU)             \
  V(I32x4Add)              \
  V(I32x4Sub)              \
  V(I32x4Mul)              \
  V(I32x4MinS)             \
  V(I32x4MinU)             \
  V(I32x4MaxS)             \
  V(I32x4MaxU)             \
  V(I32x4Eq)               \
  V(I32x4Ne)               \
  V(I32x4GtS)              \
  V(I32x4GeS)              \
  V(I32x4GtU)              \
  V(I32x4GeU)              \
  V(I32x4DotI16x8S)        \
  V(I32x4ExtMulLowI16x8S)  \
  V(I32x4ExtMulHighI16x8S) \
  V(I32x4ExtMulLowI16x8U)  \
  V(I32x4ExtMulHighI16x8U) \
  V(I32x4Shl)              \
  V(I32x4ShrS)             \
  V(I32x4ShrU)             \
  V(I16x8Add)              \
  V(I16x8Sub)              \
  V(I16x8Mul)              \
  V(I16x8MinS)             \
  V(I16x8MinU)             \
  V(I16x8MaxS)             \
  V(I16x8MaxU)             \
  V(I16x8Eq)               \
  V(I16x8Ne)               \
  V(I16x8GtS)              \
  V(I16x8GeS)              \
  V(I16x8GtU)              \
  V(I16x8GeU)              \
  V(I16x8SConvertI32x4)    \
  V(I16x8UConvertI32x4)    \
  V(I16x8AddSatS)          \
  V(I16x8SubSatS)          \
  V(I16x8AddSatU)          \
  V(I16x8SubSatU)          \
  V(I16x8RoundingAverageU) \
  V(I16x8Q15MulRSatS)      \
  V(I16x8ExtMulLowI8x16S)  \
  V(I16x8ExtMulHighI8x16S) \
  V(I16x8ExtMulLowI8x16U)  \
  V(I16x8ExtMulHighI8x16U) \
  V(I16x8Shl)              \
  V(I16x8ShrS)             \
  V(I16x8ShrU)             \
  V(I8x16Add)              \
  V(I8x16Sub)              \
  V(I8x16MinS)             \
  V(I8x16MinU)             \
  V(I8x16MaxS)             \
  V(I8x16MaxU)             \
  V(I8x16Eq)               \
  V(I8x16Ne)               \
  V(I8x16GtS)              \
  V(I8x16GeS)              \
  V(I8x16GtU)              \
  V(I8x16GeU)              \
  V(I8x16SConvertI16x8)    \
  V(I8x16UConvertI16x8)    \
  V(I8x16AddSatS)          \
  V(I8x16SubSatS)          \
  V(I8x16AddSatU)          \
  V(I8x16SubSatU)          \
  V(I8x16RoundingAverageU) \
  V(I8x16Swizzle)          \
  V(I8x16Shl)              \
  V(I8x16ShrS)             \
  V(I8x16ShrU)             \
  V(S128And)               \
  V(S128Or)                \
  V(S128Xor)               \
  V(S128AndNot)

#define SIMD_UNOP_LIST(V)      \
  V(F64x2Abs)                  \
  V(F64x2Neg)                  \
  V(F64x2Sqrt)                 \
  V(F64x2Ceil)                 \
  V(F64x2Floor)                \
  V(F64x2Trunc)                \
  V(F64x2ConvertLowI32x4S)     \
  V(F64x2ConvertLowI32x4U)     \
  V(F64x2PromoteLowF32x4)      \
  V(F64x2Splat)                \
  V(F32x4Abs)                  \
  V(F32x4Neg)                  \
  V(F32x4Sqrt)                 \
  V(F32x4SConvertI32x4)        \
  V(F32x4UConvertI32x4)        \
  V(F32x4Ceil)                 \
  V(F32x4Floor)                \
  V(F32x4Trunc)                \
  V(F32x4DemoteF64x2Zero)      \
  V(F32x4Splat)                \
  V(I64x2Abs)                  \
  V(I64x2Neg)                  \
  V(I64x2SConvertI32x4Low)     \
  V(I64x2SConvertI32x4High)    \
  V(I64x2UConvertI32x4Low)     \
  V(I64x2UConvertI32x4High)    \
  V(I64x2AllTrue)              \
  V(I64x2BitMask)              \
  V(I32x4Neg)                  \
  V(I64x2Splat)                \
  V(I32x4Abs)                  \
  V(I32x4SConvertF32x4)        \
  V(I32x4UConvertF32x4)        \
  V(I32x4SConvertI16x8Low)     \
  V(I32x4SConvertI16x8High)    \
  V(I32x4UConvertI16x8Low)     \
  V(I32x4UConvertI16x8High)    \
  V(I32x4ExtAddPairwiseI16x8S) \
  V(I32x4ExtAddPairwiseI16x8U) \
  V(I32x4TruncSatF64x2SZero)   \
  V(I32x4TruncSatF64x2UZero)   \
  V(I32x4AllTrue)              \
  V(I32x4BitMask)              \
  V(I32x4Splat)                \
  V(I16x8Neg)                  \
  V(I16x8Abs)                  \
  V(I16x8AllTrue)              \
  V(I16x8BitMask)              \
  V(I16x8Splat)                \
  V(I8x16Neg)                  \
  V(I8x16Abs)                  \
  V(I8x16Popcnt)               \
  V(I8x16AllTrue)              \
  V(I8x16BitMask)              \
  V(I8x16Splat)                \
  V(I16x8SConvertI8x16Low)     \
  V(I16x8SConvertI8x16High)    \
  V(I16x8UConvertI8x16Low)     \
  V(I16x8UConvertI8x16High)    \
  V(I16x8ExtAddPairwiseI8x16S) \
  V(I16x8ExtAddPairwiseI8x16U) \
  V(S128Not)                   \
  V(V128AnyTrue)

#define SIMD_VISIT_EXTRACT_LANE(Type, T, Sign, LaneSize)                   \
  template <typename Adapter>                                              \
  void InstructionSelectorT<Adapter>::Visit##Type##ExtractLane##Sign(      \
      node_t node) {                                                       \
    PPCOperandGeneratorT<Adapter> g(this);                                 \
    int32_t lane;                                                          \
    if constexpr (Adapter::IsTurboshaft) {                                 \
      using namespace turboshaft; /* NOLINT(build/namespaces) */           \
      const Operation& op = this->Get(node);                               \
      lane = op.template Cast<Simd128ExtractLaneOp>().lane;                \
    } else {                                                               \
      lane = OpParameter<int32_t>(node->op());                             \
    }                                                                      \
    Emit(kPPC_##T##ExtractLane##Sign | LaneSizeField::encode(LaneSize),    \
         g.DefineAsRegister(node), g.UseRegister(this->input_at(node, 0)), \
         g.UseImmediate(lane));                                            \
  }
SIMD_VISIT_EXTRACT_LANE(F64x2, F, , 64)
SIMD_VISIT_EXTRACT_LANE(F32x4, F, , 32)
SIMD_VISIT_EXTRACT_LANE(I64x2, I, , 64)
SIMD_VISIT_EXTRACT_LANE(I32x4, I, , 32)
SIMD_VISIT_EXTRACT_LANE(I16x8, I, U, 16)
SIMD_VISIT_EXTRACT_LANE(I16x8, I, S, 16)
SIMD_VISIT_EXTRACT_LANE(I8x16, I, U, 8)
SIMD_VISIT_EXTRACT_LANE(I8x16, I, S, 8)
#undef SIMD_VISIT_EXTRACT_LANE

#define SIMD_VISIT_REPLACE_LANE(Type, T, LaneSize)                            \
  template <typename Adapter>                                                 \
  void InstructionSelectorT<Adapter>::Visit##Type##ReplaceLane(node_t node) { \
    PPCOperandGeneratorT<Adapter> g(this);                                    \
    int32_t lane;                                                             \
    if constexpr (Adapter::IsTurboshaft) {                                    \
      using namespace turboshaft; /* NOLINT(build/namespaces) */              \
      const Operation& op = this->Get(node);                                  \
      lane = op.template Cast<Simd128ReplaceLaneOp>().lane;                   \
    } else {                                                                  \
      lane = OpParameter<int32_t>(node->op());                                \
    }                                                                         \
    Emit(kPPC_##T##ReplaceLane | LaneSizeField::encode(LaneSize),             \
         g.DefineSameAsFirst(node), g.UseRegister(this->input_at(node, 0)),   \
         g.UseImmediate(lane), g.UseRegister(this->input_at(node, 1)));       \
  }
SIMD_VISIT_REPLACE_LANE(F64x2, F, 64)
SIMD_VISIT_REPLACE_LANE(F32x4, F, 32)
SIMD_VISIT_REPLACE_LANE(I64x2, I, 64)
SIMD_VISIT_REPLACE_LANE(I32x4, I, 32)
SIMD_VISIT_REPLACE_LANE(I16x8, I, 16)
SIMD_VISIT_REPLACE_LANE(I8x16, I, 8)
#undef SIMD_VISIT_REPLACE_LANE

#define SIMD_VISIT_BINOP(Opcode)                                           \
  template <typename Adapter>                                              \
  void InstructionSelectorT<Adapter>::Visit##Opcode(node_t node) {         \
    PPCOperandGeneratorT<Adapter> g(this);                                 \
    InstructionOperand temps[] = {g.TempRegister()};                       \
    Emit(kPPC_##Opcode, g.DefineAsRegister(node),                          \
         g.UseRegister(this->input_at(node, 0)),                           \
         g.UseRegister(this->input_at(node, 1)), arraysize(temps), temps); \
  }
SIMD_BINOP_LIST(SIMD_VISIT_BINOP)
#undef SIMD_VISIT_BINOP
#undef SIMD_BINOP_LIST

#define SIMD_VISIT_UNOP(Opcode)                                    \
  template <typename Adapter>                                      \
  void InstructionSelectorT<Adapter>::Visit##Opcode(node_t node) { \
    PPCOperandGeneratorT<Adapter> g(this);                         \
    Emit(kPPC_##Opcode, g.DefineAsRegister(node),                  \
         g.UseRegister(this->input_at(node, 0)));                  \
  }
SIMD_UNOP_LIST(SIMD_VISIT_UNOP)
#undef SIMD_VISIT_UNOP
#undef SIMD_UNOP_LIST

#define SIMD_VISIT_QFMOP(Opcode)                                   \
  template <typename Adapter>                                      \
  void InstructionSelectorT<Adapter>::Visit##Opcode(node_t node) { \
    PPCOperandGeneratorT<Adapter> g(this);                         \
    Emit(kPPC_##Opcode, g.DefineSameAsFirst(node),                 \
         g.UseRegister(this->input_at(node, 0)),                   \
         g.UseRegister(this->input_at(node, 1)),                   \
         g.UseRegister(this->input_at(node, 2)));                  \
  }
SIMD_VISIT_QFMOP(F64x2Qfma)
SIMD_VISIT_QFMOP(F64x2Qfms)
SIMD_VISIT_QFMOP(F32x4Qfma)
SIMD_VISIT_QFMOP(F32x4Qfms)
#undef SIMD_VISIT_QFMOP

#define SIMD_RELAXED_OP_LIST(V)                           \
  V(F64x2RelaxedMin, F64x2Pmin)                           \
  V(F64x2RelaxedMax, F64x2Pmax)                           \
  V(F32x4RelaxedMin, F32x4Pmin)                           \
  V(F32x4RelaxedMax, F32x4Pmax)                           \
  V(I32x4RelaxedTruncF32x4S, I32x4SConvertF32x4)          \
  V(I32x4RelaxedTruncF32x4U, I32x4UConvertF32x4)          \
  V(I32x4RelaxedTruncF64x2SZero, I32x4TruncSatF64x2SZero) \
  V(I32x4RelaxedTruncF64x2UZero, I32x4TruncSatF64x2UZero) \
  V(I16x8RelaxedQ15MulRS, I16x8Q15MulRSatS)               \
  V(I8x16RelaxedLaneSelect, S128Select)                   \
  V(I16x8RelaxedLaneSelect, S128Select)                   \
  V(I32x4RelaxedLaneSelect, S128Select)                   \
  V(I64x2RelaxedLaneSelect, S128Select)

#define SIMD_VISIT_RELAXED_OP(name, op)                          \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##name(node_t node) { \
    Visit##op(node);                                             \
  }
SIMD_RELAXED_OP_LIST(SIMD_VISIT_RELAXED_OP)
#undef SIMD_VISIT_RELAXED_OP
#undef SIMD_RELAXED_OP_LIST

#define F16_OP_LIST(V)    \
  V(F16x8Splat)           \
  V(F16x8ExtractLane)     \
  V(F16x8ReplaceLane)     \
  V(F16x8Abs)             \
  V(F16x8Neg)             \
  V(F16x8Sqrt)            \
  V(F16x8Floor)           \
  V(F16x8Ceil)            \
  V(F16x8Trunc)           \
  V(F16x8NearestInt)      \
  V(F16x8Add)             \
  V(F16x8Sub)             \
  V(F16x8Mul)             \
  V(F16x8Div)             \
  V(F16x8Min)             \
  V(F16x8Max)             \
  V(F16x8Pmin)            \
  V(F16x8Pmax)            \
  V(F16x8Eq)              \
  V(F16x8Ne)              \
  V(F16x8Lt)              \
  V(F16x8Le)              \
  V(F16x8SConvertI16x8)   \
  V(F16x8UConvertI16x8)   \
  V(I16x8SConvertF16x8)   \
  V(I16x8UConvertF16x8)   \
  V(F32x4PromoteLowF16x8) \
  V(F16x8DemoteF32x4Zero) \
  V(F16x8DemoteF64x2Zero) \
  V(F16x8Qfma)            \
  V(F16x8Qfms)

#define VISIT_F16_OP(name)                                       \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##name(node_t node) { \
    UNIMPLEMENTED();                                             \
  }
F16_OP_LIST(VISIT_F16_OP)
#undef VISIT_F16_OP
#undef F16_OP_LIST
#undef SIMD_TYPES

#if V8_ENABLE_WEBASSEMBLY
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16Shuffle(node_t node) {
    uint8_t shuffle[kSimd128Size];
    bool is_swizzle;
    // TODO(nicohartmann@): Properly use view here once Turboshaft support is
    // implemented.
    auto view = this->simd_shuffle_view(node);
    CanonicalizeShuffle(view, shuffle, &is_swizzle);
    PPCOperandGeneratorT<Adapter> g(this);
    node_t input0 = view.input(0);
    node_t input1 = view.input(1);
    // Remap the shuffle indices to match IBM lane numbering.
    int max_index = 15;
    int total_lane_count = 2 * kSimd128Size;
    uint8_t shuffle_remapped[kSimd128Size];
    for (int i = 0; i < kSimd128Size; i++) {
      uint8_t current_index = shuffle[i];
      shuffle_remapped[i] =
          (current_index <= max_index
               ? max_index - current_index
               : total_lane_count - current_index + max_index);
    }
    Emit(kPPC_I8x16Shuffle, g.DefineAsRegister(node), g.UseRegister(input0),
         g.UseRegister(input1),
         g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle_remapped)),
         g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle_remapped + 4)),
         g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle_remapped + 8)),
         g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle_remapped + 12)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSetStackPointer(node_t node) {
  OperandGenerator g(this);
  // TODO(miladfarca): Optimize by using UseAny.
  auto input = g.UseRegister(this->input_at(node, 0));
  Emit(kArchSetStackPointer, 0, nullptr, 1, &input);
}

#else
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16Shuffle(node_t node) {
  UNREACHABLE();
}
#endif  // V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Zero(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    Emit(kPPC_S128Zero, g.DefineAsRegister(node));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Select(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    Emit(kPPC_S128Select, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)),
         g.UseRegister(this->input_at(node, 2)));
}

// This is a replica of SimdShuffle::Pack4Lanes. However, above function will
// not be available on builds with webassembly disabled, hence we need to have
// it declared locally as it is used on other visitors such as S128Const.
static int32_t Pack4Lanes(const uint8_t* shuffle) {
  int32_t result = 0;
  for (int i = 3; i >= 0; --i) {
    result <<= 8;
    result |= shuffle[i];
  }
  return result;
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Const(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    uint32_t val[kSimd128Size / sizeof(uint32_t)];
    if constexpr (Adapter::IsTurboshaft) {
      const turboshaft::Simd128ConstantOp& constant =
          this->Get(node).template Cast<turboshaft::Simd128ConstantOp>();
      memcpy(val, constant.value, kSimd128Size);
    } else {
      memcpy(val, S128ImmediateParameterOf(node->op()).data(), kSimd128Size);
    }
    // If all bytes are zeros, avoid emitting code for generic constants.
    bool all_zeros = !(val[0] || val[1] || val[2] || val[3]);
    bool all_ones = val[0] == UINT32_MAX && val[1] == UINT32_MAX &&
                    val[2] == UINT32_MAX && val[3] == UINT32_MAX;
    InstructionOperand dst = g.DefineAsRegister(node);
    if (all_zeros) {
      Emit(kPPC_S128Zero, dst);
    } else if (all_ones) {
      Emit(kPPC_S128AllOnes, dst);
    } else {
      // We have to use Pack4Lanes to reverse the bytes (lanes) on BE,
      // Which in this case is ineffective on LE.
      Emit(
          kPPC_S128Const, g.DefineAsRegister(node),
          g.UseImmediate(Pack4Lanes(reinterpret_cast<uint8_t*>(&val[0]))),
          g.UseImmediate(Pack4Lanes(reinterpret_cast<uint8_t*>(&val[0]) + 4)),
          g.UseImmediate(Pack4Lanes(reinterpret_cast<uint8_t*>(&val[0]) + 8)),
          g.UseImmediate(Pack4Lanes(reinterpret_cast<uint8_t*>(&val[0]) + 12)));
    }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8DotI8x16I7x16S(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    Emit(kPPC_I16x8DotI8x16S, g.DefineAsRegister(node),
         g.UseUniqueRegister(this->input_at(node, 0)),
         g.UseUniqueRegister(this->input_at(node, 1)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4DotI8x16I7x16AddS(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    Emit(kPPC_I32x4DotI8x16AddS, g.DefineAsRegister(node),
         g.UseUniqueRegister(this->input_at(node, 0)),
         g.UseUniqueRegister(this->input_at(node, 1)),
         g.UseUniqueRegister(this->input_at(node, 2)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitPrepareResults(
    ZoneVector<PushParameter>* results, const CallDescriptor* call_descriptor,
    node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);

    for (PushParameter output : *results) {
    if (!output.location.IsCallerFrameSlot()) continue;
    // Skip any alignment holes in nodes.
    if (this->valid(output.node)) {
      DCHECK(!call_descriptor->IsCFunctionCall());
      if (output.location.GetType() == MachineType::Float32()) {
        MarkAsFloat32(output.node);
      } else if (output.location.GetType() == MachineType::Float64()) {
        MarkAsFloat64(output.node);
      } else if (output.location.GetType() == MachineType::Simd128()) {
        MarkAsSimd128(output.node);
      }
      int offset = call_descriptor->GetOffsetToReturns();
      int reverse_slot = -output.location.GetLocation() - offset;
      Emit(kPPC_Peek, g.DefineAsRegister(output.node),
           g.UseImmediate(reverse_slot));
    }
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoadLane(node_t node) {
  PPCOperandGeneratorT<Adapter> g(this);
  InstructionCode opcode = kArchNop;
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Simd128LaneMemoryOp& load =
        this->Get(node).template Cast<Simd128LaneMemoryOp>();
    switch (load.lane_kind) {
      case Simd128LaneMemoryOp::LaneKind::k8:
        opcode = kPPC_S128Load8Lane;
        break;
      case Simd128LaneMemoryOp::LaneKind::k16:
        opcode = kPPC_S128Load16Lane;
        break;
      case Simd128LaneMemoryOp::LaneKind::k32:
        opcode = kPPC_S128Load32Lane;
        break;
      case Simd128LaneMemoryOp::LaneKind::k64:
        opcode = kPPC_S128Load64Lane;
        break;
    }
    Emit(opcode | AddressingModeField::encode(kMode_MRR),
         g.DefineSameAsFirst(node), g.UseRegister(load.value()),
         g.UseRegister(load.base()), g.UseRegister(load.index()),
         g.UseImmediate(load.lane));
  } else {
    LoadLaneParameters params = LoadLaneParametersOf(node->op());
    if (params.rep == MachineType::Int8()) {
      opcode = kPPC_S128Load8Lane;
    } else if (params.rep == MachineType::Int16()) {
      opcode = kPPC_S128Load16Lane;
    } else if (params.rep == MachineType::Int32()) {
      opcode = kPPC_S128Load32Lane;
    } else if (params.rep == MachineType::Int64()) {
      opcode = kPPC_S128Load64Lane;
    } else {
      UNREACHABLE();
    }
    Emit(opcode | AddressingModeField::encode(kMode_MRR),
         g.DefineSameAsFirst(node), g.UseRegister(node->InputAt(2)),
         g.UseRegister(node->InputAt(0)), g.UseRegister(node->InputAt(1)),
         g.UseImmediate(params.laneidx));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoadTransform(node_t node) {
  PPCOperandGeneratorT<Adapter> g(this);
  ArchOpcode opcode;
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Simd128LoadTransformOp& op =
        this->Get(node).template Cast<Simd128LoadTransformOp>();
    node_t base = op.base();
    node_t index = op.index();

    switch (op.transform_kind) {
      case Simd128LoadTransformOp::TransformKind::k8Splat:
        opcode = kPPC_S128Load8Splat;
        break;
      case Simd128LoadTransformOp::TransformKind::k16Splat:
        opcode = kPPC_S128Load16Splat;
        break;
      case Simd128LoadTransformOp::TransformKind::k32Splat:
        opcode = kPPC_S128Load32Splat;
        break;
      case Simd128LoadTransformOp::TransformKind::k64Splat:
        opcode = kPPC_S128Load64Splat;
        break;
      case Simd128LoadTransformOp::TransformKind::k8x8S:
        opcode = kPPC_S128Load8x8S;
        break;
      case Simd128LoadTransformOp::TransformKind::k8x8U:
        opcode = kPPC_S128Load8x8U;
        break;
      case Simd128LoadTransformOp::TransformKind::k16x4S:
        opcode = kPPC_S128Load16x4S;
        break;
      case Simd128LoadTransformOp::TransformKind::k16x4U:
        opcode = kPPC_S128Load16x4U;
        break;
      case Simd128LoadTransformOp::TransformKind::k32x2S:
        opcode = kPPC_S128Load32x2S;
        break;
      case Simd128LoadTransformOp::TransformKind::k32x2U:
        opcode = kPPC_S128Load32x2U;
        break;
      case Simd128LoadTransformOp::TransformKind::k32Zero:
        opcode = kPPC_S128Load32Zero;
        break;
      case Simd128LoadTransformOp::TransformKind::k64Zero:
        opcode = kPPC_S128Load64Zero;
        break;
      default:
        UNIMPLEMENTED();
    }
    Emit(opcode | AddressingModeField::encode(kMode_MRR),
         g.DefineAsRegister(node), g.UseRegister(base), g.UseRegister(index));
  } else {
    LoadTransformParameters params = LoadTransformParametersOf(node->op());
    PPCOperandGeneratorT<Adapter> g(this);
    Node* base = node->InputAt(0);
    Node* index = node->InputAt(1);

    switch (params.transformation) {
      case LoadTransformation::kS128Load8Splat:
        opcode = kPPC_S128Load8Splat;
        break;
      case LoadTransformation::kS128Load16Splat:
        opcode = kPPC_S128Load16Splat;
        break;
      case LoadTransformation::kS128Load32Splat:
        opcode = kPPC_S128Load32Splat;
        break;
      case LoadTransformation::kS128Load64Splat:
        opcode = kPPC_S128Load64Splat;
        break;
      case LoadTransformation::kS128Load8x8S:
        opcode = kPPC_S128Load8x8S;
        break;
      case LoadTransformation::kS128Load8x8U:
        opcode = kPPC_S128Load8x8U;
        break;
      case LoadTransformation::kS128Load16x4S:
        opcode = kPPC_S128Load16x4S;
        break;
      case LoadTransformation::kS128Load16x4U:
        opcode = kPPC_S128Load16x4U;
        break;
      case LoadTransformation::kS128Load32x2S:
        opcode = kPPC_S128Load32x2S;
        break;
      case LoadTransformation::kS128Load32x2U:
        opcode = kPPC_S128Load32x2U;
        break;
      case LoadTransformation::kS128Load32Zero:
        opcode = kPPC_S128Load32Zero;
        break;
      case LoadTransformation::kS128Load64Zero:
        opcode = kPPC_S128Load64Zero;
        break;
      default:
        UNREACHABLE();
    }
    Emit(opcode | AddressingModeField::encode(kMode_MRR),
         g.DefineAsRegister(node), g.UseRegister(base), g.UseRegister(index));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStoreLane(node_t node) {
    PPCOperandGeneratorT<Adapter> g(this);
    InstructionCode opcode = kArchNop;
    InstructionOperand inputs[4];
    if constexpr (Adapter::IsTurboshaft) {
      using namespace turboshaft;  // NOLINT(build/namespaces)
      const Simd128LaneMemoryOp& store =
          this->Get(node).template Cast<Simd128LaneMemoryOp>();
      switch (store.lane_kind) {
        case Simd128LaneMemoryOp::LaneKind::k8:
          opcode = kPPC_S128Store8Lane;
          break;
        case Simd128LaneMemoryOp::LaneKind::k16:
          opcode = kPPC_S128Store16Lane;
          break;
        case Simd128LaneMemoryOp::LaneKind::k32:
          opcode = kPPC_S128Store32Lane;
          break;
        case Simd128LaneMemoryOp::LaneKind::k64:
          opcode = kPPC_S128Store64Lane;
          break;
      }

      inputs[0] = g.UseRegister(store.value());
      inputs[1] = g.UseRegister(store.base());
      inputs[2] = g.UseRegister(store.index());
      inputs[3] = g.UseImmediate(store.lane);
    } else {
      StoreLaneParameters params = StoreLaneParametersOf(node->op());
      if (params.rep == MachineRepresentation::kWord8) {
        opcode = kPPC_S128Store8Lane;
      } else if (params.rep == MachineRepresentation::kWord16) {
        opcode = kPPC_S128Store16Lane;
      } else if (params.rep == MachineRepresentation::kWord32) {
        opcode = kPPC_S128Store32Lane;
      } else if (params.rep == MachineRepresentation::kWord64) {
        opcode = kPPC_S128Store64Lane;
      } else {
        UNREACHABLE();
      }

      inputs[0] = g.UseRegister(node->InputAt(2));
      inputs[1] = g.UseRegister(node->InputAt(0));
      inputs[2] = g.UseRegister(node->InputAt(1));
      inputs[3] = g.UseImmediate(params.laneidx);
    }
    Emit(opcode | AddressingModeField::encode(kMode_MRR), 0, nullptr, 4,
         inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::AddOutputToSelectContinuation(
    OperandGenerator* g, int first_input_index, node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32RoundTiesEven(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64RoundTiesEven(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2NearestInt(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4NearestInt(node_t node) {
  UNREACHABLE();
}

MachineOperatorBuilder::Flags
InstructionSelector::SupportedMachineOperatorFlags() {
  return MachineOperatorBuilder::kFloat32RoundDown |
         MachineOperatorBuilder::kFloat64RoundDown |
         MachineOperatorBuilder::kFloat32RoundUp |
         MachineOperatorBuilder::kFloat64RoundUp |
         MachineOperatorBuilder::kFloat32RoundTruncate |
         MachineOperatorBuilder::kFloat64RoundTruncate |
         MachineOperatorBuilder::kFloat64RoundTiesAway |
         MachineOperatorBuilder::kWord32Popcnt |
         MachineOperatorBuilder::kWord64Popcnt;
  // We omit kWord32ShiftIsSafe as s[rl]w use 0x3F as a mask rather than 0x1F.
}

MachineOperatorBuilder::AlignmentRequirements
InstructionSelector::AlignmentRequirements() {
  return MachineOperatorBuilder::AlignmentRequirements::
      FullUnalignedAccessSupport();
}

template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    InstructionSelectorT<TurbofanAdapter>;
template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    InstructionSelectorT<TurboshaftAdapter>;

}  // namespace compiler
}  // namespace internal
}  // namespace v8
