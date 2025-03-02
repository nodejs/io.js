// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_X64

#include "src/codegen/flush-instruction-cache.h"
#include "src/codegen/macro-assembler.h"
#include "src/common/code-memory-access-inl.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/isolate-data.h"

namespace v8 {
namespace internal {

// The deopt exit sizes below depend on the following IsolateData layout
// guarantees:
#define ASSERT_OFFSET(BuiltinName)                                       \
  static_assert(IsolateData::builtin_tier0_entry_table_offset() +        \
                    Builtins::ToInt(BuiltinName) * kSystemPointerSize <= \
                0x7F)
ASSERT_OFFSET(Builtin::kDeoptimizationEntry_Eager);
ASSERT_OFFSET(Builtin::kDeoptimizationEntry_Lazy);
#undef ASSERT_OFFSET

const int Deoptimizer::kEagerDeoptExitSize = 4;
#ifdef V8_ENABLE_CET_IBT
// With IBT, the lazy deopt entry has an additional endbr64 instruction.
const int Deoptimizer::kLazyDeoptExitSize = 8;
#else
const int Deoptimizer::kLazyDeoptExitSize = 4;
#endif

#if V8_ENABLE_CET_SHADOW_STACK
const int Deoptimizer::kAdaptShadowStackOffsetToSubtract = 7;
#else
const int Deoptimizer::kAdaptShadowStackOffsetToSubtract = 0;
#endif

// static
void Deoptimizer::PatchToJump(Address pc, Address new_pc) {
  if (!Assembler::IsNop(pc)) {
    // The place holder could be already patched.
    DCHECK(Assembler::IsJmpRel(pc));
    return;
  }

  RwxMemoryWriteScope rwx_write_scope("Patch jump to deopt trampoline");
  intptr_t displacement =
      new_pc - (pc + MacroAssembler::kIntraSegmentJmpInstrSize);
  CHECK(is_int32(displacement));
  // We'll overwrite only one instruction of 5-bytes. Give enough
  // space not to try to grow the buffer.
  constexpr int kSize = 32;
  Assembler masm(
      AssemblerOptions{},
      ExternalAssemblerBuffer(reinterpret_cast<uint8_t*>(pc), kSize));
  int offset = static_cast<int>(new_pc - pc);
  masm.jmp_rel(offset);
  FlushInstructionCache(pc, kSize);
}

Float32 RegisterValues::GetFloatRegister(unsigned n) const {
  return base::ReadUnalignedValue<Float32>(
      reinterpret_cast<Address>(simd128_registers_ + n));
}

Float64 RegisterValues::GetDoubleRegister(unsigned n) const {
  V8_ASSUME(n < arraysize(simd128_registers_));
  return base::ReadUnalignedValue<Float64>(
      reinterpret_cast<Address>(simd128_registers_ + n));
}

void RegisterValues::SetDoubleRegister(unsigned n, Float64 value) {
  base::WriteUnalignedValue<Float64>(
      reinterpret_cast<Address>(simd128_registers_ + n), value);
}

void FrameDescription::SetCallerPc(unsigned offset, intptr_t value) {
  SetFrameSlot(offset, value);
  caller_pc_ = value;
}

void FrameDescription::SetCallerFp(unsigned offset, intptr_t value) {
  SetFrameSlot(offset, value);
}

void FrameDescription::SetCallerConstantPool(unsigned offset, intptr_t value) {
  // No embedded constant pool support.
  UNREACHABLE();
}

void FrameDescription::SetPc(intptr_t pc) { pc_ = pc; }

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_X64
