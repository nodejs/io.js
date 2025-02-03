// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the
// distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been modified
// significantly by Google Inc.
// Copyright 2012 the V8 project authors. All rights reserved.

#ifndef V8_CODEGEN_ARM_ASSEMBLER_ARM_INL_H_
#define V8_CODEGEN_ARM_ASSEMBLER_ARM_INL_H_

#include "src/codegen/arm/assembler-arm.h"
#include "src/codegen/assembler.h"
#include "src/codegen/flush-instruction-cache.h"
#include "src/debug/debug.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"

namespace v8 {
namespace internal {

bool CpuFeatures::SupportsOptimizer() { return true; }

int DoubleRegister::SupportedRegisterCount() {
  return CpuFeatures::IsSupported(VFP32DREGS) ? 32 : 16;
}

void WritableRelocInfo::apply(intptr_t delta) {
  if (RelocInfo::IsInternalReference(rmode_)) {
    // absolute code pointer inside code object moves with the code object.
    int32_t* p = reinterpret_cast<int32_t*>(pc_);
    *p += delta;  // relocate entry
  } else if (RelocInfo::IsRelativeCodeTarget(rmode_)) {
    Instruction* branch = Instruction::At(pc_);
    int32_t branch_offset = branch->GetBranchOffset() - delta;
    branch->SetBranchOffset(branch_offset);
  }
}

Address RelocInfo::target_address() {
  DCHECK(IsCodeTargetMode(rmode_) || IsWasmCall(rmode_) ||
         IsWasmStubCall(rmode_));
  return Assembler::target_address_at(pc_, constant_pool_);
}

Address RelocInfo::target_address_address() {
  DCHECK(HasTargetAddressAddress());
  if (Assembler::IsMovW(Memory<int32_t>(pc_))) {
    return pc_;
  } else if (Assembler::IsLdrPcImmediateOffset(Memory<int32_t>(pc_))) {
    return constant_pool_entry_address();
  } else {
    DCHECK(Assembler::IsBOrBlPcImmediateOffset(Memory<int32_t>(pc_)));
    DCHECK(IsRelativeCodeTarget(rmode_));
    return pc_;
  }
}

Address RelocInfo::constant_pool_entry_address() {
  DCHECK(IsInConstantPool());
  return Assembler::constant_pool_entry_address(pc_, constant_pool_);
}

int RelocInfo::target_address_size() { return kPointerSize; }

Tagged<HeapObject> RelocInfo::target_object(PtrComprCageBase cage_base) {
  DCHECK(IsCodeTarget(rmode_) || IsFullEmbeddedObject(rmode_));
  return Cast<HeapObject>(
      Tagged<Object>(Assembler::target_address_at(pc_, constant_pool_)));
}

Handle<HeapObject> RelocInfo::target_object_handle(Assembler* origin) {
  if (IsCodeTarget(rmode_) || IsFullEmbeddedObject(rmode_)) {
    return Handle<HeapObject>(reinterpret_cast<Address*>(
        Assembler::target_address_at(pc_, constant_pool_)));
  }
  DCHECK(IsRelativeCodeTarget(rmode_));
  return origin->relative_code_target_object_handle_at(pc_);
}

void WritableRelocInfo::set_target_object(Tagged<HeapObject> target,
                                          ICacheFlushMode icache_flush_mode) {
  DCHECK(IsCodeTarget(rmode_) || IsFullEmbeddedObject(rmode_));
  Assembler::set_target_address_at(pc_, constant_pool_, target.ptr(),
                                   icache_flush_mode);
}

Address RelocInfo::target_external_reference() {
  DCHECK(rmode_ == EXTERNAL_REFERENCE);
  return Assembler::target_address_at(pc_, constant_pool_);
}

void WritableRelocInfo::set_target_external_reference(
    Address target, ICacheFlushMode icache_flush_mode) {
  DCHECK(rmode_ == RelocInfo::EXTERNAL_REFERENCE);
  Assembler::set_target_address_at(pc_, constant_pool_, target,
                                   icache_flush_mode);
}

Address RelocInfo::target_internal_reference() {
  DCHECK(rmode_ == INTERNAL_REFERENCE);
  return Memory<Address>(pc_);
}

Address RelocInfo::target_internal_reference_address() {
  DCHECK(rmode_ == INTERNAL_REFERENCE);
  return pc_;
}

Builtin RelocInfo::target_builtin_at(Assembler* origin) { UNREACHABLE(); }

Address RelocInfo::target_off_heap_target() {
  DCHECK(IsOffHeapTarget(rmode_));
  return Assembler::target_address_at(pc_, constant_pool_);
}

Handle<Code> Assembler::relative_code_target_object_handle_at(
    Address pc) const {
  Instruction* branch = Instruction::At(pc);
  int code_target_index = branch->GetBranchOffset() / kInstrSize;
  return GetCodeTarget(code_target_index);
}

Operand Operand::Zero() { return Operand(static_cast<int32_t>(0)); }

Operand::Operand(const ExternalReference& f)
    : rmode_(RelocInfo::EXTERNAL_REFERENCE) {
  value_.immediate = static_cast<int32_t>(f.address());
}

Operand::Operand(Tagged<Smi> value) : rmode_(RelocInfo::NO_INFO) {
  value_.immediate = static_cast<intptr_t>(value.ptr());
}

Operand::Operand(Register rm) : rm_(rm), shift_op_(LSL), shift_imm_(0) {}

void Assembler::CheckBuffer() {
  if (V8_UNLIKELY(buffer_space() <= kGap)) {
    GrowBuffer();
  }
  MaybeCheckConstPool();
}

void Assembler::emit(Instr x) {
  CheckBuffer();
  *reinterpret_cast<Instr*>(pc_) = x;
  pc_ += kInstrSize;
}

void Assembler::deserialization_set_special_target_at(
    Address constant_pool_entry, Tagged<Code> code, Address target) {
  DCHECK(!Builtins::IsIsolateIndependentBuiltin(code));
  Memory<Address>(constant_pool_entry) = target;
}

int Assembler::deserialization_special_target_size(Address location) {
  return kSpecialTargetSize;
}

void Assembler::deserialization_set_target_internal_reference_at(
    Address pc, Address target, RelocInfo::Mode mode) {
  Memory<Address>(pc) = target;
}

bool Assembler::is_constant_pool_load(Address pc) {
  return IsLdrPcImmediateOffset(Memory<int32_t>(pc));
}

Address Assembler::constant_pool_entry_address(Address pc,
                                               Address constant_pool) {
  DCHECK(Assembler::IsLdrPcImmediateOffset(Memory<int32_t>(pc)));
  Instr instr = Memory<int32_t>(pc);
  return pc + GetLdrRegisterImmediateOffset(instr) + Instruction::kPcLoadDelta;
}

Address Assembler::target_address_at(Address pc, Address constant_pool) {
  if (is_constant_pool_load(pc)) {
    // This is a constant pool lookup. Return the value in the constant pool.
    return Memory<Address>(constant_pool_entry_address(pc, constant_pool));
  } else if (CpuFeatures::IsSupported(ARMv7) && IsMovW(Memory<int32_t>(pc))) {
    // This is an movw / movt immediate load. Return the immediate.
    DCHECK(IsMovW(Memory<int32_t>(pc)) &&
           IsMovT(Memory<int32_t>(pc + kInstrSize)));
    Instruction* movw_instr = Instruction::At(pc);
    Instruction* movt_instr = Instruction::At(pc + kInstrSize);
    return static_cast<Address>((movt_instr->ImmedMovwMovtValue() << 16) |
                                movw_instr->ImmedMovwMovtValue());
  } else if (IsMovImmed(Memory<int32_t>(pc))) {
    // This is an mov / orr immediate load. Return the immediate.
    DCHECK(IsMovImmed(Memory<int32_t>(pc)) &&
           IsOrrImmed(Memory<int32_t>(pc + kInstrSize)) &&
           IsOrrImmed(Memory<int32_t>(pc + 2 * kInstrSize)) &&
           IsOrrImmed(Memory<int32_t>(pc + 3 * kInstrSize)));
    Instr mov_instr = instr_at(pc);
    Instr orr_instr_1 = instr_at(pc + kInstrSize);
    Instr orr_instr_2 = instr_at(pc + 2 * kInstrSize);
    Instr orr_instr_3 = instr_at(pc + 3 * kInstrSize);
    Address ret = static_cast<Address>(
        DecodeShiftImm(mov_instr) | DecodeShiftImm(orr_instr_1) |
        DecodeShiftImm(orr_instr_2) | DecodeShiftImm(orr_instr_3));
    return ret;
  } else {
    Instruction* branch = Instruction::At(pc);
    int32_t delta = branch->GetBranchOffset();
    return pc + delta + Instruction::kPcLoadDelta;
  }
}

void Assembler::set_target_address_at(Address pc, Address constant_pool,
                                      Address target,
                                      ICacheFlushMode icache_flush_mode) {
  if (is_constant_pool_load(pc)) {
    // This is a constant pool lookup. Update the entry in the constant pool.
    Memory<Address>(constant_pool_entry_address(pc, constant_pool)) = target;
    // Intuitively, we would think it is necessary to always flush the
    // instruction cache after patching a target address in the code as follows:
    //   FlushInstructionCache(pc, sizeof(target));
    // However, on ARM, no instruction is actually patched in the case
    // of embedded constants of the form:
    // ldr   ip, [pp, #...]
    // since the instruction accessing this address in the constant pool remains
    // unchanged.
  } else if (CpuFeatures::IsSupported(ARMv7) && IsMovW(Memory<int32_t>(pc))) {
    // This is an movw / movt immediate load. Patch the immediate embedded in
    // the instructions.
    DCHECK(IsMovW(Memory<int32_t>(pc)));
    DCHECK(IsMovT(Memory<int32_t>(pc + kInstrSize)));
    uint32_t* instr_ptr = reinterpret_cast<uint32_t*>(pc);
    uint32_t immediate = static_cast<uint32_t>(target);
    instr_ptr[0] = PatchMovwImmediate(instr_ptr[0], immediate & 0xFFFF);
    instr_ptr[1] = PatchMovwImmediate(instr_ptr[1], immediate >> 16);
    DCHECK(IsMovW(Memory<int32_t>(pc)));
    DCHECK(IsMovT(Memory<int32_t>(pc + kInstrSize)));
    if (icache_flush_mode != SKIP_ICACHE_FLUSH) {
      FlushInstructionCache(pc, 2 * kInstrSize);
    }
  } else if (IsMovImmed(Memory<int32_t>(pc))) {
    // This is an mov / orr immediate load. Patch the immediate embedded in
    // the instructions.
    DCHECK(IsMovImmed(Memory<int32_t>(pc)) &&
           IsOrrImmed(Memory<int32_t>(pc + kInstrSize)) &&
           IsOrrImmed(Memory<int32_t>(pc + 2 * kInstrSize)) &&
           IsOrrImmed(Memory<int32_t>(pc + 3 * kInstrSize)));
    uint32_t* instr_ptr = reinterpret_cast<uint32_t*>(pc);
    uint32_t immediate = static_cast<uint32_t>(target);
    instr_ptr[0] = PatchShiftImm(instr_ptr[0], immediate & kImm8Mask);
    instr_ptr[1] = PatchShiftImm(instr_ptr[1], immediate & (kImm8Mask << 8));
    instr_ptr[2] = PatchShiftImm(instr_ptr[2], immediate & (kImm8Mask << 16));
    instr_ptr[3] = PatchShiftImm(instr_ptr[3], immediate & (kImm8Mask << 24));
    DCHECK(IsMovImmed(Memory<int32_t>(pc)) &&
           IsOrrImmed(Memory<int32_t>(pc + kInstrSize)) &&
           IsOrrImmed(Memory<int32_t>(pc + 2 * kInstrSize)) &&
           IsOrrImmed(Memory<int32_t>(pc + 3 * kInstrSize)));
    if (icache_flush_mode != SKIP_ICACHE_FLUSH) {
      FlushInstructionCache(pc, 4 * kInstrSize);
    }
  } else {
    intptr_t branch_offset = target - pc - Instruction::kPcLoadDelta;
    Instruction* branch = Instruction::At(pc);
    branch->SetBranchOffset(branch_offset);
    if (icache_flush_mode != SKIP_ICACHE_FLUSH) {
      FlushInstructionCache(pc, kInstrSize);
    }
  }
}

uint32_t Assembler::uint32_constant_at(Address pc, Address constant_pool) {
  CHECK(is_constant_pool_load(pc));
  return Memory<uint32_t>(constant_pool_entry_address(pc, constant_pool));
}

void Assembler::set_uint32_constant_at(Address pc, Address constant_pool,
                                       uint32_t new_constant,
                                       ICacheFlushMode icache_flush_mode) {
  CHECK(is_constant_pool_load(pc));
  Memory<uint32_t>(constant_pool_entry_address(pc, constant_pool)) =
      new_constant;
  // Icache flushing not needed for Ldr via the constant pool.
}

EnsureSpace::EnsureSpace(Assembler* assembler) { assembler->CheckBuffer(); }

template <typename T>
bool UseScratchRegisterScope::CanAcquireVfp() const {
  VfpRegList* available = assembler_->GetScratchVfpRegisterList();
  DCHECK_NOT_NULL(available);
  for (int index = 0; index < T::kNumRegisters; index++) {
    T reg = T::from_code(index);
    uint64_t mask = reg.ToVfpRegList();
    if ((*available & mask) == mask) {
      return true;
    }
  }
  return false;
}

template <typename T>
T UseScratchRegisterScope::AcquireVfp() {
  VfpRegList* available = assembler_->GetScratchVfpRegisterList();
  DCHECK_NOT_NULL(available);
  for (int index = 0; index < T::kNumRegisters; index++) {
    T reg = T::from_code(index);
    uint64_t mask = reg.ToVfpRegList();
    if ((*available & mask) == mask) {
      *available &= ~mask;
      return reg;
    }
  }
  UNREACHABLE();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_ARM_ASSEMBLER_ARM_INL_H_
