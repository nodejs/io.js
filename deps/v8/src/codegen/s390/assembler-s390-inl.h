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
// Copyright 2014 the V8 project authors. All rights reserved.

#ifndef V8_CODEGEN_S390_ASSEMBLER_S390_INL_H_
#define V8_CODEGEN_S390_ASSEMBLER_S390_INL_H_

#include "src/codegen/assembler.h"
#include "src/codegen/flush-instruction-cache.h"
#include "src/codegen/s390/assembler-s390.h"
#include "src/debug/debug.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

bool CpuFeatures::SupportsOptimizer() { return true; }

void WritableRelocInfo::apply(intptr_t delta) {
  // Absolute code pointer inside code object moves with the code object.
  if (IsInternalReference(rmode_)) {
    // Jump table entry
    Address target = Memory<Address>(pc_);
    Memory<Address>(pc_) = target + delta;
  } else if (IsCodeTarget(rmode_)) {
    SixByteInstr instr =
        Instruction::InstructionBits(reinterpret_cast<const uint8_t*>(pc_));
    int32_t dis = static_cast<int32_t>(instr & 0xFFFFFFFF) * 2  // halfwords
                  - static_cast<int32_t>(delta);
    instr >>= 32;  // Clear the 4-byte displacement field.
    instr <<= 32;
    instr |= static_cast<uint32_t>(dis / 2);
    Instruction::SetInstructionBits<SixByteInstr>(
        reinterpret_cast<uint8_t*>(pc_), instr);
  } else {
    // mov sequence
    DCHECK(IsInternalReferenceEncoded(rmode_));
    Address target = Assembler::target_address_at(pc_, constant_pool_);
    Assembler::set_target_address_at(pc_, constant_pool_, target + delta,
                                     SKIP_ICACHE_FLUSH);
  }
}

Address RelocInfo::target_internal_reference() {
  if (IsInternalReference(rmode_)) {
    // Jump table entry
    return Memory<Address>(pc_);
  } else {
    // mov sequence
    DCHECK(IsInternalReferenceEncoded(rmode_));
    return Assembler::target_address_at(pc_, constant_pool_);
  }
}

Address RelocInfo::target_internal_reference_address() {
  DCHECK(IsInternalReference(rmode_) || IsInternalReferenceEncoded(rmode_));
  return pc_;
}

