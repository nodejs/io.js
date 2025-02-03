// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_ASSERT_TYPES_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_ASSERT_TYPES_REDUCER_H_

#include <limits>

#include "src/base/logging.h"
#include "src/base/template-utils.h"
#include "src/base/vector.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/frame.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/compiler/turboshaft/type-inference-reducer.h"
#include "src/compiler/turboshaft/types.h"
#include "src/compiler/turboshaft/uniform-reducer-adapter.h"
#include "src/heap/parked-scope.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <class Next>
class AssertTypesReducer
    : public UniformReducerAdapter<AssertTypesReducer, Next> {
#if defined(__clang__)
  static_assert(next_contains_reducer<Next, TypeInferenceReducer>::value);
#endif

 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(AssertTypes)

  using Adapter = UniformReducerAdapter<AssertTypesReducer, Next>;

  i::Tagged<Smi> NoContextConstant() {
    return Smi::FromInt(Context::kNoContext);
  }

  template <typename Op, typename Continuation>
  OpIndex ReduceInputGraphOperation(OpIndex ig_index, const Op& operation) {
    OpIndex og_index = Continuation{this}.ReduceInputGraph(ig_index, operation);
    if constexpr (std::is_same_v<Op, LoadRootRegisterOp>) {
      // LoadRootRegister is a bit special and should never be materialized,
      // hence we cannot assert its type.
      return og_index;
    }
    if (std::is_same_v<Op, ConstantOp>) {
      // Constants are constant by definition, so asserting their types doesn't
      // seem super useful. Additionally, they can appear before Parameters in
      // the graph, which leads to issues because asserting their types requires
      // inserting a Call in the graph, which can overwrite the value of
      // Parameters.
      return og_index;
    }
    if (!og_index.valid()) return og_index;
    if (!CanBeTyped(operation)) return og_index;
    // Unfortunately, we cannot insert assertions after block terminators, so we
    // skip them here.
    if (operation.IsBlockTerminator()) return og_index;

    auto reps = operation.outputs_rep();
    DCHECK_GT(reps.size(), 0);
    if (reps.size() == 1) {
      Type type = __ GetInputGraphType(ig_index);
      InsertTypeAssert(reps[0], og_index, type);
    }
    return og_index;
  }

  void InsertTypeAssert(RegisterRepresentation rep, OpIndex value,
                        const Type& type) {
    DCHECK(!type.IsInvalid());
    if (type.IsNone()) {
      __ Unreachable();
      return;
    }

    if (type.IsAny()) {
      // Ignore any typed for now.
      return;
    }

    auto GenerateBuiltinCall =
        [this](Builtin builtin, OpIndex original_value,
               base::SmallVector<OpIndex, 6> actual_value_indices,
               const Type& type) {
          i::Tagged<Smi> op_id = Smi::FromInt(original_value.id());
          // Add expected type and operation id.
          Handle<TurboshaftType> expected_type = type.AllocateOnHeap(factory());
          actual_value_indices.push_back(__ HeapConstant(expected_type));
          actual_value_indices.push_back(__ SmiConstant(op_id));
          actual_value_indices.push_back(__ SmiConstant(NoContextConstant()));
          __ CallBuiltin(
              builtin, OpIndex::Invalid(),
              {actual_value_indices.data(), actual_value_indices.size()},
              CanThrow::kNo, isolate_);
#ifdef DEBUG
          // Used for debugging
          if (v8_flags.turboshaft_trace_typing) {
            PrintF("Inserted assert for %3d:%-40s (%s)\n", original_value.id(),
                   __ output_graph().Get(original_value).ToString().c_str(),
                   type.ToString().c_str());
          }
#endif
        };

    switch (rep.value()) {
      case RegisterRepresentation::Word32(): {
        DCHECK(type.IsWord32());
        base::SmallVector<OpIndex, 6> actual_value_indices = {value};
        GenerateBuiltinCall(Builtin::kCheckTurboshaftWord32Type, value,
                            std::move(actual_value_indices), type);
        break;
      }
      case RegisterRepresentation::Word64(): {
        DCHECK(type.IsWord64());
        OpIndex value_high =
            __ TruncateWord64ToWord32(__ Word64ShiftRightLogical(value, 32));
        OpIndex value_low = __ TruncateWord64ToWord32(value);
        base::SmallVector<OpIndex, 6> actual_value_indices = {value_high,
                                                              value_low};
        GenerateBuiltinCall(Builtin::kCheckTurboshaftWord64Type, value,
                            std::move(actual_value_indices), type);
        break;
      }
      case RegisterRepresentation::Float32(): {
        DCHECK(type.IsFloat32());
        base::SmallVector<OpIndex, 6> actual_value_indices = {value};
        GenerateBuiltinCall(Builtin::kCheckTurboshaftFloat32Type, value,
                            std::move(actual_value_indices), type);
        break;
      }
      case RegisterRepresentation::Float64(): {
        DCHECK(type.IsFloat64());
        base::SmallVector<OpIndex, 6> actual_value_indices = {value};
        GenerateBuiltinCall(Builtin::kCheckTurboshaftFloat64Type, value,
                            std::move(actual_value_indices), type);
        break;
      }
      case RegisterRepresentation::Tagged():
      case RegisterRepresentation::Compressed():
      case RegisterRepresentation::Simd128():
      case RegisterRepresentation::Simd256():
        // TODO(nicohartmann@): Handle remaining cases.
        break;
    }
  }

 private:
  Factory* factory() { return isolate_->factory(); }
  Isolate* isolate_ = __ data() -> isolate();
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_ASSERT_TYPES_REDUCER_H_
