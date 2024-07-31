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

#ifndef V8_CODEGEN_PPC_ASSEMBLER_PPC_INL_H_
#define V8_CODEGEN_PPC_ASSEMBLER_PPC_INL_H_

#include "src/codegen/assembler.h"
#include "src/codegen/flush-instruction-cache.h"
#include "src/codegen/ppc/assembler-ppc.h"
#include "src/debug/debug.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

bool CpuFeatures::SupportsOptimizer() { return true; }

void WritableRelocInfo::apply(intptr_t delta) {
  // absolute code pointer inside code object moves with the code object.
  if (IsInternalReference(rmode_)) {
    // Jump table entry
    Address target = Memory<Address>(pc_);
    Memory<Address>(pc_) = target + delta;
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
  DCHECK(IsCodeTarget(rmode_) || IsWasmCall(rmode_) || IsWasmStubCall(rmode_));
  return Assembler::target_address_at(pc_, constant_pool_);
}

Address RelocInfo::target_address_address() {
  DCHECK(HasTargetAddressAddress());

  if (V8_EMBEDDED_CONSTANT_POOL_BOOL &&
      Assembler::IsConstantPoolLoadStart(pc_)) {
    // We return the PC for embedded constant pool since this function is used
    // by the serializer and expects the address to reside within the code
    // object.
    return pc_;
  }

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

Address RelocInfo::constant_pool_entry_address() {
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
    DCHECK(constant_pool_);
    ConstantPoolEntry::Access access;
    if (Assembler::IsConstantPoolLoadStart(pc_, &access))
      return Assembler::target_constant_pool_address_at(
          pc_, constant_pool_, access, ConstantPoolEntry::INTPTR);
  }
  UNREACHABLE();
}

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

Handle<Object> Assembler::code_target_object_handle_at(Address pc,
                                                       Address constant_pool) {
  int index =
      static_cast<int>(target_address_at(pc, constant_pool)) & 0xFFFFFFFF;
  return GetCodeTarget(index);
}

Tagged<HeapObject> RelocInfo::target_object(PtrComprCageBase cage_base) {
  DCHECK(IsCodeTarget(rmode_) || IsEmbeddedObjectMode(rmode_));
  if (IsCompressedEmbeddedObject(rmode_)) {
    Tagged_t compressed =
        Assembler::target_compressed_address_at(pc_, constant_pool_);
    DCHECK(!HAS_SMI_TAG(compressed));
    Tagged<Object> obj(
        V8HeapCompressionScheme::DecompressTagged(cage_base, compressed));
    return HeapObject::cast(obj);
  } else {
    return HeapObject::cast(
        Tagged<Object>(Assembler::target_address_at(pc_, constant_pool_)));
  }
}

Handle<HeapObject> Assembler::compressed_embedded_object_handle_at(
    Address pc, Address const_pool) {
  return GetEmbeddedObject(target_compressed_address_at(pc, const_pool));
}

Handle<HeapObject> RelocInfo::target_object_handle(Assembler* origin) {
  DCHECK(IsCodeTarget(rmode_) || IsEmbeddedObjectMode(rmode_));
  if (IsCodeTarget(rmode_)) {
    return Handle<HeapObject>::cast(
        origin->code_target_object_handle_at(pc_, constant_pool_));
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

Operand::Operand(Register rm) : rm_(rm), rmode_(RelocInfo::NO_INFO) {}

void Assembler::UntrackBranch() {
  DCHECK(!trampoline_emitted_);
  DCHECK_GT(tracked_branch_count_, 0);
  int count = --tracked_branch_count_;
  if (count == 0) {
    // Reset
    next_trampoline_check_ = kMaxInt;
  } else {
    next_trampoline_check_ += kTrampolineSlotsSize;
  }
}

// Fetch the 32bit value from the FIXED_SEQUENCE lis/ori
Address Assembler::target_address_at(Address pc, Address constant_pool) {
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL && constant_pool) {
    ConstantPoolEntry::Access access;
    if (IsConstantPoolLoadStart(pc, &access))
      return Memory<Address>(target_constant_pool_address_at(
          pc, constant_pool, access, ConstantPoolEntry::INTPTR));
  }

  Instr instr1 = instr_at(pc);
  Instr instr2 = instr_at(pc + kInstrSize);
  // Interpret 2 instructions generated by lis/ori
  if (IsLis(instr1) && IsOri(instr2)) {
#if V8_TARGET_ARCH_PPC64
    Instr instr4 = instr_at(pc + (3 * kInstrSize));
    Instr instr5 = instr_at(pc + (4 * kInstrSize));
    // Assemble the 64 bit value.
    uint64_t hi = (static_cast<uint32_t>((instr1 & kImm16Mask) << 16) |
                   static_cast<uint32_t>(instr2 & kImm16Mask));
    uint64_t lo = (static_cast<uint32_t>((instr4 & kImm16Mask) << 16) |
                   static_cast<uint32_t>(instr5 & kImm16Mask));
    return static_cast<Address>((hi << 32) | lo);
#else
    // Assemble the 32 bit value.
    return static_cast<Address>(((instr1 & kImm16Mask) << 16) |
                                (instr2 & kImm16Mask));
#endif
  }

  UNREACHABLE();
}

#if V8_TARGET_ARCH_PPC64
const uint32_t kLoadIntptrOpcode = LD;
#else
const uint32_t kLoadIntptrOpcode = LWZ;
#endif

// Constant pool load sequence detection:
// 1) REGULAR access:
//    load <dst>, kConstantPoolRegister + <offset>
//
// 2) OVERFLOWED access:
//    addis <scratch>, kConstantPoolRegister, <offset_high>
//    load <dst>, <scratch> + <offset_low>
bool Assembler::IsConstantPoolLoadStart(Address pc,
                                        ConstantPoolEntry::Access* access) {
  Instr instr = instr_at(pc);
  uint32_t opcode = instr & kOpcodeMask;
  if (GetRA(instr) != kConstantPoolRegister) return false;
  bool overflowed = (opcode == ADDIS);
#ifdef DEBUG
  if (overflowed) {
    opcode = instr_at(pc + kInstrSize) & kOpcodeMask;
  }
  DCHECK(opcode == kLoadIntptrOpcode || opcode == LFD);
#endif
  if (access) {
    *access = (overflowed ? ConstantPoolEntry::OVERFLOWED
                          : ConstantPoolEntry::REGULAR);
  }
  return true;
}

bool Assembler::IsConstantPoolLoadEnd(Address pc,
                                      ConstantPoolEntry::Access* access) {
  Instr instr = instr_at(pc);
  uint32_t opcode = instr & kOpcodeMask;
  bool overflowed = false;
  if (!(opcode == kLoadIntptrOpcode || opcode == LFD)) return false;
  if (GetRA(instr) != kConstantPoolRegister) {
    instr = instr_at(pc - kInstrSize);
    opcode = instr & kOpcodeMask;
    if ((opcode != ADDIS) || GetRA(instr) != kConstantPoolRegister) {
      return false;
    }
    overflowed = true;
  }
  if (access) {
    *access = (overflowed ? ConstantPoolEntry::OVERFLOWED
                          : ConstantPoolEntry::REGULAR);
  }
  return true;
}

int Assembler::GetConstantPoolOffset(Address pc,
                                     ConstantPoolEntry::Access access,
                                     ConstantPoolEntry::Type type) {
  bool overflowed = (access == ConstantPoolEntry::OVERFLOWED);
#ifdef DEBUG
  ConstantPoolEntry::Access access_check =
      static_cast<ConstantPoolEntry::Access>(-1);
  DCHECK(IsConstantPoolLoadStart(pc, &access_check));
  DCHECK(access_check == access);
#endif
  int offset;
  if (overflowed) {
    offset = (instr_at(pc) & kImm16Mask) << 16;
    offset += SIGN_EXT_IMM16(instr_at(pc + kInstrSize) & kImm16Mask);
    DCHECK(!is_int16(offset));
  } else {
    offset = SIGN_EXT_IMM16((instr_at(pc) & kImm16Mask));
  }
  return offset;
}

void Assembler::PatchConstantPoolAccessInstruction(
    int pc_offset, int offset, ConstantPoolEntry::Access access,
    ConstantPoolEntry::Type type) {
  Address pc = reinterpret_cast<Address>(buffer_start_) + pc_offset;
  bool overflowed = (access == ConstantPoolEntry::OVERFLOWED);
  CHECK(overflowed != is_int16(offset));
#ifdef DEBUG
  ConstantPoolEntry::Access access_check =
      static_cast<ConstantPoolEntry::Access>(-1);
  DCHECK(IsConstantPoolLoadStart(pc, &access_check));
  DCHECK(access_check == access);
#endif
  if (overflowed) {
    int hi_word = static_cast<int>(offset >> 16);
    int lo_word = static_cast<int>(offset & 0xffff);
    if (lo_word & 0x8000) hi_word++;

    Instr instr1 = instr_at(pc);
    Instr instr2 = instr_at(pc + kInstrSize);
    instr1 &= ~kImm16Mask;
    instr1 |= (hi_word & kImm16Mask);
    instr2 &= ~kImm16Mask;
    instr2 |= (lo_word & kImm16Mask);
    instr_at_put(pc, instr1);
    instr_at_put(pc + kInstrSize, instr2);
  } else {
    Instr instr = instr_at(pc);
    instr &= ~kImm16Mask;
    instr |= (offset & kImm16Mask);
    instr_at_put(pc, instr);
  }
}

Address Assembler::target_constant_pool_address_at(
    Address pc, Address constant_pool, ConstantPoolEntry::Access access,
    ConstantPoolEntry::Type type) {
  Address addr = constant_pool;
  DCHECK(addr);
  addr += GetConstantPoolOffset(pc, access, type);
  return addr;
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

// This code assumes the FIXED_SEQUENCE of lis/ori
void Assembler::set_target_address_at(Address pc, Address constant_pool,
                                      Address target,
                                      ICacheFlushMode icache_flush_mode) {
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL && constant_pool) {
    ConstantPoolEntry::Access access;
    if (IsConstantPoolLoadStart(pc, &access)) {
      Memory<Address>(target_constant_pool_address_at(
          pc, constant_pool, access, ConstantPoolEntry::INTPTR)) = target;
      return;
    }
  }

  Instr instr1 = instr_at(pc);
  Instr instr2 = instr_at(pc + kInstrSize);
  // Interpret 2 instructions generated by lis/ori
  if (IsLis(instr1) && IsOri(instr2)) {
#if V8_TARGET_ARCH_PPC64
    Instr instr4 = instr_at(pc + (3 * kInstrSize));
    Instr instr5 = instr_at(pc + (4 * kInstrSize));
    // Needs to be fixed up when mov changes to handle 64-bit values.
    uint32_t* p = reinterpret_cast<uint32_t*>(pc);
    uintptr_t itarget = static_cast<uintptr_t>(target);

    instr5 &= ~kImm16Mask;
    instr5 |= itarget & kImm16Mask;
    itarget = itarget >> 16;

    instr4 &= ~kImm16Mask;
    instr4 |= itarget & kImm16Mask;
    itarget = itarget >> 16;

    instr2 &= ~kImm16Mask;
    instr2 |= itarget & kImm16Mask;
    itarget = itarget >> 16;

    instr1 &= ~kImm16Mask;
    instr1 |= itarget & kImm16Mask;
    itarget = itarget >> 16;

    *p = instr1;
    *(p + 1) = instr2;
    *(p + 3) = instr4;
    *(p + 4) = instr5;
    if (icache_flush_mode != SKIP_ICACHE_FLUSH) {
      FlushInstructionCache(p, 5 * kInstrSize);
    }
#else
    uint32_t* p = reinterpret_cast<uint32_t*>(pc);
    uint32_t itarget = static_cast<uint32_t>(target);
    int lo_word = itarget & kImm16Mask;
    int hi_word = itarget >> 16;
    instr1 &= ~kImm16Mask;
    instr1 |= hi_word;
    instr2 &= ~kImm16Mask;
    instr2 |= lo_word;

    *p = instr1;
    *(p + 1) = instr2;
    if (icache_flush_mode != SKIP_ICACHE_FLUSH) {
      FlushInstructionCache(p, 2 * kInstrSize);
    }
#endif
    return;
  }
  UNREACHABLE();
}
}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_PPC_ASSEMBLER_PPC_INL_H_
