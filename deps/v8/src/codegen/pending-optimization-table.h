// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_PENDING_OPTIMIZATION_TABLE_H_
#define V8_CODEGEN_PENDING_OPTIMIZATION_TABLE_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

class IsCompiledScope;

// This class adds the functionality to properly test the optimized code. This
// is only for use in tests. All these functions should only be called when
// testing_d8_flag_for_tests is set.
class ManualOptimizationTable {
 public:
  // This function should be called before we mark the function for
  // optimization. It should be called when |function| is already compiled and
  // has a feedback vector allocated, and it blocks heuristic optimization.
  //
  // This also holds on to the bytecode strongly, preventing the bytecode from
  // being flushed.
  static void MarkFunctionForManualOptimization(
      Isolate* isolate, DirectHandle<JSFunction> function,
      IsCompiledScope* is_compiled_scope);

  // Returns true if MarkFunctionForManualOptimization was called with this
  // function.
  static bool IsMarkedForManualOptimization(Isolate* isolate,
                                            Tagged<JSFunction> function);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_PENDING_OPTIMIZATION_TABLE_H_
