// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/machine-graph.h"

#include "src/codegen/external-reference.h"

namespace v8 {
namespace internal {
namespace compiler {

Node* MachineGraph::UniqueInt32Constant(int32_t value) {
  return graph()->NewNode(common()->Int32Constant(value));
}

Node* MachineGraph::UniqueInt64Constant(int64_t value) {
  return graph()->NewNode(common()->Int64Constant(value));
}

Node* MachineGraph::Int32Constant(int32_t value) {
  Node** loc = cache_.FindInt32Constant(value);
  if (*loc == nullptr) {
    *loc = UniqueInt32Constant(value);
  }
  return *loc;
}

Node* MachineGraph::Int64Constant(int64_t value) {
  Node** loc = cache_.FindInt64Constant(value);
  if (*loc == nullptr) {
    *loc = UniqueInt64Constant(value);
  }
  return *loc;
}

Node* MachineGraph::IntPtrConstant(intptr_t value) {
  return machine()->Is32() ? Int32Constant(static_cast<int32_t>(value))
                           : Int64Constant(static_cast<int64_t>(value));
}

Node* MachineGraph::UintPtrConstant(uintptr_t value) {
  return machine()->Is32() ? Uint32Constant(static_cast<uint32_t>(value))
                           : Uint64Constant(static_cast<uint64_t>(value));
}

Node* MachineGraph::UniqueIntPtrConstant(intptr_t value) {
  return machine()->Is32() ? UniqueInt32Constant(static_cast<int32_t>(value))
                           : UniqueInt64Constant(static_cast<int64_t>(value));
}

Node* MachineGraph::TaggedIndexConstant(intptr_t value) {
  int32_t value32 = static_cast<int32_t>(value);
  Node** loc = cache_.FindTaggedIndexConstant(value32);
  if (*loc == nullptr) {
    *loc = graph()->NewNode(common()->TaggedIndexConstant(value32));
  }
  return *loc;
}

Node* MachineGraph::RelocatableInt32Constant(int32_t value,
                                             RelocInfo::Mode rmode) {
  Node** loc = cache_.FindRelocatableInt32Constant(
      value, static_cast<RelocInfoMode>(rmode));
  if (*loc == nullptr) {
    *loc = graph()->NewNode(common()->RelocatableInt32Constant(value, rmode));
  }
  return *loc;
}

Node* MachineGraph::RelocatableInt64Constant(int64_t value,
                                             RelocInfo::Mode rmode) {
  Node** loc = cache_.FindRelocatableInt64Constant(
      value, static_cast<RelocInfoMode>(rmode));
  if (*loc == nullptr) {
    *loc = graph()->NewNode(common()->RelocatableInt64Constant(value, rmode));
  }
  return *loc;
}

Node* MachineGraph::RelocatableIntPtrConstant(intptr_t value,
                                              RelocInfo::Mode rmode) {
  return kSystemPointerSize == 8
             ? RelocatableInt64Constant(value, rmode)
             : RelocatableInt32Constant(static_cast<int>(value), rmode);
}

Node* MachineGraph::RelocatableWasmBuiltinCallTarget(Builtin builtin) {
  return RelocatableIntPtrConstant(static_cast<intptr_t>(builtin),
                                   RelocInfo::WASM_STUB_CALL);
}

Node* MachineGraph::Float32Constant(float value) {
  Node** loc = cache_.FindFloat32Constant(value);
  if (*loc == nullptr) {
    *loc = graph()->NewNode(common()->Float32Constant(value));
  }
  return *loc;
}

Node* MachineGraph::Float64Constant(double value) {
  Node** loc = cache_.FindFloat64Constant(value);
  if (*loc == nullptr) {
    *loc = graph()->NewNode(common()->Float64Constant(value));
  }
  return *loc;
}

Node* MachineGraph::PointerConstant(intptr_t value) {
  Node** loc = cache_.FindPointerConstant(value);
  if (*loc == nullptr) {
    *loc = graph()->NewNode(common()->PointerConstant(value));
  }
  return *loc;
}

Node* MachineGraph::ExternalConstant(ExternalReference reference) {
  Node** loc = cache_.FindExternalConstant(reference);
  if (*loc == nullptr) {
    *loc = graph()->NewNode(common()->ExternalConstant(reference));
  }
  return *loc;
}

Node* MachineGraph::ExternalConstant(Runtime::FunctionId function_id) {
  return ExternalConstant(ExternalReference::Create(function_id));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
