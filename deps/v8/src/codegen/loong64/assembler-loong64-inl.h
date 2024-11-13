// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_LOONG64_ASSEMBLER_LOONG64_INL_H_
#define V8_CODEGEN_LOONG64_ASSEMBLER_LOONG64_INL_H_

#include "src/codegen/assembler.h"
#include "src/codegen/flush-instruction-cache.h"
#include "src/codegen/loong64/assembler-loong64.h"
#include "src/debug/debug.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

bool CpuFeatures::SupportsOptimizer() { return IsSupported(FPU); }

// -----------------------------------------------------------------------------
// Operand and MemOperand.

bool Operand::is_reg() const { return rm_.is_valid(); }

int64_t Operand::immediate() const {
  DCHECK(!is_reg());
  DCHECK(!IsHeapNumberRequest());
  return value_.immediate;
}

// -----------------------------------------------------------------------------
// RelocInfo.

void WritableRelocInfo::apply(intptr_t delta) {
  if (IsInternalReference(rmode_)) {
    // Absolute code pointer inside code object moves with the code object.
    Assembler::RelocateInternalReference(rmode_, pc_, delta);
  } else {
    DCHECK(IsRelativeCodeTarget(rmode_) || IsNearBuiltinEntry(rmode_));
    Assembler::RelocateRelativeReference(rmode_, pc_, delta);
  }
}

Address RelocInfo::target_address() {
  DCHECK(IsCodeTargetMode(rmode_) || IsNearBuiltinEntry(rmode_) ||
         IsWasmCall(rmode_) || IsWasmStubCall(rmode_));
  return Assembler::target_address_at(pc_, constant_pool_);
}

Address RelocInfo::target_address_address() {
  DCHECK(HasTargetAddressAddress());
  // Read the address of the word containing the target_address in an
  // instruction stream.
  // The only architecture-independent user of this function is the serializer.
  // The serializer uses it to find out how many raw bytes of instruction to
  // output before the next target.
  // For an instruction like LUI/ORI where the target bits are mixed into the
  // instruction bits, the size of the target will be zero, indicating that the
  // serializer should not step forward in memory after a target is resolved
  // and written. In this case the target_address_address function should
  // return the end of the instructions to be patched, allowing the
  // deserializer to deserialize the instructions as raw bytes and put them in
  // place, ready to be patched with the target. After jump optimization,
  // that is the address of the instruction that follows J/JAL/JR/JALR
  // instruction.
  return pc_ + Assembler::kInstructionsFor64BitConstant * kInstrSize;
}

Address RelocInfo::constant_pool_entry_address() { UNREACHABLE(); }

int RelocInfo::target_address_size() { return Assembler::kSpecialTargetSize; }

void Assembler::deserialization_set_special_target_at(
    Address instruction_payload, Tagged<Code> code, Address target) {
  set_target_address_at(instruction_payload,
                        !code.is_null() ? code->constant_pool() : kNullAddress,
                        target);
}

int Assembler::deserialization_special_target_size(
    Address instruction_payload) {
  return kSpecialTargetSize;
}

void Assembler::deserialization_set_target_internal_reference_at(
    Address pc, Address target, RelocInfo::Mode mode) {
  WriteUnalignedValue<Address>(pc, target);
}

Handle<HeapObject> Assembler::compressed_embedded_object_handle_at(
    Address pc, Address constant_pool) {
  return GetEmbeddedObject(target_compressed_address_at(pc, constant_pool));
}

Handle<HeapObject> Assembler::embedded_object_handle_at(Address pc,
                                                        Address constant_pool) {
  return GetEmbeddedObject(target_address_at(pc, constant_pool));
}

Handle<Code> Assembler::code_target_object_handle_at(Address pc,
                                                     Address constant_pool) {
  int index =
      static_cast<int>(target_address_at(pc, constant_pool)) & 0xFFFFFFFF;
  return GetCodeTarget(index);
}

Builtin Assembler::target_builtin_at(Address pc) {
  int builtin_id = static_cast<int>(target_address_at(pc) - pc) >> 2;
  DCHECK(Builtins::IsBuiltinId(builtin_id));
  return static_cast<Builtin>(builtin_id);
}

Tagged<HeapObject> RelocInfo::target_object(PtrComprCageBase cage_base) {
  DCHECK(IsCodeTarget(rmode_) || IsFullEmbeddedObject(rmode_));
  if (IsCompressedEmbeddedObject(rmode_)) {
    Tagged_t compressed =
        Assembler::target_compressed_address_at(pc_, constant_pool_);
    DCHECK(!HAS_SMI_TAG(compressed));
    Tagged<Object> obj(
        V8HeapCompressionScheme::DecompressTagged(cage_base, compressed));
    return Cast<HeapObject>(obj);
  } else {
    return Cast<HeapObject>(
        Tagged<Object>(Assembler::target_address_at(pc_, constant_pool_)));
  }
}

