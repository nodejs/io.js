// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_PPC64

#include "src/execution/ppc/frame-constants-ppc.h"

#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/execution/frame-constants.h"

namespace v8 {
namespace internal {

Register JavaScriptFrame::fp_register() { return v8::internal::fp; }
Register JavaScriptFrame::context_register() { return cp; }
Register JavaScriptFrame::constant_pool_pointer_register() {
  DCHECK(V8_EMBEDDED_CONSTANT_POOL_BOOL);
  return kConstantPoolRegister;
}

int UnoptimizedFrameConstants::RegisterStackSlotCount(int register_count) {
  return register_count;
}

int BuiltinContinuationFrameConstants::PaddingSlotCount(int register_count) {
  USE(register_count);
  return 0;
}

// static
intptr_t MaglevFrame::StackGuardFrameSize(int register_input_count) {
  USE(register_input_count);
  UNREACHABLE();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_PPC64