Address RelocInfo::target_address() {
  DCHECK(IsRelativeCodeTarget(rmode_) || IsCodeTarget(rmode_) ||
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
  // For an instruction like LIS/ORI where the target bits are mixed into the
  // instruction bits, the size of the target will be zero, indicating that the
  // serializer should not step forward in memory after a target is resolved
  // and written.
  return pc_;
}

Address RelocInfo::constant_pool_entry_address() { UNREACHABLE(); }

void Assembler::set_target_compressed_address_at(
    Address pc, Address constant_pool, Tagged_t target,
    ICacheFlushMode icache_flush_mode) {
  Assembler::set_target_address_at(
      pc, constant_pool, static_cast<Address>(target), icache_flush_mode);
}

int RelocInfo::target_address_size() {
  if (IsCodedSpecially()) {
    return Assembler::kSpecialTargetSize;
  } else {
    return kSystemPointerSize;
  }
}

Tagged_t Assembler::target_compressed_address_at(Address pc,
                                                 Address constant_pool) {
  return static_cast<Tagged_t>(target_address_at(pc, constant_pool));
}

Handle<Object> Assembler::code_target_object_handle_at(Address pc) {
  SixByteInstr instr =
      Instruction::InstructionBits(reinterpret_cast<const uint8_t*>(pc));
  int index = instr & 0xFFFFFFFF;
  return GetCodeTarget(index);
}

Tagged<HeapObject> RelocInfo::target_object(PtrComprCageBase cage_base) {
  DCHECK(IsCodeTarget(rmode_) || IsEmbeddedObjectMode(rmode_));
  if (IsCompressedEmbeddedObject(rmode_)) {
    return Cast<HeapObject>(
        Tagged<Object>(V8HeapCompressionScheme::DecompressTagged(
            cage_base,
            Assembler::target_compressed_address_at(pc_, constant_pool_))));
  } else {
    return Cast<HeapObject>(
        Tagged<Object>(Assembler::target_address_at(pc_, constant_pool_)));
  }
}

Handle<HeapObject> Assembler::compressed_embedded_object_handle_at(
    Address pc, Address const_pool) {
  return GetEmbeddedObject(target_compressed_address_at(pc, const_pool));
}

Handle<HeapObject> RelocInfo::target_object_handle(Assembler* origin) {
  DCHECK(IsRelativeCodeTarget(rmode_) || IsCodeTarget(rmode_) ||
         IsEmbeddedObjectMode(rmode_));
  if (IsCodeTarget(rmode_) || IsRelativeCodeTarget(rmode_)) {
    return Cast<HeapObject>(origin->code_target_object_handle_at(pc_));
  } else {
    if (IsCompressedEmbeddedObject(rmode_)) {
      return origin->compressed_embedded_object_handle_at(pc_, constant_pool_);
    }
    return Handle<HeapObject>(reinterpret_cast<Address*>(
        Assembler::target_address_at(pc_, constant_pool_)));
  }
}

void WritableRelocInfo::set_target_object(Tagged<HeapObject> target,
                                          ICacheFlushMode icache_flush_mode) {
  DCHECK(IsCodeTarget(rmode_) || IsEmbeddedObjectMode(rmode_));
  if (IsCompressedEmbeddedObject(rmode_)) {
    Assembler::set_target_compressed_address_at(
        pc_, constant_pool_,
        V8HeapCompressionScheme::CompressObject(target.ptr()),
        icache_flush_mode);
  } else {
    DCHECK(IsFullEmbeddedObject(rmode_));
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

Builtin RelocInfo::target_builtin_at(Assembler* origin) { UNREACHABLE(); }

Address RelocInfo::target_off_heap_target() {
  DCHECK(IsOffHeapTarget(rmode_));
  return Assembler::target_address_at(pc_, constant_pool_);
}

// Operand constructors
Operand::Operand(Register rm) : rm_(rm), rmode_(RelocInfo::NO_INFO) {}

// Fetch the 32bit value from the FIXED_SEQUENCE IIHF / IILF
Address Assembler::target_address_at(Address pc, Address constant_pool) {
  // S390 Instruction!
  // We want to check for instructions generated by Asm::mov()
  Opcode op1 =
      Instruction::S390OpcodeValue(reinterpret_cast<const uint8_t*>(pc));
  SixByteInstr instr_1 =
      Instruction::InstructionBits(reinterpret_cast<const uint8_t*>(pc));

  if (BRASL == op1 || BRCL == op1) {
    int32_t dis = static_cast<int32_t>(instr_1 & 0xFFFFFFFF) * 2;
    return pc + dis;
  }

#if V8_TARGET_ARCH_S390X
  int instr1_length =
      Instruction::InstructionLength(reinterpret_cast<const uint8_t*>(pc));
  Opcode op2 = Instruction::S390OpcodeValue(
      reinterpret_cast<const uint8_t*>(pc + instr1_length));
  SixByteInstr instr_2 = Instruction::InstructionBits(
      reinterpret_cast<const uint8_t*>(pc + instr1_length));
  // IIHF for hi_32, IILF for lo_32
  if (IIHF == op1 && IILF == op2) {
    return static_cast<Address>(((instr_1 & 0xFFFFFFFF) << 32) |
                                ((instr_2 & 0xFFFFFFFF)));
  }
#else
  // IILF loads 32-bits
  if (IILF == op1 || CFI == op1) {
    return static_cast<Address>((instr_1 & 0xFFFFFFFF));
  }
#endif

  UNIMPLEMENTED();
  return 0;
}

// This sets the branch destination (which gets loaded at the call address).
// This is for calls and branches within generated code.  The serializer
// has already deserialized the mov instructions etc.
// There is a FIXED_SEQUENCE assumption here
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
  if (RelocInfo::IsInternalReferenceEncoded(mode)) {
    set_target_address_at(pc, kNullAddress, target, SKIP_ICACHE_FLUSH);
  } else {
    Memory<Address>(pc) = target;
  }
}

// This code assumes the FIXED_SEQUENCE of IIHF/IILF
void Assembler::set_target_address_at(Address pc, Address constant_pool,
                                      Address target,
                                      ICacheFlushMode icache_flush_mode) {
  // Check for instructions generated by Asm::mov()
  Opcode op1 =
      Instruction::S390OpcodeValue(reinterpret_cast<const uint8_t*>(pc));
  SixByteInstr instr_1 =
      Instruction::InstructionBits(reinterpret_cast<const uint8_t*>(pc));
  bool patched = false;

  if (BRASL == op1 || BRCL == op1) {
    instr_1 >>= 32;  // Zero out the lower 32-bits
    instr_1 <<= 32;
    int32_t halfwords = (target - pc) / 2;  // number of halfwords
    instr_1 |= static_cast<uint32_t>(halfwords);
    Instruction::SetInstructionBits<SixByteInstr>(
        reinterpret_cast<uint8_t*>(pc), instr_1);
    if (icache_flush_mode != SKIP_ICACHE_FLUSH) {
      FlushInstructionCache(pc, 6);
    }
    patched = true;
  } else {
#if V8_TARGET_ARCH_S390X
    int instr1_length =
        Instruction::InstructionLength(reinterpret_cast<const uint8_t*>(pc));
    Opcode op2 = Instruction::S390OpcodeValue(
        reinterpret_cast<const uint8_t*>(pc + instr1_length));
    SixByteInstr instr_2 = Instruction::InstructionBits(
        reinterpret_cast<const uint8_t*>(pc + instr1_length));
    // IIHF for hi_32, IILF for lo_32
    if (IIHF == op1 && IILF == op2) {
      // IIHF
      instr_1 >>= 32;  // Zero out the lower 32-bits
      instr_1 <<= 32;
      instr_1 |= reinterpret_cast<uint64_t>(target) >> 32;

      Instruction::SetInstructionBits<SixByteInstr>(
          reinterpret_cast<uint8_t*>(pc), instr_1);

      // IILF
      instr_2 >>= 32;
      instr_2 <<= 32;
      instr_2 |= reinterpret_cast<uint64_t>(target) & 0xFFFFFFFF;

      Instruction::SetInstructionBits<SixByteInstr>(
          reinterpret_cast<uint8_t*>(pc + instr1_length), instr_2);
      if (icache_flush_mode != SKIP_ICACHE_FLUSH) {
        FlushInstructionCache(pc, 12);
      }
      patched = true;
    }
#else
    // IILF loads 32-bits
    if (IILF == op1 || CFI == op1) {
      instr_1 >>= 32;  // Zero out the lower 32-bits
      instr_1 <<= 32;
      instr_1 |= reinterpret_cast<uint32_t>(target);

      Instruction::SetInstructionBits<SixByteInstr>(
          reinterpret_cast<uint8_t*>(pc), instr_1);
      if (icache_flush_mode != SKIP_ICACHE_FLUSH) {
        FlushInstructionCache(pc, 6);
      }
      patched = true;
    }
#endif
  }
  if (!patched) UNREACHABLE();
}

uint32_t Assembler::uint32_constant_at(Address pc, Address constant_pool) {
  Opcode op1 =
      Instruction::S390OpcodeValue(reinterpret_cast<const uint8_t*>(pc));
  // Set by MacroAssembler::mov.
  CHECK(op1 == LGFI);
  SixByteInstr instr_1 =
      Instruction::InstructionBits(reinterpret_cast<const uint8_t*>(pc));
  return static_cast<uint32_t>((instr_1 << 32) >> 32);
}

void Assembler::set_uint32_constant_at(Address pc, Address constant_pool,
                                       uint32_t new_constant,
                                       ICacheFlushMode icache_flush_mode) {
  Opcode op1 =
      Instruction::S390OpcodeValue(reinterpret_cast<const uint8_t*>(pc));
  // Set by MacroAssembler::mov.
  CHECK(op1 == LGFI);
  SixByteInstr instr_1 =
      Instruction::InstructionBits(reinterpret_cast<const uint8_t*>(pc));
  instr_1 >>= 32;  // Zero out the lower 32-bits
  instr_1 <<= 32;
  instr_1 |= new_constant;
  Instruction::SetInstructionBits<SixByteInstr>(reinterpret_cast<uint8_t*>(pc),
                                                instr_1);
  if (icache_flush_mode != SKIP_ICACHE_FLUSH) {
    FlushInstructionCache(pc, 6);
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_S390_ASSEMBLER_S390_INL_H_
