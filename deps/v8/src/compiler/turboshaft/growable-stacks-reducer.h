// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_GROWABLE_STACKS_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_GROWABLE_STACKS_REDUCER_H_

#include "src/compiler/globals.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/uniform-reducer-adapter.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <class Next>
class GrowableStacksReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(GrowableStacks)

  GrowableStacksReducer() {
    if (!__ data()->wasm_module_sig() ||
        !v8_flags.experimental_wasm_growable_stacks) {
      // We are not compiling a wasm function if there is no signature.
      skip_reducer_ = true;
      return;
    }
    call_descriptor_ = compiler::GetWasmCallDescriptor(
        __ graph_zone(), __ data()->wasm_module_sig());
#if V8_TARGET_ARCH_32_BIT
    call_descriptor_ =
        compiler::GetI32WasmCallDescriptor(__ graph_zone(), call_descriptor_);
#endif
  }

  V<None> REDUCE(WasmStackCheck)(WasmStackCheckOp::Kind kind) {
    CHECK_EQ(kind, WasmStackCheckOp::Kind::kFunctionEntry);
    if (skip_reducer_) {
      return Next::ReduceWasmStackCheck(kind);
    }
    // Loads of the stack limit should not be load-eliminated as it can be
    // modified by another thread.
    V<WordPtr> limit = __ Load(
        __ LoadRootRegister(), LoadOp::Kind::RawAligned().NotLoadEliminable(),
        MemoryRepresentation::UintPtr(), IsolateData::jslimit_offset());

    IF_NOT (LIKELY(__ StackPointerGreaterThan(limit, StackCheckKind::kWasm))) {
      const int stack_parameter_count = 0;
      const CallDescriptor* stub_call_descriptor =
          compiler::Linkage::GetStubCallDescriptor(
              __ graph_zone(), WasmGrowableStackGuardDescriptor{},
              stack_parameter_count, CallDescriptor::kNoFlags,
              Operator::kNoProperties, StubCallMode::kCallWasmRuntimeStub);
      const TSCallDescriptor* ts_stub_call_descriptor =
          TSCallDescriptor::Create(stub_call_descriptor,
                                   compiler::CanThrow::kNo,
                                   LazyDeoptOnThrow::kNo, __ graph_zone());
      V<WordPtr> builtin =
          __ RelocatableWasmBuiltinCallTarget(Builtin::kWasmGrowableStackGuard);
      auto param_slots_size = __ IntPtrConstant(
          call_descriptor_->ParameterSlotCount() * kSystemPointerSize);
      __ Call(
          builtin, {param_slots_size}, ts_stub_call_descriptor,
          OpEffects().CanReadMemory().RequiredWhenUnused().CanCreateIdentity());
    }

    return V<None>::Invalid();
  }

  OpIndex REDUCE(Return)(V<Word32> pop_count,
                         base::Vector<const OpIndex> return_values,
                         bool spill_caller_frame_slots) {
    if (skip_reducer_ || !spill_caller_frame_slots ||
        call_descriptor_->ReturnSlotCount() == 0) {
      return Next::ReduceReturn(pop_count, return_values,
                                spill_caller_frame_slots);
    }
    V<Word32> frame_marker = __ Load(
        __ FramePointer(), LoadOp::Kind::RawAligned(),
        MemoryRepresentation::Uint32(), WasmFrameConstants::kFrameTypeOffset);

    Label<WordPtr> done(this);
    IF (UNLIKELY(__ Word32Equal(
            frame_marker,
            StackFrame::TypeToMarker(StackFrame::WASM_SEGMENT_START)))) {
      auto sig =
          FixedSizeSignature<MachineType>::Returns(MachineType::Pointer())
              .Params(MachineType::Pointer());
      const CallDescriptor* ccall_descriptor =
          compiler::Linkage::GetSimplifiedCDescriptor(__ graph_zone(), &sig);
      const TSCallDescriptor* ts_ccall_descriptor = TSCallDescriptor::Create(
          ccall_descriptor, compiler::CanThrow::kNo,
          compiler::LazyDeoptOnThrow::kNo, __ graph_zone());
      GOTO(done, __ template Call<WordPtr>(
                     __ ExternalConstant(ExternalReference::wasm_load_old_fp()),
                     OpIndex::Invalid(),
                     base::VectorOf({__ ExternalConstant(
                         ExternalReference::isolate_address())}),
                     ts_ccall_descriptor));
    } ELSE {
      GOTO(done, __ FramePointer());
    }
    BIND(done, old_fp);

    base::SmallVector<OpIndex, 8> register_return_values;
    for (size_t i = 0; i < call_descriptor_->ReturnCount(); i++) {
      LinkageLocation loc = call_descriptor_->GetReturnLocation(i);
      if (!loc.IsCallerFrameSlot()) {
        register_return_values.push_back(return_values[i]);
        continue;
      }
      __ Store(old_fp, return_values[i], StoreOp::Kind::RawAligned(),
               MemoryRepresentation::FromMachineType(loc.GetType()),
               compiler::kNoWriteBarrier,
               FrameSlotToFPOffset(loc.GetLocation()));
    }
    return Next::ReduceReturn(pop_count, base::VectorOf(register_return_values),
                              spill_caller_frame_slots);
  }

 private:
  bool skip_reducer_ = false;
  CallDescriptor* call_descriptor_ = nullptr;
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_GROWABLE_STACKS_REDUCER_H_
