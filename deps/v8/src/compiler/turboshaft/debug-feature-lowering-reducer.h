// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_DEBUG_FEATURE_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_DEBUG_FEATURE_LOWERING_REDUCER_H_

#include "src/compiler/js-heap-broker.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/builtin-call-descriptors.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/representations.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <typename Next>
class DebugFeatureLoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(DebugFeatureLowering)

  OpIndex REDUCE(DebugPrint)(OpIndex input, RegisterRepresentation rep) {
    if (isolate_ != nullptr) {
      switch (rep.value()) {
        case RegisterRepresentation::WordPtr():
          __ CallBuiltin_DebugPrintWordPtr(isolate_, __ NoContextConstant(),
                                           input);
          break;
        case RegisterRepresentation::Float64():
          __ CallBuiltin_DebugPrintFloat64(isolate_, __ NoContextConstant(),
                                           input);
          break;
        case RegisterRepresentation::Tagged():
          __ CallRuntime_DebugPrint(isolate_, input);
          break;
        default:
          // TODO(nicohartmann@): Support other representations.
          UNIMPLEMENTED();
      }
    } else {
#if V8_ENABLE_WEBASSEMBLY
      DCHECK(__ data()->is_wasm());
      switch (rep.value()) {
        case RegisterRepresentation::Float64():
          __ template WasmCallBuiltinThroughJumptable<
              BuiltinCallDescriptor::DebugPrintFloat64>(__ NoContextConstant(),
                                                        {input});
          break;
        case RegisterRepresentation::WordPtr():
          __ template WasmCallBuiltinThroughJumptable<
              BuiltinCallDescriptor::DebugPrintWordPtr>(__ NoContextConstant(),
                                                        {input});
          break;
        default:
          // TODO(mliedtke): Support other representations.
          UNIMPLEMENTED();
      }
#else
      UNREACHABLE();
#endif
    }
    return {};
  }

  V<None> REDUCE(StaticAssert)(V<Word32> condition, const char* source) {
    // Static asserts should be (statically asserted and) removed by turboshaft.
    UnparkedScopeIfNeeded scope(broker_);
    AllowHandleDereference allow_handle_dereference;
    std::cout << __ output_graph().Get(condition);
    FATAL(
        "Expected Turbofan static assert to hold, but got non-true input:\n  "
        "%s",
        source);
  }

  OpIndex REDUCE(CheckTurboshaftTypeOf)(OpIndex input,
                                        RegisterRepresentation rep, Type type,
                                        bool successful) {
    if (successful) return input;

    UnparkedScopeIfNeeded scope(broker_);
    AllowHandleDereference allow_handle_dereference;
    FATAL("Checking type %s of operation %d:%s failed!",
          type.ToString().c_str(), input.id(),
          __ output_graph().Get(input).ToString().c_str());
  }

 private:
  Isolate* isolate_ = __ data() -> isolate();
  JSHeapBroker* broker_ = __ data() -> broker();
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_DEBUG_FEATURE_LOWERING_REDUCER_H_
