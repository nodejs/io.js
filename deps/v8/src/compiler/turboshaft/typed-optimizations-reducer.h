// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_TYPED_OPTIMIZATIONS_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_TYPED_OPTIMIZATIONS_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/typer.h"
#include "src/compiler/turboshaft/uniform-reducer-adapter.h"

namespace v8::internal::compiler::turboshaft {

template <typename>
class TypeInferenceReducer;

template <typename Next>
class TypedOptimizationsReducer
    : public UniformReducerAdapter<TypedOptimizationsReducer, Next> {
#if defined(__clang__)
  // Typed optimizations require a typed graph.
  static_assert(next_contains_reducer<Next, TypeInferenceReducer>::value);
#endif

 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(TypedOptimizations)

  using Adapter = UniformReducerAdapter<TypedOptimizationsReducer, Next>;

  OpIndex ReduceInputGraphBranch(OpIndex ig_index, const BranchOp& operation) {
    if (!ShouldSkipOptimizationStep()) {
      Type condition_type = GetType(operation.condition());
      if (!condition_type.IsInvalid()) {
        if (condition_type.IsNone()) {
          Asm().Unreachable();
          return OpIndex::Invalid();
        }
        condition_type = Typer::TruncateWord32Input(condition_type, true,
                                                    Asm().graph_zone());
        DCHECK(condition_type.IsWord32());
        if (auto c = condition_type.AsWord32().try_get_constant()) {
          Block* goto_target = *c == 0 ? operation.if_false : operation.if_true;
          Asm().Goto(Asm().MapToNewGraph(goto_target));
          return OpIndex::Invalid();
        }
      }
    }
    return Adapter::ReduceInputGraphBranch(ig_index, operation);
  }

  template <typename Op, typename Continuation>
  OpIndex ReduceInputGraphOperation(OpIndex ig_index, const Op& operation) {
    if (!ShouldSkipOptimizationStep()) {
      Type type = GetType(ig_index);
      if (type.IsNone()) {
        // This operation is dead. Remove it.
        DCHECK(CanBeTyped(operation));
        Asm().Unreachable();
        return OpIndex::Invalid();
      } else if (!type.IsInvalid()) {
        // See if we can replace the operation by a constant.
        if (OpIndex constant = TryAssembleConstantForType(type);
            constant.valid()) {
          return constant;
        }
      }
    }

    // Otherwise just continue with reduction.
    return Continuation{this}.ReduceInputGraph(ig_index, operation);
  }

 private:
  // If {type} is a single value that can be respresented by a constant, this
  // function returns the index for a corresponding ConstantOp. It returns
  // OpIndex::Invalid otherwise.
  OpIndex TryAssembleConstantForType(const Type& type) {
    switch (type.kind()) {
      case Type::Kind::kWord32: {
        auto w32 = type.AsWord32();
        if (auto c = w32.try_get_constant()) {
          return Asm().Word32Constant(*c);
        }
        break;
      }
      case Type::Kind::kWord64: {
        auto w64 = type.AsWord64();
        if (auto c = w64.try_get_constant()) {
          return Asm().Word64Constant(*c);
        }
        break;
      }
      case Type::Kind::kFloat32: {
        auto f32 = type.AsFloat32();
        if (f32.is_only_nan()) {
          return Asm().Float32Constant(nan_v<32>);
        } else if (f32.is_only_minus_zero()) {
          return Asm().Float32Constant(-0.0f);
        } else if (auto c = f32.try_get_constant()) {
          return Asm().Float32Constant(*c);
        }
        break;
      }
      case Type::Kind::kFloat64: {
        auto f64 = type.AsFloat64();
        if (f64.is_only_nan()) {
          return Asm().Float64Constant(nan_v<64>);
        } else if (f64.is_only_minus_zero()) {
          return Asm().Float64Constant(-0.0);
        } else if (auto c = f64.try_get_constant()) {
          return Asm().Float64Constant(*c);
        }
        break;
      }
      default:
        break;
    }
    return OpIndex::Invalid();
  }

  Type GetType(const OpIndex index) {
    // Typed optimizations use the types of the input graph.
    return Asm().GetInputGraphType(index);
  }
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_TYPED_OPTIMIZATIONS_REDUCER_H_
