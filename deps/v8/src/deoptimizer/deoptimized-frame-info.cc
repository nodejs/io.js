// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/deoptimizer/deoptimized-frame-info.h"

#include "src/execution/isolate.h"
#include "src/objects/js-function-inl.h"
#include "src/objects/oddball.h"

namespace v8 {
namespace internal {
namespace {

Handle<Object> GetValueForDebugger(TranslatedFrame::iterator it,
                                   Isolate* isolate) {
  if (it->GetRawValue() == ReadOnlyRoots(isolate).arguments_marker() &&
      !it->IsMaterializableByDebugger()) {
    return isolate->factory()->optimized_out();
  }
  return it->GetValue();
}

}  // namespace

DeoptimizedFrameInfo::DeoptimizedFrameInfo(TranslatedState* state,
                                           TranslatedState::iterator frame_it,
                                           Isolate* isolate) {
  int parameter_count =
      frame_it->shared_info()
          ->internal_formal_parameter_count_without_receiver();
  TranslatedFrame::iterator stack_it = frame_it->begin();

  // Get the function. Note that this might materialize the function.
  // In case the debugger mutates this value, we should deoptimize
  // the function and remember the value in the materialized value store.
  DCHECK_EQ(parameter_count,
            Cast<JSFunction>(stack_it->GetValue())
                ->shared()
                ->internal_formal_parameter_count_without_receiver());

  stack_it++;  // Skip the function.
  stack_it++;  // Skip the receiver.

  DCHECK_EQ(TranslatedFrame::kUnoptimizedFunction, frame_it->kind());

  parameters_.resize(static_cast<size_t>(parameter_count));
  for (int i = 0; i < parameter_count; i++) {
    Handle<Object> parameter = GetValueForDebugger(stack_it, isolate);
    SetParameter(i, parameter);
    stack_it++;
  }

  // Get the context.
  context_ = GetValueForDebugger(stack_it, isolate);
  stack_it++;

  // Get the expression stack.
  DCHECK_EQ(TranslatedFrame::kUnoptimizedFunction, frame_it->kind());
  const int stack_height = frame_it->height();  // Accumulator *not* included.

  expression_stack_.resize(static_cast<size_t>(stack_height));
  for (int i = 0; i < stack_height; i++) {
    Handle<Object> expression = GetValueForDebugger(stack_it, isolate);
    SetExpression(i, expression);
    stack_it++;
  }

  DCHECK_EQ(TranslatedFrame::kUnoptimizedFunction, frame_it->kind());
  stack_it++;  // Skip the accumulator.

  CHECK(stack_it == frame_it->end());
}

}  // namespace internal
}  // namespace v8
