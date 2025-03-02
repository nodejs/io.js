// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file implements the Turbofan API when TF is enabled.
// See also v8_enable_turbofan in BUILD.gn.

#include "src/codegen/compiler.h"
#include "src/compiler/pipeline.h"
#include "src/compiler/turbofan.h"
#include "src/objects/code-kind.h"

namespace v8 {
namespace internal {
namespace compiler {

std::unique_ptr<TurbofanCompilationJob> NewCompilationJob(
    Isolate* isolate, Handle<JSFunction> function, IsScriptAvailable has_script,
    BytecodeOffset osr_offset) {
  return Pipeline::NewCompilationJob(isolate, function, CodeKind::TURBOFAN_JS,
                                     has_script == IsScriptAvailable::kYes,
                                     osr_offset);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
