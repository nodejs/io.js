// Use of this source code is governed by a BSD-style license that can be
// Copyright 2021 the V8 project authors. All rights reserved.
// found in the LICENSE file.

#ifndef V8_BASELINE_IA32_BASELINE_COMPILER_IA32_INL_H_
#define V8_BASELINE_IA32_BASELINE_COMPILER_IA32_INL_H_

#include "src/base/macros.h"
#include "src/baseline/baseline-compiler.h"
#include "src/codegen/interface-descriptors.h"

namespace v8 {
namespace internal {
namespace baseline {

#define __ basm_.

// A builtin call/jump mode that is used then short builtin calls feature is
// not enabled.
constexpr BuiltinCallJumpMode kFallbackBuiltinCallJumpModeForBaseline =
    BuiltinCallJumpMode::kIndirect;

void BaselineCompiler::Prologue() {
  DCHECK_EQ(kJSFunctionRegister, kJavaScriptCallTargetRegister);
  int max_frame_size = bytecode_->max_frame_size();
  CallBuiltin<Builtin::kBaselineOutOfLinePrologue>(
      kContextRegister, kJSFunctionRegister, kJavaScriptCallArgCountRegister,
      max_frame_size, kJavaScriptCallNewTargetRegister, bytecode_);

  PrologueFillFrame();
}

void BaselineCompiler::PrologueFillFrame() {
  ASM_CODE_COMMENT(&masm_);
  // Inlined register frame fill
  interpreter::Register new_target_or_generator_register =
      bytecode_->incoming_new_target_or_generator_register();
  if (v8_flags.debug_code) {
    __ masm()->CompareRoot(kInterpreterAccumulatorRegister,
                           RootIndex::kUndefinedValue);
    __ masm()->Assert(equal, AbortReason::kUnexpectedValue);
  }
  int register_count = bytecode_->register_count();
  // Magic value
  const int kLoopUnrollSize = 8;
  const int new_target_index = new_target_or_generator_register.index();
  const bool has_new_target = new_target_index != kMaxInt;
  if (has_new_target) {
    DCHECK_LE(new_target_index, register_count);
    for (int i = 0; i < new_target_index; i++) {
      __ Push(kInterpreterAccumulatorRegister);
    }
    // Push new_target_or_generator.
    __ Push(kJavaScriptCallNewTargetRegister);
    register_count -= new_target_index + 1;
  }
  if (register_count < 2 * kLoopUnrollSize) {
    // If the frame is small enough, just unroll the frame fill completely.
    for (int i = 0; i < register_count; ++i) {
      __ Push(kInterpreterAccumulatorRegister);
    }
  } else {
    // Extract the first few registers to round to the unroll size.
    int first_registers = register_count % kLoopUnrollSize;
    for (int i = 0; i < first_registers; ++i) {
      __ Push(kInterpreterAccumulatorRegister);
    }
    BaselineAssembler::ScratchRegisterScope scope(&basm_);
    Register scratch = scope.AcquireScratch();
    __ Move(scratch, register_count / kLoopUnrollSize);
    // We enter the loop unconditionally, so make sure we need to loop at least
    // once.
    DCHECK_GT(register_count / kLoopUnrollSize, 0);
    Label loop;
    __ Bind(&loop);
    for (int i = 0; i < kLoopUnrollSize; ++i) {
      __ Push(kInterpreterAccumulatorRegister);
    }
    __ masm()->dec(scratch);
    __ masm()->j(greater, &loop);
  }
}

void BaselineCompiler::VerifyFrameSize() {
  __ masm()->movd(xmm0, eax);
  __ Move(eax, esp);
  __ masm()->add(eax,
                 Immediate(InterpreterFrameConstants::kFixedFrameSizeFromFp +
                           bytecode_->frame_size()));
  __ masm()->cmp(eax, ebp);
  __ masm()->Assert(equal, AbortReason::kUnexpectedStackPointer);
  __ masm()->movd(eax, xmm0);
}

#undef __

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_IA32_BASELINE_COMPILER_IA32_INL_H_
