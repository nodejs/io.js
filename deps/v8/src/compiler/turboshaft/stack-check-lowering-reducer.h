// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_STACK_CHECK_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_STACK_CHECK_LOWERING_REDUCER_H_

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
class StackCheckLoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(StackCheckLowering)

  V<None> REDUCE(JSStackCheck)(V<Context> context,
                               OptionalV<FrameState> frame_state,
                               JSStackCheckOp::Kind kind) {
    switch (kind) {
      case JSStackCheckOp::Kind::kFunctionEntry: {
        // Loads of the stack limit should not be load-eliminated as it can be
        // modified by another thread.
        V<WordPtr> limit =
            __ Load(__ ExternalConstant(
                        ExternalReference::address_of_jslimit(isolate())),
                    LoadOp::Kind::RawAligned().NotLoadEliminable(),
                    MemoryRepresentation::UintPtr());

        IF_NOT (LIKELY(__ StackPointerGreaterThan(
                    limit, StackCheckKind::kJSFunctionEntry))) {
          __ CallRuntime_StackGuardWithGap(isolate(), frame_state.value(),
                                           context, __ StackCheckOffset());
        }
        break;
      }
      case JSStackCheckOp::Kind::kBuiltinEntry: {
        V<WordPtr> stack_limit = __ LoadOffHeap(
            __ ExternalConstant(
                ExternalReference::address_of_jslimit(isolate())),
            MemoryRepresentation::UintPtr());
        IF_NOT (LIKELY(__ StackPointerGreaterThan(
                    stack_limit, StackCheckKind::kCodeStubAssembler))) {
          __ CallRuntime_StackGuard(isolate(), context);
        }
        break;
      }
      case JSStackCheckOp::Kind::kLoop: {
        V<Word32> limit = __ Load(
            __ ExternalConstant(
                ExternalReference::address_of_no_heap_write_interrupt_request(
                    isolate())),
            LoadOp::Kind::RawAligned().NotLoadEliminable(),
            MemoryRepresentation::Uint8());

        IF_NOT (LIKELY(__ Word32Equal(limit, 0))) {
          __ CallRuntime_HandleNoHeapWritesInterrupts(
              isolate(), frame_state.value(), context);
        }
        break;
      }
    }

    return V<None>::Invalid();
  }

#ifdef V8_ENABLE_WEBASSEMBLY
  V<None> REDUCE(WasmStackCheck)(WasmStackCheckOp::Kind kind) {
    if (kind == WasmStackCheckOp::Kind::kFunctionEntry && __ IsLeafFunction()) {
      return V<None>::Invalid();
    }

    if (kind == WasmStackCheckOp::Kind::kFunctionEntry &&
        v8_flags.experimental_wasm_growable_stacks) {
      // WasmStackCheck should be lowered by GrowableStacksReducer
      // in a special way.
      return Next::ReduceWasmStackCheck(kind);
    }

    // Loads of the stack limit should not be load-eliminated as it can be
    // modified by another thread.
    V<WordPtr> limit = __ Load(
        __ LoadRootRegister(), LoadOp::Kind::RawAligned().NotLoadEliminable(),
        MemoryRepresentation::UintPtr(), IsolateData::jslimit_offset());

    IF_NOT (LIKELY(__ StackPointerGreaterThan(limit, StackCheckKind::kWasm))) {
      // TODO(14108): Cache descriptor.
      const CallDescriptor* call_descriptor =
          compiler::Linkage::GetStubCallDescriptor(
              __ graph_zone(),                      // zone
              NoContextDescriptor{},                // descriptor
              0,                                    // stack parameter count
              CallDescriptor::kNoFlags,             // flags
              Operator::kNoProperties,              // properties
              StubCallMode::kCallWasmRuntimeStub);  // stub call mode
      const TSCallDescriptor* ts_call_descriptor =
          TSCallDescriptor::Create(call_descriptor, compiler::CanThrow::kNo,
                                   LazyDeoptOnThrow::kNo, __ graph_zone());
      V<WordPtr> builtin =
          __ RelocatableWasmBuiltinCallTarget(Builtin::kWasmStackGuard);
      // Pass custom effects to the `Call` node to mark it as non-writing.
      __ Call(
          builtin, {}, ts_call_descriptor,
          OpEffects().CanReadMemory().RequiredWhenUnused().CanCreateIdentity());
    }

    return V<None>::Invalid();
  }
#endif  // V8_ENABLE_WEBASSEMBLY

 private:
  Isolate* isolate() {
    if (!isolate_) isolate_ = __ data() -> isolate();
    return isolate_;
  }

  Isolate* isolate_ = nullptr;
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_STACK_CHECK_LOWERING_REDUCER_H_