Handle<HeapObject> RelocInfo::target_object_handle(Assembler* origin) {
  if (IsCodeTarget(rmode_)) {
    return origin->code_target_object_handle_at(pc_, constant_pool_);
  } else if (IsFullEmbeddedObject(rmode_)) {
    return origin->embedded_object_handle_at(pc_, constant_pool_);
  } else if (IsCompressedEmbeddedObject(rmode_)) {
    return origin->compressed_embedded_object_handle_at(pc_, constant_pool_);
  } else {
    DCHECK(IsRelativeCodeTarget(rmode_));
    return origin->relative_code_target_object_handle_at(pc_);
  }
}

void WritableRelocInfo::set_target_object(Tagged<HeapObject> target,
                                          ICacheFlushMode icache_flush_mode) {
  DCHECK(IsCodeTarget(rmode_) || IsEmbeddedObjectMode(rmode_));
  if (IsCompressedEmbeddedObject(rmode_)) {
    DCHECK(COMPRESS_POINTERS_BOOL);
    // We must not compress pointers to objects outside of the main pointer
    // compression cage as we wouldn't be able to decompress them with the
    // correct cage base.
    DCHECK_IMPLIES(V8_ENABLE_SANDBOX_BOOL, !IsTrustedSpaceObject(target));
    DCHECK_IMPLIES(V8_EXTERNAL_CODE_SPACE_BOOL, !IsCodeSpaceObject(target));
    Assembler::set_target_compressed_address_at(
        pc_, constant_pool_,
        V8HeapCompressionScheme::CompressObject(target.ptr()),
        icache_flush_mode);
  } else {
    Assembler::set_target_address_at(pc_, constant_pool_, target.ptr(),
                                     icache_flush_mode);
  }
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
  if (rmode_ == INTERNAL_REFERENCE) {
    return Memory<Address>(pc_);
  } else {
    UNREACHABLE();
  }
}

Address RelocInfo::target_internal_reference_address() {
  DCHECK(rmode_ == INTERNAL_REFERENCE);
  return pc_;
}

Handle<Code> Assembler::relative_code_target_object_handle_at(
    Address pc) const {
  Instr instr = instr_at(pc);
  int32_t code_target_index = instr & kImm26Mask;
  code_target_index = ((code_target_index & 0x3ff) << 22 >> 6) |
                      ((code_target_index >> 10) & kImm16Mask);
  return GetCodeTarget(code_target_index);
}

Builtin RelocInfo::target_builtin_at(Assembler* origin) {
  DCHECK(IsNearBuiltinEntry(rmode_));
  return Assembler::target_builtin_at(pc_);
}

Address RelocInfo::target_off_heap_target() {
  DCHECK(IsOffHeapTarget(rmode_));
  return Assembler::target_address_at(pc_, constant_pool_);
}

uint32_t Assembler::uint32_constant_at(Address pc, Address constant_pool) {
  // target_compressed_address_at function could interpret lu12i.w and ori
  // instructions generated by MacroAssembler::li for a 32-bit value.
  return Assembler::target_compressed_address_at(pc);
}

void Assembler::set_uint32_constant_at(Address pc, Address constant_pool,
                                       uint32_t new_constant,
                                       ICacheFlushMode icache_flush_mode) {
  // set_target_compressed_value_at function could update 32-bit value loaded
  // by lu12i.w and ori instructions.
  Assembler::set_target_compressed_value_at(pc, new_constant,
                                            icache_flush_mode);
}

// -----------------------------------------------------------------------------
// Assembler.

void Assembler::CheckBuffer() {
  if (buffer_space() <= kGap) {
    GrowBuffer();
  }
}

void Assembler::EmitHelper(Instr x) {
  *reinterpret_cast<Instr*>(pc_) = x;
  pc_ += kInstrSize;
  CheckTrampolinePoolQuick();
}

template <>
inline void Assembler::EmitHelper(uint8_t x);

template <typename T>
void Assembler::EmitHelper(T x) {
  *reinterpret_cast<T*>(pc_) = x;
  pc_ += sizeof(x);
  CheckTrampolinePoolQuick();
}

template <>
void Assembler::EmitHelper(uint8_t x) {
  *reinterpret_cast<uint8_t*>(pc_) = x;
  pc_ += sizeof(x);
  if (reinterpret_cast<intptr_t>(pc_) % kInstrSize == 0) {
    CheckTrampolinePoolQuick();
  }
}

void Assembler::emit(Instr x) {
  if (!is_buffer_growth_blocked()) {
    CheckBuffer();
  }
  EmitHelper(x);
}

void Assembler::emit(uint64_t data) {
  //  CheckForEmitInForbiddenSlot();
  if (!is_buffer_growth_blocked()) {
    CheckBuffer();
  }
  EmitHelper(data);
}

EnsureSpace::EnsureSpace(Assembler* assembler) { assembler->CheckBuffer(); }

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_LOONG64_ASSEMBLER_LOONG64_INL_H_
