// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-compilation-unit.h"

#include "src/compiler/heap-refs.h"
#include "src/compiler/js-heap-broker.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-graph-labeller.h"
#include "src/objects/js-function-inl.h"

namespace v8 {
namespace internal {
namespace maglev {

MaglevCompilationUnit::MaglevCompilationUnit(MaglevCompilationInfo* info,
                                             Handle<JSFunction> function)
    : MaglevCompilationUnit(
          info, nullptr,
          MakeRef(info->broker(), info->broker()->CanonicalPersistentHandle(
                                      function->shared())),
          MakeRef(info->broker(), info->broker()->CanonicalPersistentHandle(
                                      function->feedback_vector()))) {}

MaglevCompilationUnit::MaglevCompilationUnit(
    MaglevCompilationInfo* info, const MaglevCompilationUnit* caller,
    compiler::SharedFunctionInfoRef shared_function_info,
    compiler::FeedbackVectorRef feedback_vector)
    : info_(info),
      caller_(caller),
      shared_function_info_(shared_function_info),
      bytecode_(shared_function_info.GetBytecodeArray(broker())),
      feedback_(feedback_vector),
      register_count_(bytecode_->register_count()),
      parameter_count_(bytecode_->parameter_count()),
      inlining_depth_(caller == nullptr ? 0 : caller->inlining_depth_ + 1) {
  // Check that the parameter count in the bytecode and in the shared function
  // info are consistent.
  SBXCHECK_EQ(
      bytecode_->parameter_count(),
      shared_function_info.internal_formal_parameter_count_with_receiver());
}

MaglevCompilationUnit::MaglevCompilationUnit(
    MaglevCompilationInfo* info, const MaglevCompilationUnit* caller,
    int register_count, int parameter_count)
    : info_(info),
      caller_(caller),
      register_count_(register_count),
      parameter_count_(parameter_count),
      inlining_depth_(caller == nullptr ? 0 : caller->inlining_depth_ + 1) {}

compiler::JSHeapBroker* MaglevCompilationUnit::broker() const {
  return info_->broker();
}

Zone* MaglevCompilationUnit::zone() const { return info_->zone(); }

bool MaglevCompilationUnit::has_graph_labeller() const {
  return info_->has_graph_labeller();
}

MaglevGraphLabeller* MaglevCompilationUnit::graph_labeller() const {
  DCHECK(has_graph_labeller());
  return info_->graph_labeller();
}

void MaglevCompilationUnit::RegisterNodeInGraphLabeller(const Node* node) {
  if (has_graph_labeller()) {
    graph_labeller()->RegisterNode(node);
  }
}

bool MaglevCompilationUnit::is_osr() const {
  return inlining_depth_ == 0 && info_->toplevel_is_osr();
}

BytecodeOffset MaglevCompilationUnit::osr_offset() const {
  return is_osr() ? info_->toplevel_osr_offset() : BytecodeOffset::None();
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
