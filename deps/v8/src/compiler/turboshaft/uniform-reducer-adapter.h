// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_UNIFORM_REDUCER_ADAPTER_H_
#define V8_COMPILER_TURBOSHAFT_UNIFORM_REDUCER_ADAPTER_H_

#include "src/compiler/turboshaft/operations.h"

namespace v8::internal::compiler::turboshaft {

// UniformReducerAdapter allows to handle all operations uniformly during a
// reduction by wiring all ReduceInputGraphXyz and ReduceXyz calls through
// a single ReduceInputGraphOperation and ReduceOperation, respectively.
//
// This is how to use the adapter with your reducer MyReducer, which can then
// be used in a ReducerStack like any other reducer):
//
// template <typename Next>
// class MyReducer : public UniformReducerAdapter<MyReducer, Next> {
//  public:
//   TURBOSHAFT_REDUCER_BOILERPLATE()
//   using Adapter = UniformReducerAdapter<MyReducer, Next>;
//
//   OpIndex ReduceInputGraphConstant(OpIndex ig_index, const ConstantOp& op) {
//     /* Handle ConstantOps separately */
//     /* ... */
//
//     /* Call Adapter::ReduceInputGraphConstant(index, op) to also run */
//     /* through the generic handling in ReduceInputGraphOperation */
//     return Next::ReduceInputGraphConstant(index, op);
//   }
//
//   template <typename Op, typename Continuation>
//   OpIndex ReduceInputGraphOperation(OpIndex ig_index, const Op& op) {
//     /* Handle all (other) operations uniformly */
//     /* ... */
//
//     /* Forward to next reducer using the Continuation object */
//     return Continuation{this}.ReduceInputGraph(ig_index, op);
//   }
//
//   OpIndex ReduceConstant(ConstantOp::Kind kind, ConstantOp::Storage st) {
//     /* Handle Constants separately */
//     /* ... */
//
//     /* Call Adapter::ReduceConstant(kind, st) to also run through the */
//     /* generic handling in ReduceOperation */
//     return Next::ReduceConstant(kind, st);
//   }
//
//   template <Opcode opcode, typename Continuation, typename... Args>
//   OpIndex ReduceOperation(Args... args) {
//     /* Handle all (other) operations uniformly */
//     /* ... */
//
//     /* Forward to next reducer using the Continuation object */
//     return Continuation{this}.Reduce(args...);
//   }
//
//  private:
//   /* ... */
// };
//
// NOTICE: Inside the ReduceXyz and ReduceInputGraphXyz callbacks of MyReducer,
// you need to make a choice:
//
//   A) Call Next::ReduceXyz (or Next::ReduceInputGraphXyz) to forward to the
//      next reducer in the stack. Then the uniform ReduceOperation (and
//      ReduceInputGraphOperation) of the current reducer is not visited for
//      OperationXyz.
//   B) Call Adapter::ReduceXyz (or Adapter::ReduceInputGraphXyz) to forward to
//      the uniform ReduceOperation (and ReduceInputGraphOperation) such that
//      OperationXyz is also processed by those (in addition to the special
//      handling in ReduceXyz and ReduceInputGraphXyz).
//
// For the above MyReducer, consider this CopyingPhase<R1, MyReducer, R2>.
// Then the ReduceInputGraph (RIG) and Reduce (R) implementations are visited as
// follows for Operations OpA and OpB (and all other operations that are not
// ConstantOp), when all reducers just forward to Next. For ConstantOp, the
// reduction is equivalent to any "normal" reducer that does not use a
// UniformReducerAdapter.
//
//
// InputGraph OpA                     OpB     ____________________________
//             |                       |     |  ___                       |
//             |                       |     | |   |                      |
//             v                       v     | |   v                      v
// R1        RIGOpA                  RIGOpB  | |  ROpA                   ROpB
//             |     __          __    |     | |   |    ___        ___    |
//             |    |  |        |  |   |     | |   |   |   |      |   |   |
//             |    |  v        v  |   |     | |   |   |   v      v   |   |
// MyReducer   |    | RIGOperation |   |     | |   |   |  ROperation  |   |
//             v    |      v       |   |     | |   v   |      v       |   v
// (Adapter) RIGOpA | Continuation | RIGOpB  | |  ROpA | Continuation |  ROpB
//             |____|  |        |  |___|     | |   |___|  |        |  |___|
//                     |        |            | |          |        |
//              _______|        |______      | |    ______|        |______
//             |                       |     | |   |                      |
//             |                       |     | |   |                      |
//             v                       v     | |   v                      v
// R2        RIGOpA                  RIGOpB  | |  ROpA                   ROpB
//             |                       |_____| |   |                      |
//             |_______________________________|   |                      |
//                                                 v                      v
// OutputGraph                                    OpA                    OpB
//
//
template <template <typename> typename Reducer, typename Next>
class UniformReducerAdapter : public Next {
 public:
  template <Opcode opcode, typename Continuation, typename... Args>
  auto ReduceOperation(Args... args) {
    return Continuation{this}.Reduce(args...);
  }

  template <typename Op, typename Continuation>
  auto ReduceInputGraphOperation(OpIndex ig_index, const Op& operation) {
    return Continuation{this}.ReduceInputGraph(ig_index, operation);
  }

#define REDUCE(op)                                                           \
  struct Reduce##op##Continuation final {                                    \
    explicit Reduce##op##Continuation(Next* _this) : this_(_this) {}         \
    using Op = op##Op;                                                       \
    auto ReduceInputGraph(OpIndex ig_index, const op##Op& operation) {       \
      return this_->ReduceInputGraph##op(ig_index, operation);               \
    }                                                                        \
    template <typename... Args>                                              \
    auto Reduce(Args... args) const {                                        \
      return this_->Reduce##op(args...);                                     \
    }                                                                        \
    Next* this_;                                                             \
  };                                                                         \
  auto ReduceInputGraph##op(OpIndex ig_index, const op##Op& operation) {     \
    return static_cast<Reducer<Next>*>(this)                                 \
        ->template ReduceInputGraphOperation<op##Op,                         \
                                             Reduce##op##Continuation>(      \
            ig_index, operation);                                            \
  }                                                                          \
  template <typename... Args>                                                \
  auto Reduce##op(Args... args) {                                            \
    return static_cast<Reducer<Next>*>(this)                                 \
        ->template ReduceOperation<Opcode::k##op, Reduce##op##Continuation>( \
            args...);                                                        \
  }
  TURBOSHAFT_OPERATION_LIST(REDUCE)
#undef REDUCE
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_UNIFORM_REDUCER_ADAPTER_H_
