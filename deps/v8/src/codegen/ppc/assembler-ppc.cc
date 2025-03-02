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

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2014 the V8 project authors. All rights reserved.

#include "src/codegen/ppc/assembler-ppc.h"

#if defined(__PASE__)
#include <sys/utsname.h>
#endif

#if V8_TARGET_ARCH_PPC64

#include "src/base/bits.h"
#include "src/base/cpu.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/ppc/assembler-ppc-inl.h"
#include "src/deoptimizer/deoptimizer.h"

namespace v8 {
namespace internal {

// Get the CPU features enabled by the build.
static unsigned CpuFeaturesImpliedByCompiler() {
  unsigned answer = 0;
  return answer;
}

bool CpuFeatures::SupportsWasmSimd128() {
#if V8_ENABLE_WEBASSEMBLY
  return CpuFeatures::IsSupported(PPC_9_PLUS);
#else
  return false;
#endif  // V8_ENABLE_WEBASSEMBLY
}

void CpuFeatures::ProbeImpl(bool cross_compile) {
  supported_ |= CpuFeaturesImpliedByCompiler();
  icache_line_size_ = 128;

  // Only use statically determined features for cross compile (snapshot).
  if (cross_compile) return;

// Probe for additional features at runtime.
#ifdef USE_SIMULATOR
  // Simulator
  supported_ |= (1u << PPC_10_PLUS);
#else
  base::CPU cpu;
  if (cpu.part() == base::CPU::kPPCPower10) {
#if defined(__PASE__)
    // Some P10 features such as prefixed isns will only be supported in future
    // ibmi versions. We only enable full power 10 features if version>7.4
    struct utsname uts;
    memset(reinterpret_cast<void*>(&uts), 0, sizeof(uts));
    int r = uname(&uts);
    CHECK_GE(r, 0);
    int rel = atoi(uts.release);
    if (rel > 4) {
      supported_ |= (1u << PPC_10_PLUS);
    } else {
      supported_ |= (1u << PPC_9_PLUS);
    }
#else
    supported_ |= (1u << PPC_10_PLUS);
#endif
  } else if (cpu.part() == base::CPU::kPPCPower9) {
    supported_ |= (1u << PPC_9_PLUS);
  } else if (cpu.part() == base::CPU::kPPCPower8) {
    supported_ |= (1u << PPC_8_PLUS);
  }
#if V8_OS_LINUX
  if (cpu.icache_line_size() != base::CPU::kUnknownCacheLineSize) {
    icache_line_size_ = cpu.icache_line_size();
  }
#endif
#endif
  if (supported_ & (1u << PPC_10_PLUS)) supported_ |= (1u << PPC_9_PLUS);
  if (supported_ & (1u << PPC_9_PLUS)) supported_ |= (1u << PPC_8_PLUS);

  // Set a static value on whether Simd is supported.
  // This variable is only used for certain archs to query SupportWasmSimd128()
  // at runtime in builtins using an extern ref. Other callers should use
  // CpuFeatures::SupportWasmSimd128().
  CpuFeatures::supports_wasm_simd_128_ = CpuFeatures::SupportsWasmSimd128();
}

void CpuFeatures::PrintTarget() {
  const char* ppc_arch = nullptr;
  ppc_arch = "ppc64";
  printf("target %s\n", ppc_arch);
}

void CpuFeatures::PrintFeatures() {
  printf("PPC_8_PLUS=%d\n", CpuFeatures::IsSupported(PPC_8_PLUS));
  printf("PPC_9_PLUS=%d\n", CpuFeatures::IsSupported(PPC_9_PLUS));
  printf("PPC_10_PLUS=%d\n", CpuFeatures::IsSupported(PPC_10_PLUS));
}

Register ToRegister(int num) {
  DCHECK(num >= 0 && num < kNumRegisters);
  const Register kRegisters[] = {r0,  sp,  r2,  r3,  r4,  r5,  r6,  r7,
                                 r8,  r9,  r10, r11, ip,  r13, r14, r15,
                                 r16, r17, r18, r19, r20, r21, r22, r23,
                                 r24, r25, r26, r27, r28, r29, r30, fp};
  return kRegisters[num];
}

// -----------------------------------------------------------------------------
// Implementation of RelocInfo

const int RelocInfo::kApplyMask =
    RelocInfo::ModeMask(RelocInfo::INTERNAL_REFERENCE) |
    RelocInfo::ModeMask(RelocInfo::INTERNAL_REFERENCE_ENCODED);

bool RelocInfo::IsCodedSpecially() {
  // The deserializer needs to know whether a pointer is specially
  // coded.  Being specially coded on PPC means that it is a lis/ori
  // instruction sequence or is a constant pool entry, and these are
  // always the case inside code objects.
  return true;
}

bool RelocInfo::IsInConstantPool() {
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL && constant_pool_ != kNullAddress) {
    return Assembler::IsConstantPoolLoadStart(pc_);
  }
  return false;
}

uint32_t RelocInfo::wasm_call_tag() const {
  DCHECK(rmode_ == WASM_CALL || rmode_ == WASM_STUB_CALL);
  return static_cast<uint32_t>(
      Assembler::target_address_at(pc_, constant_pool_));
}

// -----------------------------------------------------------------------------
// Implementation of Operand and MemOperand
// See assembler-ppc-inl.h for inlined constructors

Operand::Operand(Handle<HeapObject> handle) {
  rm_ = no_reg;
  value_.immediate = static_cast<intptr_t>(handle.address());
  rmode_ = RelocInfo::FULL_EMBEDDED_OBJECT;
}

Operand Operand::EmbeddedNumber(double value) {
  int32_t smi;
  if (DoubleToSmiInteger(value, &smi)) return Operand(Smi::FromInt(smi));
  Operand result(0, RelocInfo::FULL_EMBEDDED_OBJECT);
  result.is_heap_number_request_ = true;
  result.value_.heap_number_request = HeapNumberRequest(value);
  return result;
}

MemOperand::MemOperand(Register rn, int64_t offset)
    : ra_(rn), offset_(offset), rb_(no_reg) {}

MemOperand::MemOperand(Register ra, Register rb)
    : ra_(ra), offset_(0), rb_(rb) {}

MemOperand::MemOperand(Register ra, Register rb, int64_t offset)
    : ra_(ra), offset_(offset), rb_(rb) {}

void Assembler::AllocateAndInstallRequestedHeapNumbers(LocalIsolate* isolate) {
  DCHECK_IMPLIES(isolate == nullptr, heap_number_requests_.empty());
  for (auto& request : heap_number_requests_) {
    Handle<HeapObject> object =
        isolate->factory()->NewHeapNumber<AllocationType::kOld>(
            request.heap_number());
    Address pc = reinterpret_cast<Address>(buffer_start_) + request.offset();
    Address constant_pool = kNullAddress;
    set_target_address_at(pc, constant_pool, object.address(), nullptr,
                          SKIP_ICACHE_FLUSH);
  }
}

// -----------------------------------------------------------------------------
// Specific instructions, constants, and masks.

Assembler::Assembler(const AssemblerOptions& options,
                     std::unique_ptr<AssemblerBuffer> buffer)
    : AssemblerBase(options, std::move(buffer)),
      scratch_register_list_({ip}),
      constant_pool_builder_(kLoadPtrMaxReachBits, kLoadDoubleMaxReachBits) {
  reloc_info_writer.Reposition(buffer_start_ + buffer_->size(), pc_);

  no_trampoline_pool_before_ = 0;
  trampoline_pool_blocked_nesting_ = 0;
  constant_pool_entry_sharing_blocked_nesting_ = 0;
  next_trampoline_check_ = kMaxInt;
  internal_trampoline_exception_ = false;
  last_bound_pos_ = 0;
  optimizable_cmpi_pos_ = -1;
  trampoline_emitted_ = v8_flags.force_long_branches;
  tracked_branch_count_ = 0;
  relocations_.reserve(128);
}

void Assembler::GetCode(Isolate* isolate, CodeDesc* desc) {
  GetCode(isolate->main_thread_local_isolate(), desc);
}
void Assembler::GetCode(LocalIsolate* isolate, CodeDesc* desc,
                        SafepointTableBuilderBase* safepoint_table_builder,
                        int handler_table_offset) {
  // As a crutch to avoid having to add manual Align calls wherever we use a
  // raw workflow to create InstructionStream objects (mostly in tests), add
  // another Align call here. It does no harm - the end of the InstructionStream
  // object is aligned to the (larger) kCodeAlignment anyways.
  // TODO(jgruber): Consider moving responsibility for proper alignment to
  // metadata table builders (safepoint, handler, constant pool, code
  // comments).
  DataAlign(InstructionStream::kMetadataAlignment);

  // Emit constant pool if necessary.
  int constant_pool_size = EmitConstantPool();

  EmitRelocations();

  int code_comments_size = WriteCodeComments();

  AllocateAndInstallRequestedHeapNumbers(isolate);

  // Set up code descriptor.
  // TODO(jgruber): Reconsider how these offsets and sizes are maintained up to
  // this point to make CodeDesc initialization less fiddly.

  static constexpr int kBuiltinJumpTableInfoSize = 0;
  const int instruction_size = pc_offset();
  const int builtin_jump_table_info_offset =
      instruction_size - kBuiltinJumpTableInfoSize;
  const int code_comments_offset =
      builtin_jump_table_info_offset - code_comments_size;
  const int constant_pool_offset = code_comments_offset - constant_pool_size;
  const int handler_table_offset2 = (handler_table_offset == kNoHandlerTable)
                                        ? constant_pool_offset
                                        : handler_table_offset;
  const int safepoint_table_offset =
      (safepoint_table_builder == kNoSafepointTable)
          ? handler_table_offset2
          : safepoint_table_builder->safepoint_table_offset();
  const int reloc_info_offset =
      static_cast<int>(reloc_info_writer.pos() - buffer_->start());
  CodeDesc::Initialize(desc, this, safepoint_table_offset,
                       handler_table_offset2, constant_pool_offset,
                       code_comments_offset, builtin_jump_table_info_offset,
                       reloc_info_offset);
}

void Assembler::Align(int m) {
  DCHECK(m >= 4 && base::bits::IsPowerOfTwo(m));
  DCHECK_EQ(pc_offset() & (kInstrSize - 1), 0);
  while ((pc_offset() & (m - 1)) != 0) {
    nop();
  }
}

void Assembler::CodeTargetAlign() { Align(8); }

Condition Assembler::GetCondition(Instr instr) {
  switch (instr & kCondMask) {
    case BT:
      return eq;
    case BF:
      return ne;
    default:
      UNIMPLEMENTED();
  }
}

bool Assembler::IsLis(Instr instr) {
  return ((instr & kOpcodeMask) == ADDIS) && GetRA(instr) == r0;
}

bool Assembler::IsLi(Instr instr) {
  return ((instr & kOpcodeMask) == ADDI) && GetRA(instr) == r0;
}

bool Assembler::IsAddic(Instr instr) { return (instr & kOpcodeMask) == ADDIC; }

bool Assembler::IsOri(Instr instr) { return (instr & kOpcodeMask) == ORI; }

bool Assembler::IsBranch(Instr instr) { return ((instr & kOpcodeMask) == BCX); }

Register Assembler::GetRA(Instr instr) {
  return Register::from_code(Instruction::RAValue(instr));
}

Register Assembler::GetRB(Instr instr) {
  return Register::from_code(Instruction::RBValue(instr));
}

// This code assumes a FIXED_SEQUENCE for 64bit loads (lis/ori)
bool Assembler::Is64BitLoadIntoR12(Instr instr1, Instr instr2, Instr instr3,
                                   Instr instr4, Instr instr5) {
  // Check the instructions are indeed a five part load (into r12)
  // 3d800000       lis     r12, 0
  // 618c0000       ori     r12, r12, 0
  // 798c07c6       rldicr  r12, r12, 32, 31
  // 658c00c3       oris    r12, r12, 195
  // 618ccd40       ori     r12, r12, 52544
  return (((instr1 >> 16) == 0x3D80) && ((instr2 >> 16) == 0x618C) &&
          (instr3 == 0x798C07C6) && ((instr4 >> 16) == 0x658C) &&
          ((instr5 >> 16) == 0x618C));
}

bool Assembler::IsCmpRegister(Instr instr) {
  return (((instr & kOpcodeMask) == EXT2) &&
          ((EXT2 | (instr & kExt2OpcodeMask)) == CMP));
}

bool Assembler::IsRlwinm(Instr instr) {
  return ((instr & kOpcodeMask) == RLWINMX);
}

bool Assembler::IsAndi(Instr instr) { return ((instr & kOpcodeMask) == ANDIx); }

bool Assembler::IsRldicl(Instr instr) {
  return (((instr & kOpcodeMask) == EXT5) &&
          ((EXT5 | (instr & kExt5OpcodeMask)) == RLDICL));
}

bool Assembler::IsCmpImmediate(Instr instr) {
  return ((instr & kOpcodeMask) == CMPI);
}

bool Assembler::IsCrSet(Instr instr) {
  return (((instr & kOpcodeMask) == EXT1) &&
          ((EXT1 | (instr & kExt1OpcodeMask)) == CREQV));
}

Register Assembler::GetCmpImmediateRegister(Instr instr) {
  DCHECK(IsCmpImmediate(instr));
  return GetRA(instr);
}

int Assembler::GetCmpImmediateRawImmediate(Instr instr) {
  DCHECK(IsCmpImmediate(instr));
  return instr & kOff16Mask;
}

// Labels refer to positions in the (to be) generated code.
// There are bound, linked, and unused labels.
//
// Bound labels refer to known positions in the already
// generated code. pos() is the position the label refers to.
//
// Linked labels refer to unknown positions in the code
// to be generated; pos() is the position of the last
// instruction using the label.

// The link chain is terminated by a negative code position (must be aligned)
const int kEndOfChain = -4;

// Dummy opcodes for unbound label mov instructions or jump table entries.
enum {
  kUnboundMovLabelOffsetOpcode = 0 << 26,
  kUnboundAddLabelOffsetOpcode = 1 << 26,
  kUnboundAddLabelLongOffsetOpcode = 2 << 26,
  kUnboundMovLabelAddrOpcode = 3 << 26,
  kUnboundJumpTableEntryOpcode = 4 << 26
};

int Assembler::target_at(int pos) {
  Instr instr = instr_at(pos);
  // check which type of branch this is 16 or 26 bit offset
  uint32_t opcode = instr & kOpcodeMask;
  int link;
  switch (opcode) {
    case BX:
      link = SIGN_EXT_IMM26(instr & kImm26Mask);
      link &= ~(kAAMask | kLKMask);  // discard AA|LK bits if present
      break;
    case BCX:
      link = SIGN_EXT_IMM16((instr & kImm16Mask));
      link &= ~(kAAMask | kLKMask);  // discard AA|LK bits if present
      break;
    case kUnboundMovLabelOffsetOpcode:
    case kUnboundAddLabelOffsetOpcode:
    case kUnboundAddLabelLongOffsetOpcode:
    case kUnboundMovLabelAddrOpcode:
    case kUnboundJumpTableEntryOpcode:
      link = SIGN_EXT_IMM26(instr & kImm26Mask);
      link <<= 2;
      break;
    default:
      DCHECK(false);
      return -1;
  }

  if (link == 0) return kEndOfChain;
  return pos + link;
}

void Assembler::target_at_put(int pos, int target_pos, bool* is_branch) {
  Instr instr = instr_at(pos);
  uint32_t opcode = instr & kOpcodeMask;

  if (is_branch != nullptr) {
    *is_branch = (opcode == BX || opcode == BCX);
  }

  switch (opcode) {
    case BX: {
      int imm26 = target_pos - pos;
      CHECK(is_int26(imm26) && (imm26 & (kAAMask | kLKMask)) == 0);
      if (imm26 == kInstrSize && !(instr & kLKMask)) {
        // Branch to next instr without link.
        instr = ORI;  // nop: ori, 0,0,0
      } else {
        instr &= ((~kImm26Mask) | kAAMask | kLKMask);
        instr |= (imm26 & kImm26Mask);
      }
      instr_at_put(pos, instr);
      break;
    }
    case BCX: {
      int imm16 = target_pos - pos;
      CHECK(is_int16(imm16) && (imm16 & (kAAMask | kLKMask)) == 0);
      if (imm16 == kInstrSize && !(instr & kLKMask)) {
        // Branch to next instr without link.
        instr = ORI;  // nop: ori, 0,0,0
      } else {
        instr &= ((~kImm16Mask) | kAAMask | kLKMask);
        instr |= (imm16 & kImm16Mask);
      }
      instr_at_put(pos, instr);
      break;
    }
    case kUnboundMovLabelOffsetOpcode: {
      // Load the position of the label relative to the generated code object
      // pointer in a register.
      Register dst = Register::from_code(instr_at(pos + kInstrSize));
      int32_t offset =
          target_pos + (InstructionStream::kHeaderSize - kHeapObjectTag);
      PatchingAssembler patcher(
          options(), reinterpret_cast<uint8_t*>(buffer_start_ + pos), 2);
      patcher.bitwise_mov32(dst, offset);
      break;
    }
    case kUnboundAddLabelLongOffsetOpcode:
    case kUnboundAddLabelOffsetOpcode: {
      // dst = base + position + immediate
      Instr operands = instr_at(pos + kInstrSize);
      Register dst = Register::from_code((operands >> 27) & 0x1F);
      Register base = Register::from_code((operands >> 22) & 0x1F);
      int32_t delta = (opcode == kUnboundAddLabelLongOffsetOpcode)
                          ? static_cast<int32_t>(instr_at(pos + 2 * kInstrSize))
                          : (SIGN_EXT_IMM22(operands & kImm22Mask));
      int32_t offset = target_pos + delta;
      PatchingAssembler patcher(
          options(), reinterpret_cast<uint8_t*>(buffer_start_ + pos),
          2 + static_cast<int32_t>(opcode == kUnboundAddLabelLongOffsetOpcode));
      patcher.bitwise_add32(dst, base, offset);
      if (opcode == kUnboundAddLabelLongOffsetOpcode) patcher.nop();
      break;
    }
    case kUnboundMovLabelAddrOpcode: {
      // Load the address of the label in a register.
      Register dst = Register::from_code(instr_at(pos + kInstrSize));
      PatchingAssembler patcher(options(),
                                reinterpret_cast<uint8_t*>(buffer_start_ + pos),
                                kMovInstructionsNoConstantPool);
      // Keep internal references relative until EmitRelocations.
      patcher.bitwise_mov(dst, target_pos);
      break;
    }
    case kUnboundJumpTableEntryOpcode: {
      PatchingAssembler patcher(options(),
                                reinterpret_cast<uint8_t*>(buffer_start_ + pos),
                                kSystemPointerSize / kInstrSize);
      // Keep internal references relative until EmitRelocations.
      patcher.dp(target_pos);
      break;
    }
    default:
      DCHECK(false);
      break;
  }
}

int Assembler::max_reach_from(int pos) {
  Instr instr = instr_at(pos);
  uint32_t opcode = instr & kOpcodeMask;

  // check which type of branch this is 16 or 26 bit offset
  switch (opcode) {
    case BX:
      return 26;
    case BCX:
      return 16;
    case kUnboundMovLabelOffsetOpcode:
    case kUnboundAddLabelOffsetOpcode:
    case kUnboundMovLabelAddrOpcode:
    case kUnboundJumpTableEntryOpcode:
      return 0;  // no limit on reach
  }

  DCHECK(false);
  return 0;
}

void Assembler::bind_to(Label* L, int pos) {
  DCHECK(0 <= pos && pos <= pc_offset());  // must have a valid binding position
  int32_t trampoline_pos = kInvalidSlotPos;
  bool is_branch = false;
  while (L->is_linked()) {
    int fixup_pos = L->pos();
    int32_t offset = pos - fixup_pos;
    int maxReach = max_reach_from(fixup_pos);
    next(L);  // call next before overwriting link with target at fixup_pos
    if (maxReach && is_intn(offset, maxReach) == false) {
      if (trampoline_pos == kInvalidSlotPos) {
        trampoline_pos = get_trampoline_entry();
        CHECK_NE(trampoline_pos, kInvalidSlotPos);
        target_at_put(trampoline_pos, pos);
      }
      target_at_put(fixup_pos, trampoline_pos);
    } else {
      target_at_put(fixup_pos, pos, &is_branch);
    }
  }
  L->bind_to(pos);

  if (!trampoline_emitted_ && is_branch) {
    UntrackBranch();
  }

  // Keep track of the last bound label so we don't eliminate any instructions
  // before a bound label.
  if (pos > last_bound_pos_) last_bound_pos_ = pos;
}

void Assembler::bind(Label* L) {
  DCHECK(!L->is_bound());  // label can only be bound once
  bind_to(L, pc_offset());
}

void Assembler::next(Label* L) {
  DCHECK(L->is_linked());
  int link = target_at(L->pos());
  if (link == kEndOfChain) {
    L->Unuse();
  } else {
    DCHECK_GE(link, 0);
    L->link_to(link);
  }
}

bool Assembler::is_near(Label* L, Condition cond) {
  DCHECK(L->is_bound());
  if (L->is_bound() == false) return false;

  int maxReach = ((cond == al) ? 26 : 16);
  int offset = L->pos() - pc_offset();

  return is_intn(offset, maxReach);
}

void Assembler::a_form(Instr instr, DoubleRegister frt, DoubleRegister fra,
                       DoubleRegister frb, RCBit r) {
  emit(instr | frt.code() * B21 | fra.code() * B16 | frb.code() * B11 | r);
}

void Assembler::d_form(Instr instr, Register rt, Register ra,
                       const intptr_t val, bool signed_disp) {
  if (signed_disp) {
    if (!is_int16(val)) {
      PrintF("val = %" V8PRIdPTR ", 0x%" V8PRIxPTR "\n", val, val);
    }
    CHECK(is_int16(val));
  } else {
    if (!is_uint16(val)) {
      PrintF("val = %" V8PRIdPTR ", 0x%" V8PRIxPTR
             ", is_unsigned_imm16(val)=%d, kImm16Mask=0x%x\n",
             val, val, is_uint16(val), kImm16Mask);
    }
    CHECK(is_uint16(val));
  }
  emit(instr | rt.code() * B21 | ra.code() * B16 | (kImm16Mask & val));
}

void Assembler::xo_form(Instr instr, Register rt, Register ra, Register rb,
                        OEBit o, RCBit r) {
  emit(instr | rt.code() * B21 | ra.code() * B16 | rb.code() * B11 | o | r);
}

void Assembler::md_form(Instr instr, Register ra, Register rs, int shift,
                        int maskbit, RCBit r) {
  int sh0_4 = shift & 0x1F;
  int sh5 = (shift >> 5) & 0x1;
  int m0_4 = maskbit & 0x1F;
  int m5 = (maskbit >> 5) & 0x1;

  emit(instr | rs.code() * B21 | ra.code() * B16 | sh0_4 * B11 | m0_4 * B6 |
       m5 * B5 | sh5 * B1 | r);
}

void Assembler::mds_form(Instr instr, Register ra, Register rs, Register rb,
                         int maskbit, RCBit r) {
  int m0_4 = maskbit & 0x1F;
  int m5 = (maskbit >> 5) & 0x1;

  emit(instr | rs.code() * B21 | ra.code() * B16 | rb.code() * B11 | m0_4 * B6 |
       m5 * B5 | r);
}

// Returns the next free trampoline entry.
int32_t Assembler::get_trampoline_entry() {
  int32_t trampoline_entry = kInvalidSlotPos;

  if (!internal_trampoline_exception_) {
    trampoline_entry = trampoline_.take_slot();

    if (kInvalidSlotPos == trampoline_entry) {
      internal_trampoline_exception_ = true;
    }
  }
  return trampoline_entry;
}

int Assembler::link(Label* L) {
  int position;
  if (L->is_bound()) {
    position = L->pos();
  } else {
    if (L->is_linked()) {
      position = L->pos();  // L's link
    } else {
      // was: target_pos = kEndOfChain;
      // However, using self to mark the first reference
      // should avoid most instances of branch offset overflow.  See
      // target_at() for where this is converted back to kEndOfChain.
      position = pc_offset();
    }
    L->link_to(pc_offset());
  }

  return position;
}

// Branch instructions.

void Assembler::bclr(BOfield bo, int condition_bit, LKBit lk) {
  emit(EXT1 | static_cast<uint32_t>(bo) | condition_bit * B16 | BCLRX | lk);
}

void Assembler::bcctr(BOfield bo, int condition_bit, LKBit lk) {
  emit(EXT1 | static_cast<uint32_t>(bo) | condition_bit * B16 | BCCTRX | lk);
}

// Pseudo op - branch to link register
void Assembler::blr() { bclr(BA, 0, LeaveLK); }

// Pseudo op - branch to count register -- used for "jump"
void Assembler::bctr() { bcctr(BA, 0, LeaveLK); }

void Assembler::bctrl() { bcctr(BA, 0, SetLK); }

void Assembler::bc(int branch_offset, BOfield bo, int condition_bit, LKBit lk) {
  int imm16 = branch_offset;
  CHECK(is_int16(imm16) && (imm16 & (kAAMask | kLKMask)) == 0);
  emit(BCX | static_cast<uint32_t>(bo) | condition_bit * B16 |
       (imm16 & kImm16Mask) | lk);
}

void Assembler::b(int branch_offset, LKBit lk) {
  int imm26 = branch_offset;
  CHECK(is_int26(imm26) && (imm26 & (kAAMask | kLKMask)) == 0);
  emit(BX | (imm26 & kImm26Mask) | lk);
}

void Assembler::xori(Register dst, Register src, const Operand& imm) {
  d_form(XORI, src, dst, imm.immediate(), false);
}

void Assembler::xoris(Register ra, Register rs, const Operand& imm) {
  d_form(XORIS, rs, ra, imm.immediate(), false);
}

void Assembler::rlwinm(Register ra, Register rs, int sh, int mb, int me,
                       RCBit rc) {
  sh &= 0x1F;
  mb &= 0x1F;
  me &= 0x1F;
  emit(RLWINMX | rs.code() * B21 | ra.code() * B16 | sh * B11 | mb * B6 |
       me << 1 | rc);
}

void Assembler::rlwnm(Register ra, Register rs, Register rb, int mb, int me,
                      RCBit rc) {
  mb &= 0x1F;
  me &= 0x1F;
  emit(RLWNMX | rs.code() * B21 | ra.code() * B16 | rb.code() * B11 | mb * B6 |
       me << 1 | rc);
}

void Assembler::rlwimi(Register ra, Register rs, int sh, int mb, int me,
                       RCBit rc) {
  sh &= 0x1F;
  mb &= 0x1F;
  me &= 0x1F;
  emit(RLWIMIX | rs.code() * B21 | ra.code() * B16 | sh * B11 | mb * B6 |
       me << 1 | rc);
}

void Assembler::slwi(Register dst, Register src, const Operand& val, RCBit rc) {
  DCHECK((32 > val.immediate()) && (val.immediate() >= 0));
  rlwinm(dst, src, val.immediate(), 0, 31 - val.immediate(), rc);
}

void Assembler::srwi(Register dst, Register src, const Operand& val, RCBit rc) {
  DCHECK((32 > val.immediate()) && (val.immediate() >= 0));
  rlwinm(dst, src, 32 - val.immediate(), val.immediate(), 31, rc);
}

void Assembler::clrrwi(Register dst, Register src, const Operand& val,
                       RCBit rc) {
  DCHECK((32 > val.immediate()) && (val.immediate() >= 0));
  rlwinm(dst, src, 0, 0, 31 - val.immediate(), rc);
}

void Assembler::clrlwi(Register dst, Register src, const Operand& val,
                       RCBit rc) {
  DCHECK((32 > val.immediate()) && (val.immediate() >= 0));
  rlwinm(dst, src, 0, val.immediate(), 31, rc);
}

void Assembler::rotlw(Register ra, Register rs, Register rb, RCBit r) {
  rlwnm(ra, rs, rb, 0, 31, r);
}

void Assembler::rotlwi(Register ra, Register rs, int sh, RCBit r) {
  rlwinm(ra, rs, sh, 0, 31, r);
}

void Assembler::rotrwi(Register ra, Register rs, int sh, RCBit r) {
  rlwinm(ra, rs, 32 - sh, 0, 31, r);
}

void Assembler::subi(Register dst, Register src, const Operand& imm) {
  addi(dst, src, Operand(-(imm.immediate())));
}

void Assembler::addc(Register dst, Register src1, Register src2, OEBit o,
                     RCBit r) {
  xo_form(EXT2 | ADDCX, dst, src1, src2, o, r);
}

void Assembler::adde(Register dst, Register src1, Register src2, OEBit o,
                     RCBit r) {
  xo_form(EXT2 | ADDEX, dst, src1, src2, o, r);
}

void Assembler::addze(Register dst, Register src1, OEBit o, RCBit r) {
  // a special xo_form
  emit(EXT2 | ADDZEX | dst.code() * B21 | src1.code() * B16 | o | r);
}

void Assembler::sub(Register dst, Register src1, Register src2, OEBit o,
                    RCBit r) {
  xo_form(EXT2 | SUBFX, dst, src2, src1, o, r);
}

void Assembler::subc(Register dst, Register src1, Register src2, OEBit o,
                     RCBit r) {
  xo_form(EXT2 | SUBFCX, dst, src2, src1, o, r);
}

void Assembler::sube(Register dst, Register src1, Register src2, OEBit o,
                     RCBit r) {
  xo_form(EXT2 | SUBFEX, dst, src2, src1, o, r);
}

void Assembler::subfic(Register dst, Register src, const Operand& imm) {
  d_form(SUBFIC, dst, src, imm.immediate(), true);
}

void Assembler::add(Register dst, Register src1, Register src2, OEBit o,
                    RCBit r) {
  xo_form(EXT2 | ADDX, dst, src1, src2, o, r);
}

// Multiply low word
void Assembler::mullw(Register dst, Register src1, Register src2, OEBit o,
                      RCBit r) {
  xo_form(EXT2 | MULLW, dst, src1, src2, o, r);
}

void Assembler::mulli(Register dst, Register src, const Operand& imm) {
  d_form(MULLI, dst, src, imm.immediate(), true);
}

// Multiply hi doubleword
void Assembler::mulhd(Register dst, Register src1, Register src2, RCBit r) {
  xo_form(EXT2 | MULHD, dst, src1, src2, LeaveOE, r);
}

// Multiply hi doubleword unsigned
void Assembler::mulhdu(Register dst, Register src1, Register src2, RCBit r) {
  xo_form(EXT2 | MULHDU, dst, src1, src2, LeaveOE, r);
}

// Multiply hi word
void Assembler::mulhw(Register dst, Register src1, Register src2, RCBit r) {
  xo_form(EXT2 | MULHWX, dst, src1, src2, LeaveOE, r);
}

// Multiply hi word unsigned
void Assembler::mulhwu(Register dst, Register src1, Register src2, RCBit r) {
  xo_form(EXT2 | MULHWUX, dst, src1, src2, LeaveOE, r);
}

// Divide word
void Assembler::divw(Register dst, Register src1, Register src2, OEBit o,
                     RCBit r) {
  xo_form(EXT2 | DIVW, dst, src1, src2, o, r);
}

// Divide word unsigned
void Assembler::divwu(Register dst, Register src1, Register src2, OEBit o,
                      RCBit r) {
  xo_form(EXT2 | DIVWU, dst, src1, src2, o, r);
}

void Assembler::addi(Register dst, Register src, const Operand& imm) {
  DCHECK(src != r0);  // use li instead to show intent
  d_form(ADDI, dst, src, imm.immediate(), true);
}

void Assembler::addis(Register dst, Register src, const Operand& imm) {
  DCHECK(src != r0);  // use lis instead to show intent
  d_form(ADDIS, dst, src, imm.immediate(), true);
}

void Assembler::addic(Register dst, Register src, const Operand& imm) {
  d_form(ADDIC, dst, src, imm.immediate(), true);
}

void Assembler::andi(Register ra, Register rs, const Operand& imm) {
  d_form(ANDIx, rs, ra, imm.immediate(), false);
}

void Assembler::andis(Register ra, Register rs, const Operand& imm) {
  d_form(ANDISx, rs, ra, imm.immediate(), false);
}

void Assembler::ori(Register ra, Register rs, const Operand& imm) {
  d_form(ORI, rs, ra, imm.immediate(), false);
}

void Assembler::oris(Register dst, Register src, const Operand& imm) {
  d_form(ORIS, src, dst, imm.immediate(), false);
}

void Assembler::cmpi(Register src1, const Operand& src2, CRegister cr) {
  intptr_t imm16 = src2.immediate();
  int L = 1;
  DCHECK(is_int16(imm16));
  DCHECK(cr.code() >= 0 && cr.code() <= 7);
  imm16 &= kImm16Mask;
  emit(CMPI | cr.code() * B23 | L * B21 | src1.code() * B16 | imm16);
}

void Assembler::cmpli(Register src1, const Operand& src2, CRegister cr) {
  uintptr_t uimm16 = src2.immediate();
  int L = 1;
  DCHECK(is_uint16(uimm16));
  DCHECK(cr.code() >= 0 && cr.code() <= 7);
  uimm16 &= kImm16Mask;
  emit(CMPLI | cr.code() * B23 | L * B21 | src1.code() * B16 | uimm16);
}

void Assembler::cmpwi(Register src1, const Operand& src2, CRegister cr) {
  intptr_t imm16 = src2.immediate();
  int L = 0;
  int pos = pc_offset();
  DCHECK(is_int16(imm16));
  DCHECK(cr.code() >= 0 && cr.code() <= 7);
  imm16 &= kImm16Mask;

  // For cmpwi against 0, save postition and cr for later examination
  // of potential optimization.
  if (imm16 == 0 && pos > 0 && last_bound_pos_ != pos) {
    optimizable_cmpi_pos_ = pos;
    cmpi_cr_ = cr;
  }
  emit(CMPI | cr.code() * B23 | L * B21 | src1.code() * B16 | imm16);
}

void Assembler::cmplwi(Register src1, const Operand& src2, CRegister cr) {
  uintptr_t uimm16 = src2.immediate();
  int L = 0;
  DCHECK(is_uint16(uimm16));
  DCHECK(cr.code() >= 0 && cr.code() <= 7);
  uimm16 &= kImm16Mask;
  emit(CMPLI | cr.code() * B23 | L * B21 | src1.code() * B16 | uimm16);
}

void Assembler::isel(Register rt, Register ra, Register rb, int cb) {
  emit(EXT2 | ISEL | rt.code() * B21 | ra.code() * B16 | rb.code() * B11 |
       cb * B6);
}

// Pseudo op - load immediate
void Assembler::li(Register dst, const Operand& imm) {
  d_form(ADDI, dst, r0, imm.immediate(), true);
}

void Assembler::lis(Register dst, const Operand& imm) {
  d_form(ADDIS, dst, r0, imm.immediate(), true);
}

// Pseudo op - move register
void Assembler::mr(Register dst, Register src) {
  // actually or(dst, src, src)
  orx(dst, src, src);
}

void Assembler::lbz(Register dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  d_form(LBZ, dst, src.ra(), src.offset(), true);
}

void Assembler::lhz(Register dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  d_form(LHZ, dst, src.ra(), src.offset(), true);
}

void Assembler::lwz(Register dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  d_form(LWZ, dst, src.ra(), src.offset(), true);
}

void Assembler::lwzu(Register dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  d_form(LWZU, dst, src.ra(), src.offset(), true);
}

void Assembler::lha(Register dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  d_form(LHA, dst, src.ra(), src.offset(), true);
}

void Assembler::lwa(Register dst, const MemOperand& src) {
  int offset = src.offset();
  DCHECK(src.ra_ != r0);
  CHECK(!(offset & 3) && is_int16(offset));
  offset = kImm16Mask & offset;
  emit(LD | dst.code() * B21 | src.ra().code() * B16 | offset | 2);
}

void Assembler::stb(Register dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  d_form(STB, dst, src.ra(), src.offset(), true);
}

void Assembler::sth(Register dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  d_form(STH, dst, src.ra(), src.offset(), true);
}

void Assembler::stw(Register dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  d_form(STW, dst, src.ra(), src.offset(), true);
}

void Assembler::stwu(Register dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  d_form(STWU, dst, src.ra(), src.offset(), true);
}

void Assembler::neg(Register rt, Register ra, OEBit o, RCBit r) {
  emit(EXT2 | NEGX | rt.code() * B21 | ra.code() * B16 | o | r);
}

// 64bit specific instructions
void Assembler::ld(Register rd, const MemOperand& src) {
  int offset = src.offset();
  DCHECK(src.ra_ != r0);
  CHECK(!(offset & 3) && is_int16(offset));
  offset = kImm16Mask & offset;
  emit(LD | rd.code() * B21 | src.ra().code() * B16 | offset);
}

void Assembler::ldu(Register rd, const MemOperand& src) {
  int offset = src.offset();
  DCHECK(src.ra_ != r0);
  CHECK(!(offset & 3) && is_int16(offset));
  offset = kImm16Mask & offset;
  emit(LD | rd.code() * B21 | src.ra().code() * B16 | offset | 1);
}

void Assembler::std(Register rs, const MemOperand& src) {
  int offset = src.offset();
  DCHECK(src.ra_ != r0);
  CHECK(!(offset & 3) && is_int16(offset));
  offset = kImm16Mask & offset;
  emit(STD | rs.code() * B21 | src.ra().code() * B16 | offset);
}

void Assembler::stdu(Register rs, const MemOperand& src) {
  int offset = src.offset();
  DCHECK(src.ra_ != r0);
  CHECK(!(offset & 3) && is_int16(offset));
  offset = kImm16Mask & offset;
  emit(STD | rs.code() * B21 | src.ra().code() * B16 | offset | 1);
}

void Assembler::rldic(Register ra, Register rs, int sh, int mb, RCBit r) {
  md_form(EXT5 | RLDIC, ra, rs, sh, mb, r);
}

void Assembler::rldicl(Register ra, Register rs, int sh, int mb, RCBit r) {
  md_form(EXT5 | RLDICL, ra, rs, sh, mb, r);
}

void Assembler::rldcl(Register ra, Register rs, Register rb, int mb, RCBit r) {
  mds_form(EXT5 | RLDCL, ra, rs, rb, mb, r);
}

void Assembler::rldicr(Register ra, Register rs, int sh, int me, RCBit r) {
  md_form(EXT5 | RLDICR, ra, rs, sh, me, r);
}

void Assembler::sldi(Register dst, Register src, const Operand& val, RCBit rc) {
  DCHECK((64 > val.immediate()) && (val.immediate() >= 0));
  rldicr(dst, src, val.immediate(), 63 - val.immediate(), rc);
}

void Assembler::srdi(Register dst, Register src, const Operand& val, RCBit rc) {
  DCHECK((64 > val.immediate()) && (val.immediate() >= 0));
  rldicl(dst, src, 64 - val.immediate(), val.immediate(), rc);
}

void Assembler::clrrdi(Register dst, Register src, const Operand& val,
                       RCBit rc) {
  DCHECK((64 > val.immediate()) && (val.immediate() >= 0));
  rldicr(dst, src, 0, 63 - val.immediate(), rc);
}

void Assembler::clrldi(Register dst, Register src, const Operand& val,
                       RCBit rc) {
  DCHECK((64 > val.immediate()) && (val.immediate() >= 0));
  rldicl(dst, src, 0, val.immediate(), rc);
}

void Assembler::rldimi(Register ra, Register rs, int sh, int mb, RCBit r) {
  md_form(EXT5 | RLDIMI, ra, rs, sh, mb, r);
}

void Assembler::sradi(Register ra, Register rs, int sh, RCBit r) {
  int sh0_4 = sh & 0x1F;
  int sh5 = (sh >> 5) & 0x1;

  emit(EXT2 | SRADIX | rs.code() * B21 | ra.code() * B16 | sh0_4 * B11 |
       sh5 * B1 | r);
}

void Assembler::rotld(Register ra, Register rs, Register rb, RCBit r) {
  rldcl(ra, rs, rb, 0, r);
}

void Assembler::rotldi(Register ra, Register rs, int sh, RCBit r) {
  rldicl(ra, rs, sh, 0, r);
}

void Assembler::rotrdi(Register ra, Register rs, int sh, RCBit r) {
  rldicl(ra, rs, 64 - sh, 0, r);
}

void Assembler::mulld(Register dst, Register src1, Register src2, OEBit o,
                      RCBit r) {
  xo_form(EXT2 | MULLD, dst, src1, src2, o, r);
}

void Assembler::divd(Register dst, Register src1, Register src2, OEBit o,
                     RCBit r) {
  xo_form(EXT2 | DIVD, dst, src1, src2, o, r);
}

void Assembler::divdu(Register dst, Register src1, Register src2, OEBit o,
                      RCBit r) {
  xo_form(EXT2 | DIVDU, dst, src1, src2, o, r);
}

// Prefixed instructions.
#define GENERATE_PREFIX_SUFFIX_BITS(immediate, prefix, suffix)      \
  CHECK(is_int34(immediate));                                       \
  int32_t prefix =                                                  \
      SIGN_EXT_IMM18((immediate >> 16) & kImm18Mask); /* 18 bits.*/ \
  int16_t suffix = immediate & kImm16Mask;            /* 16 bits.*/ \
  DCHECK(is_int18(prefix));

void Assembler::paddi(Register dst, Register src, const Operand& imm) {
  CHECK(CpuFeatures::IsSupported(PPC_10_PLUS));
  DCHECK(src != r0);  // use pli instead to show intent.
  intptr_t immediate = imm.immediate();
  GENERATE_PREFIX_SUFFIX_BITS(immediate, hi, lo)
  BlockTrampolinePoolScope block_trampoline_pool(this);
  pload_store_mls(Operand(hi));
  addi(dst, src, Operand(lo));
}

void Assembler::pli(Register dst, const Operand& imm) {
  CHECK(CpuFeatures::IsSupported(PPC_10_PLUS));
  intptr_t immediate = imm.immediate();
  GENERATE_PREFIX_SUFFIX_BITS(immediate, hi, lo)
  BlockTrampolinePoolScope block_trampoline_pool(this);
  pload_store_mls(Operand(hi));
  li(dst, Operand(lo));
}

void Assembler::psubi(Register dst, Register src, const Operand& imm) {
  paddi(dst, src, Operand(-(imm.immediate())));
}

void Assembler::plbz(Register dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  int64_t offset = src.offset();
  GENERATE_PREFIX_SUFFIX_BITS(offset, hi, lo)
  BlockTrampolinePoolScope block_trampoline_pool(this);
  pload_store_mls(Operand(hi));
  lbz(dst, MemOperand(src.ra(), lo));
}

void Assembler::plhz(Register dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  int64_t offset = src.offset();
  GENERATE_PREFIX_SUFFIX_BITS(offset, hi, lo)
  BlockTrampolinePoolScope block_trampoline_pool(this);
  pload_store_mls(Operand(hi));
  lhz(dst, MemOperand(src.ra(), lo));
}

void Assembler::plha(Register dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  int64_t offset = src.offset();
  GENERATE_PREFIX_SUFFIX_BITS(offset, hi, lo)
  BlockTrampolinePoolScope block_trampoline_pool(this);
  pload_store_mls(Operand(hi));
  lha(dst, MemOperand(src.ra(), lo));
}

void Assembler::plwz(Register dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  int64_t offset = src.offset();
  GENERATE_PREFIX_SUFFIX_BITS(offset, hi, lo)
  BlockTrampolinePoolScope block_trampoline_pool(this);
  pload_store_mls(Operand(hi));
  lwz(dst, MemOperand(src.ra(), lo));
}

void Assembler::plwa(Register dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  int64_t offset = src.offset();
  GENERATE_PREFIX_SUFFIX_BITS(offset, hi, lo)
  BlockTrampolinePoolScope block_trampoline_pool(this);
  pload_store_8ls(Operand(hi));
  emit(PPLWA | dst.code() * B21 | src.ra().code() * B16 | (lo & kImm16Mask));
}

void Assembler::pld(Register dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  int64_t offset = src.offset();
  GENERATE_PREFIX_SUFFIX_BITS(offset, hi, lo)
  BlockTrampolinePoolScope block_trampoline_pool(this);
  pload_store_8ls(Operand(hi));
  emit(PPLD | dst.code() * B21 | src.ra().code() * B16 | (lo & kImm16Mask));
}

void Assembler::plfs(DoubleRegister dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  int64_t offset = src.offset();
  GENERATE_PREFIX_SUFFIX_BITS(offset, hi, lo)
  BlockTrampolinePoolScope block_trampoline_pool(this);
  pload_store_mls(Operand(hi));
  lfs(dst, MemOperand(src.ra(), lo));
}

void Assembler::plfd(DoubleRegister dst, const MemOperand& src) {
  DCHECK(src.ra_ != r0);
  int64_t offset = src.offset();
  GENERATE_PREFIX_SUFFIX_BITS(offset, hi, lo)
  BlockTrampolinePoolScope block_trampoline_pool(this);
  pload_store_mls(Operand(hi));
  lfd(dst, MemOperand(src.ra(), lo));
}

void Assembler::pstb(Register src, const MemOperand& dst) {
  DCHECK(dst.ra_ != r0);
  int64_t offset = dst.offset();
  GENERATE_PREFIX_SUFFIX_BITS(offset, hi, lo)
  BlockTrampolinePoolScope block_trampoline_pool(this);
  pload_store_mls(Operand(hi));
  stb(src, MemOperand(dst.ra(), lo));
}

void Assembler::psth(Register src, const MemOperand& dst) {
  DCHECK(dst.ra_ != r0);
  int64_t offset = dst.offset();
  GENERATE_PREFIX_SUFFIX_BITS(offset, hi, lo)
  BlockTrampolinePoolScope block_trampoline_pool(this);
  pload_store_mls(Operand(hi));
  sth(src, MemOperand(dst.ra(), lo));
}

void Assembler::pstw(Register src, const MemOperand& dst) {
  DCHECK(dst.ra_ != r0);
  int64_t offset = dst.offset();
  GENERATE_PREFIX_SUFFIX_BITS(offset, hi, lo)
  BlockTrampolinePoolScope block_trampoline_pool(this);
  pload_store_mls(Operand(hi));
  stw(src, MemOperand(dst.ra(), lo));
}

void Assembler::pstd(Register src, const MemOperand& dst) {
  DCHECK(dst.ra_ != r0);
  int64_t offset = dst.offset();
  GENERATE_PREFIX_SUFFIX_BITS(offset, hi, lo)
  BlockTrampolinePoolScope block_trampoline_pool(this);
  pload_store_8ls(Operand(hi));
  emit(PPSTD | src.code() * B21 | dst.ra().code() * B16 | (lo & kImm16Mask));
}

void Assembler::pstfs(const DoubleRegister src, const MemOperand& dst) {
  DCHECK(dst.ra_ != r0);
  int64_t offset = dst.offset();
  GENERATE_PREFIX_SUFFIX_BITS(offset, hi, lo)
  BlockTrampolinePoolScope block_trampoline_pool(this);
  pload_store_mls(Operand(hi));
  stfs(src, MemOperand(dst.ra(), lo));
}

void Assembler::pstfd(const DoubleRegister src, const MemOperand& dst) {
  DCHECK(dst.ra_ != r0);
  int64_t offset = dst.offset();
  GENERATE_PREFIX_SUFFIX_BITS(offset, hi, lo)
  BlockTrampolinePoolScope block_trampoline_pool(this);
  pload_store_mls(Operand(hi));
  stfd(src, MemOperand(dst.ra(), lo));
}
#undef GENERATE_PREFIX_SUFFIX_BITS

int Assembler::instructions_required_for_mov(Register dst,
                                             const Operand& src) const {
  bool canOptimize =
      !(src.must_output_reloc_info(this) || is_trampoline_pool_blocked());
  if (use_constant_pool_for_mov(dst, src, canOptimize)) {
    if (ConstantPoolAccessIsInOverflow()) {
      return kMovInstructionsConstantPool + 1;
    }
    return kMovInstructionsConstantPool;
  }
  DCHECK(!canOptimize);
  return kMovInstructionsNoConstantPool;
}

bool Assembler::use_constant_pool_for_mov(Register dst, const Operand& src,
                                          bool canOptimize) const {
  if (!V8_EMBEDDED_CONSTANT_POOL_BOOL || !is_constant_pool_available()) {
    // If there is no constant pool available, we must use a mov
    // immediate sequence.
    return false;
  }
  intptr_t value = src.immediate();
  bool allowOverflow = !((canOptimize && is_int32(value)) || dst == r0);
  if (canOptimize &&
      (is_int16(value) ||
       (CpuFeatures::IsSupported(PPC_10_PLUS) && is_int34(value)))) {
    // Prefer a single-instruction load-immediate.
    return false;
  }
  if (!allowOverflow && ConstantPoolAccessIsInOverflow()) {
    // Prefer non-relocatable two-instruction bitwise-mov32 over
    // overflow sequence.
    return false;
  }

  return true;
}

void Assembler::EnsureSpaceFor(int space_needed) {
  if (buffer_space() <= (kGap + space_needed)) {
    GrowBuffer(space_needed);
  }
}

bool Operand::must_output_reloc_info(const Assembler* assembler) const {
  if (rmode_ == RelocInfo::EXTERNAL_REFERENCE) {
    if (assembler != nullptr && assembler->predictable_code_size()) return true;
    return assembler->options().record_reloc_info_for_serialization;
  } else if (RelocInfo::IsNoInfo(rmode_)) {
    return false;
  }
  return true;
}

// Primarily used for loading constants
// This should really move to be in macro-assembler as it
// is really a pseudo instruction
// Some usages of this intend for a FIXED_SEQUENCE to be used
// Todo - break this dependency so we can optimize mov() in general
// and only use the generic version when we require a fixed sequence
void Assembler::mov(Register dst, const Operand& src) {
  intptr_t value;
  if (src.IsHeapNumberRequest()) {
    RequestHeapNumber(src.heap_number_request());
    value = 0;
  } else {
    value = src.immediate();
  }
  bool relocatable = src.must_output_reloc_info(this);
  bool canOptimize;

  canOptimize =
      !(relocatable ||
        (is_trampoline_pool_blocked() &&
         (!is_int16(value) ||
          !(CpuFeatures::IsSupported(PPC_10_PLUS) && is_int34(value)))));

  if (!src.IsHeapNumberRequest() &&
      use_constant_pool_for_mov(dst, src, canOptimize)) {
    DCHECK(is_constant_pool_available());
    if (relocatable) {
      RecordRelocInfo(src.rmode_);
    }
    ConstantPoolEntry::Access access = ConstantPoolAddEntry(src.rmode_, value);
    if (access == ConstantPoolEntry::OVERFLOWED) {
      addis(dst, kConstantPoolRegister, Operand::Zero());
      ld(dst, MemOperand(dst, 0));
    } else {
      ld(dst, MemOperand(kConstantPoolRegister, 0));
    }
    return;
  }

  if (canOptimize) {
    if (is_int16(value)) {
      li(dst, Operand(value));
    } else if (CpuFeatures::IsSupported(PPC_10_PLUS) && is_int34(value)) {
      pli(dst, Operand(value));
    } else {
      uint16_t u16;
      if (is_int32(value)) {
        lis(dst, Operand(value >> 16));
      } else {
        if (is_int48(value)) {
          li(dst, Operand(value >> 32));
        } else {
          lis(dst, Operand(value >> 48));
          u16 = ((value >> 32) & 0xFFFF);
          if (u16) {
            ori(dst, dst, Operand(u16));
          }
        }
        sldi(dst, dst, Operand(32));
        u16 = ((value >> 16) & 0xFFFF);
        if (u16) {
          oris(dst, dst, Operand(u16));
        }
      }
      u16 = (value & 0xFFFF);
      if (u16) {
        ori(dst, dst, Operand(u16));
      }
    }
    return;
  }

  DCHECK(!canOptimize);
  if (relocatable) {
    RecordRelocInfo(src.rmode_);
  }
  bitwise_mov(dst, value);
}

void Assembler::bitwise_mov(Register dst, intptr_t value) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  int32_t hi_32 = static_cast<int32_t>(value >> 32);
  int32_t lo_32 = static_cast<int32_t>(value);
  int hi_word = static_cast<int>(hi_32 >> 16);
  int lo_word = static_cast<int>(hi_32 & 0xFFFF);
  lis(dst, Operand(SIGN_EXT_IMM16(hi_word)));
  ori(dst, dst, Operand(lo_word));
  sldi(dst, dst, Operand(32));
  hi_word = static_cast<int>(((lo_32 >> 16) & 0xFFFF));
  lo_word = static_cast<int>(lo_32 & 0xFFFF);
  oris(dst, dst, Operand(hi_word));
  ori(dst, dst, Operand(lo_word));
}

void Assembler::bitwise_mov32(Register dst, int32_t value) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  int hi_word = static_cast<int>(value >> 16);
  int lo_word = static_cast<int>(value & 0xFFFF);
  lis(dst, Operand(SIGN_EXT_IMM16(hi_word)));
  ori(dst, dst, Operand(lo_word));
}

void Assembler::bitwise_add32(Register dst, Register src, int32_t value) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (is_int16(value)) {
    addi(dst, src, Operand(value));
    nop();
  } else {
    int hi_word = static_cast<int>(value >> 16);
    int lo_word = static_cast<int>(value & 0xFFFF);
    if (lo_word & 0x8000) hi_word++;
    addis(dst, src, Operand(SIGN_EXT_IMM16(hi_word)));
    addic(dst, dst, Operand(SIGN_EXT_IMM16(lo_word)));
  }
}

void Assembler::patch_pc_address(Register dst, int pc_offset,
                                 int return_address_offset) {
  DCHECK(is_int16(return_address_offset));
  Assembler patching_assembler(
      AssemblerOptions{},
      ExternalAssemblerBuffer(buffer_start_ + pc_offset, kInstrSize + kGap));
  patching_assembler.addi(dst, dst, Operand(return_address_offset));
}

void Assembler::mov_label_offset(Register dst, Label* label) {
  int position = link(label);
  if (label->is_bound()) {
    // Load the position of the label relative to the generated code object.
    mov(dst,
        Operand(position + InstructionStream::kHeaderSize - kHeapObjectTag));
  } else {
    // Encode internal reference to unbound label. We use a dummy opcode
    // such that it won't collide with any opcode that might appear in the
    // label's chain.  Encode the destination register in the 2nd instruction.
    int link = position - pc_offset();
    DCHECK_EQ(0, link & 3);
    link >>= 2;
    DCHECK(is_int26(link));

    // When the label is bound, these instructions will be patched
    // with a 2 instruction mov sequence that will load the
    // destination register with the position of the label from the
    // beginning of the code.
    //
    // target_at extracts the link and target_at_put patches the instructions.
    BlockTrampolinePoolScope block_trampoline_pool(this);
    emit(kUnboundMovLabelOffsetOpcode | (link & kImm26Mask));
    emit(dst.code());
  }
}

void Assembler::add_label_offset(Register dst, Register base, Label* label,
                                 int delta) {
  int position = link(label);
  if (label->is_bound()) {
    // dst = base + position + delta
    position += delta;
    bitwise_add32(dst, base, position);
  } else {
    // Encode internal reference to unbound label. We use a dummy opcode
    // such that it won't collide with any opcode that might appear in the
    // label's chain.  Encode the operands in the 2nd instruction.
    int link = position - pc_offset();
    DCHECK_EQ(0, link & 3);
    link >>= 2;
    DCHECK(is_int26(link));
    BlockTrampolinePoolScope block_trampoline_pool(this);

    emit((is_int22(delta) ? kUnboundAddLabelOffsetOpcode
                          : kUnboundAddLabelLongOffsetOpcode) |
         (link & kImm26Mask));
    emit(dst.code() * B27 | base.code() * B22 | (delta & kImm22Mask));

    if (!is_int22(delta)) {
      emit(delta);
    }
  }
}

void Assembler::mov_label_addr(Register dst, Label* label) {
  CheckBuffer();
  RecordRelocInfo(RelocInfo::INTERNAL_REFERENCE_ENCODED);
  int position = link(label);
  if (label->is_bound()) {
    // Keep internal references relative until EmitRelocations.
    bitwise_mov(dst, position);
  } else {
    // Encode internal reference to unbound label. We use a dummy opcode
    // such that it won't collide with any opcode that might appear in the
    // label's chain.  Encode the destination register in the 2nd instruction.
    int link = position - pc_offset();
    DCHECK_EQ(0, link & 3);
    link >>= 2;
    DCHECK(is_int26(link));

    // When the label is bound, these instructions will be patched
    // with a multi-instruction mov sequence that will load the
    // destination register with the address of the label.
    //
    // target_at extracts the link and target_at_put patches the instructions.
    BlockTrampolinePoolScope block_trampoline_pool(this);
    emit(kUnboundMovLabelAddrOpcode | (link & kImm26Mask));
    emit(dst.code());
    DCHECK_GE(kMovInstructionsNoConstantPool, 2);
    for (int i = 0; i < kMovInstructionsNoConstantPool - 2; i++) nop();
  }
}

void Assembler::emit_label_addr(Label* label) {
  CheckBuffer();
  RecordRelocInfo(RelocInfo::INTERNAL_REFERENCE);
  int position = link(label);
  if (label->is_bound()) {
    // Keep internal references relative until EmitRelocations.
    dp(position);
  } else {
    // Encode internal reference to unbound label. We use a dummy opcode
    // such that it won't collide with any opcode that might appear in the
    // label's chain.
    int link = position - pc_offset();
    DCHECK_EQ(0, link & 3);
    link >>= 2;
    DCHECK(is_int26(link));

    // When the label is bound, the instruction(s) will be patched
    // as a jump table entry containing the label address.  target_at extracts
    // the link and target_at_put patches the instruction(s).
    BlockTrampolinePoolScope block_trampoline_pool(this);
    emit(kUnboundJumpTableEntryOpcode | (link & kImm26Mask));
    nop();
  }
}

// Special register instructions
void Assembler::crxor(int bt, int ba, int bb) {
  emit(EXT1 | CRXOR | bt * B21 | ba * B16 | bb * B11);
}

void Assembler::creqv(int bt, int ba, int bb) {
  emit(EXT1 | CREQV | bt * B21 | ba * B16 | bb * B11);
}

void Assembler::mflr(Register dst) {
  emit(EXT2 | MFSPR | dst.code() * B21 | 256 << 11);  // Ignore RC bit
}

void Assembler::mtlr(Register src) {
  emit(EXT2 | MTSPR | src.code() * B21 | 256 << 11);  // Ignore RC bit
}

void Assembler::mtctr(Register src) {
  emit(EXT2 | MTSPR | src.code() * B21 | 288 << 11);  // Ignore RC bit
}

void Assembler::mtxer(Register src) {
  emit(EXT2 | MTSPR | src.code() * B21 | 32 << 11);
}

void Assembler::mcrfs(CRegister cr, FPSCRBit bit) {
  DCHECK_LT(static_cast<int>(bit), 32);
  int bf = cr.code();
  int bfa = bit / CRWIDTH;
  emit(EXT4 | MCRFS | bf * B23 | bfa * B18);
}

void Assembler::mfcr(Register dst) { emit(EXT2 | MFCR | dst.code() * B21); }

void Assembler::mtcrf(Register src, uint8_t FXM) {
  emit(MTCRF | src.code() * B21 | FXM * B12);
}
void Assembler::mffprd(Register dst, DoubleRegister src) {
  emit(EXT2 | MFVSRD | src.code() * B21 | dst.code() * B16);
}

void Assembler::mffprwz(Register dst, DoubleRegister src) {
  emit(EXT2 | MFVSRWZ | src.code() * B21 | dst.code() * B16);
}

void Assembler::mtfprd(DoubleRegister dst, Register src) {
  emit(EXT2 | MTVSRD | dst.code() * B21 | src.code() * B16);
}

void Assembler::mtfprwz(DoubleRegister dst, Register src) {
  emit(EXT2 | MTVSRWZ | dst.code() * B21 | src.code() * B16);
}

void Assembler::mtfprwa(DoubleRegister dst, Register src) {
  emit(EXT2 | MTVSRWA | dst.code() * B21 | src.code() * B16);
}

// Exception-generating instructions and debugging support.
// Stops with a non-negative code less than kNumOfWatchedStops support
// enabling/disabling and a counter feature. See simulator-ppc.h .
void Assembler::stop(Condition cond, int32_t code, CRegister cr) {
  if (cond != al) {
    Label skip;
    b(NegateCondition(cond), &skip, cr);
    bkpt(0);
    bind(&skip);
  } else {
    bkpt(0);
  }
}

void Assembler::bkpt(uint32_t imm16) { emit(0x7D821008); }

void Assembler::dcbf(Register ra, Register rb) {
  emit(EXT2 | DCBF | ra.code() * B16 | rb.code() * B11);
}

void Assembler::sync() { emit(EXT2 | SYNC); }

void Assembler::lwsync() { emit(EXT2 | SYNC | 1 * B21); }

void Assembler::icbi(Register ra, Register rb) {
  emit(EXT2 | ICBI | ra.code() * B16 | rb.code() * B11);
}

void Assembler::isync() { emit(EXT1 | ISYNC); }

// Floating point support

void Assembler::lfd(const DoubleRegister frt, const MemOperand& src) {
  int offset = src.offset();
  Register ra = src.ra();
  DCHECK(ra != r0);
  CHECK(is_int16(offset));
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(LFD | frt.code() * B21 | ra.code() * B16 | imm16);
}

void Assembler::lfdu(const DoubleRegister frt, const MemOperand& src) {
  int offset = src.offset();
  Register ra = src.ra();
  DCHECK(ra != r0);
  CHECK(is_int16(offset));
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(LFDU | frt.code() * B21 | ra.code() * B16 | imm16);
}

void Assembler::lfs(const DoubleRegister frt, const MemOperand& src) {
  int offset = src.offset();
  Register ra = src.ra();
  CHECK(is_int16(offset));
  DCHECK(ra != r0);
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(LFS | frt.code() * B21 | ra.code() * B16 | imm16);
}

void Assembler::lfsu(const DoubleRegister frt, const MemOperand& src) {
  int offset = src.offset();
  Register ra = src.ra();
  CHECK(is_int16(offset));
  DCHECK(ra != r0);
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(LFSU | frt.code() * B21 | ra.code() * B16 | imm16);
}

void Assembler::stfd(const DoubleRegister frs, const MemOperand& src) {
  int offset = src.offset();
  Register ra = src.ra();
  CHECK(is_int16(offset));
  DCHECK(ra != r0);
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(STFD | frs.code() * B21 | ra.code() * B16 | imm16);
}

void Assembler::stfdu(const DoubleRegister frs, const MemOperand& src) {
  int offset = src.offset();
  Register ra = src.ra();
  CHECK(is_int16(offset));
  DCHECK(ra != r0);
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(STFDU | frs.code() * B21 | ra.code() * B16 | imm16);
}

void Assembler::stfs(const DoubleRegister frs, const MemOperand& src) {
  int offset = src.offset();
  Register ra = src.ra();
  CHECK(is_int16(offset));
  DCHECK(ra != r0);
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(STFS | frs.code() * B21 | ra.code() * B16 | imm16);
}

void Assembler::stfsu(const DoubleRegister frs, const MemOperand& src) {
  int offset = src.offset();
  Register ra = src.ra();
  CHECK(is_int16(offset));
  DCHECK(ra != r0);
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(STFSU | frs.code() * B21 | ra.code() * B16 | imm16);
}

void Assembler::fsub(const DoubleRegister frt, const DoubleRegister fra,
                     const DoubleRegister frb, RCBit rc) {
  a_form(EXT4 | FSUB, frt, fra, frb, rc);
}

void Assembler::fadd(const DoubleRegister frt, const DoubleRegister fra,
                     const DoubleRegister frb, RCBit rc) {
  a_form(EXT4 | FADD, frt, fra, frb, rc);
}

void Assembler::fmul(const DoubleRegister frt, const DoubleRegister fra,
                     const DoubleRegister frc, RCBit rc) {
  emit(EXT4 | FMUL | frt.code() * B21 | fra.code() * B16 | frc.code() * B6 |
       rc);
}

void Assembler::fcpsgn(const DoubleRegister frt, const DoubleRegister fra,
                       const DoubleRegister frb, RCBit rc) {
  emit(EXT4 | FCPSGN | frt.code() * B21 | fra.code() * B16 | frb.code() * B11 |
       rc);
}

void Assembler::fdiv(const DoubleRegister frt, const DoubleRegister fra,
                     const DoubleRegister frb, RCBit rc) {
  a_form(EXT4 | FDIV, frt, fra, frb, rc);
}

void Assembler::fcmpu(const DoubleRegister fra, const DoubleRegister frb,
                      CRegister cr) {
  DCHECK(cr.code() >= 0 && cr.code() <= 7);
  emit(EXT4 | FCMPU | cr.code() * B23 | fra.code() * B16 | frb.code() * B11);
}

void Assembler::fmr(const DoubleRegister frt, const DoubleRegister frb,
                    RCBit rc) {
  emit(EXT4 | FMR | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::fctiwz(const DoubleRegister frt, const DoubleRegister frb) {
  emit(EXT4 | FCTIWZ | frt.code() * B21 | frb.code() * B11);
}

void Assembler::fctiw(const DoubleRegister frt, const DoubleRegister frb) {
  emit(EXT4 | FCTIW | frt.code() * B21 | frb.code() * B11);
}

void Assembler::fctiwuz(const DoubleRegister frt, const DoubleRegister frb) {
  emit(EXT4 | FCTIWUZ | frt.code() * B21 | frb.code() * B11);
}

void Assembler::frin(const DoubleRegister frt, const DoubleRegister frb,
                     RCBit rc) {
  emit(EXT4 | FRIN | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::friz(const DoubleRegister frt, const DoubleRegister frb,
                     RCBit rc) {
  emit(EXT4 | FRIZ | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::frip(const DoubleRegister frt, const DoubleRegister frb,
                     RCBit rc) {
  emit(EXT4 | FRIP | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::frim(const DoubleRegister frt, const DoubleRegister frb,
                     RCBit rc) {
  emit(EXT4 | FRIM | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::frsp(const DoubleRegister frt, const DoubleRegister frb,
                     RCBit rc) {
  emit(EXT4 | FRSP | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::fcfid(const DoubleRegister frt, const DoubleRegister frb,
                      RCBit rc) {
  emit(EXT4 | FCFID | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::fcfidu(const DoubleRegister frt, const DoubleRegister frb,
                       RCBit rc) {
  emit(EXT4 | FCFIDU | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::fcfidus(const DoubleRegister frt, const DoubleRegister frb,
                        RCBit rc) {
  emit(EXT3 | FCFIDUS | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::fcfids(const DoubleRegister frt, const DoubleRegister frb,
                       RCBit rc) {
  emit(EXT3 | FCFIDS | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::fctid(const DoubleRegister frt, const DoubleRegister frb,
                      RCBit rc) {
  emit(EXT4 | FCTID | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::fctidz(const DoubleRegister frt, const DoubleRegister frb,
                       RCBit rc) {
  emit(EXT4 | FCTIDZ | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::fctidu(const DoubleRegister frt, const DoubleRegister frb,
                       RCBit rc) {
  emit(EXT4 | FCTIDU | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::fctiduz(const DoubleRegister frt, const DoubleRegister frb,
                        RCBit rc) {
  emit(EXT4 | FCTIDUZ | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::fsel(const DoubleRegister frt, const DoubleRegister fra,
                     const DoubleRegister frc, const DoubleRegister frb,
                     RCBit rc) {
  emit(EXT4 | FSEL | frt.code() * B21 | fra.code() * B16 | frb.code() * B11 |
       frc.code() * B6 | rc);
}

void Assembler::fneg(const DoubleRegister frt, const DoubleRegister frb,
                     RCBit rc) {
  emit(EXT4 | FNEG | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::mtfsb0(FPSCRBit bit, RCBit rc) {
  DCHECK_LT(static_cast<int>(bit), 32);
  int bt = bit;
  emit(EXT4 | MTFSB0 | bt * B21 | rc);
}

void Assembler::mtfsb1(FPSCRBit bit, RCBit rc) {
  DCHECK_LT(static_cast<int>(bit), 32);
  int bt = bit;
  emit(EXT4 | MTFSB1 | bt * B21 | rc);
}

void Assembler::mtfsfi(int bf, int immediate, RCBit rc) {
  emit(EXT4 | MTFSFI | bf * B23 | immediate * B12 | rc);
}

void Assembler::mffs(const DoubleRegister frt, RCBit rc) {
  emit(EXT4 | MFFS | frt.code() * B21 | rc);
}

void Assembler::mtfsf(const DoubleRegister frb, bool L, int FLM, bool W,
                      RCBit rc) {
  emit(EXT4 | MTFSF | frb.code() * B11 | W * B16 | FLM * B17 | L * B25 | rc);
}

void Assembler::fsqrt(const DoubleRegister frt, const DoubleRegister frb,
                      RCBit rc) {
  emit(EXT4 | FSQRT | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::fabs(const DoubleRegister frt, const DoubleRegister frb,
                     RCBit rc) {
  emit(EXT4 | FABS | frt.code() * B21 | frb.code() * B11 | rc);
}

void Assembler::fmadd(const DoubleRegister frt, const DoubleRegister fra,
                      const DoubleRegister frc, const DoubleRegister frb,
                      RCBit rc) {
  emit(EXT4 | FMADD | frt.code() * B21 | fra.code() * B16 | frb.code() * B11 |
       frc.code() * B6 | rc);
}

void Assembler::fmsub(const DoubleRegister frt, const DoubleRegister fra,
                      const DoubleRegister frc, const DoubleRegister frb,
                      RCBit rc) {
  emit(EXT4 | FMSUB | frt.code() * B21 | fra.code() * B16 | frb.code() * B11 |
       frc.code() * B6 | rc);
}

// Vector instructions
void Assembler::mfvsrd(const Register ra, const Simd128Register rs) {
  int SX = 1;
  emit(MFVSRD | rs.code() * B21 | ra.code() * B16 | SX);
}

void Assembler::mfvsrwz(const Register ra, const Simd128Register rs) {
  int SX = 1;
  emit(MFVSRWZ | rs.code() * B21 | ra.code() * B16 | SX);
}

void Assembler::mtvsrd(const Simd128Register rt, const Register ra) {
  int TX = 1;
  emit(MTVSRD | rt.code() * B21 | ra.code() * B16 | TX);
}

void Assembler::mtvsrdd(const Simd128Register rt, const Register ra,
                        const Register rb) {
  int TX = 1;
  emit(MTVSRDD | rt.code() * B21 | ra.code() * B16 | rb.code() * B11 | TX);
}

void Assembler::lxvd(const Simd128Register rt, const MemOperand& src) {
  CHECK(src.rb().is_valid());
  int TX = 1;
  emit(LXVD | rt.code() * B21 | src.ra().code() * B16 | src.rb().code() * B11 |
       TX);
}

void Assembler::lxvx(const Simd128Register rt, const MemOperand& src) {
  CHECK(src.rb().is_valid());
  int TX = 1;
  emit(LXVX | rt.code() * B21 | src.ra().code() * B16 | src.rb().code() * B11 |
       TX);
}

void Assembler::lxsdx(const Simd128Register rt, const MemOperand& src) {
  CHECK(src.rb().is_valid());
  int TX = 1;
  emit(LXSDX | rt.code() * B21 | src.ra().code() * B16 | src.rb().code() * B11 |
       TX);
}

void Assembler::lxsibzx(const Simd128Register rt, const MemOperand& src) {
  CHECK(src.rb().is_valid());
  int TX = 1;
  emit(LXSIBZX | rt.code() * B21 | src.ra().code() * B16 |
       src.rb().code() * B11 | TX);
}

void Assembler::lxsihzx(const Simd128Register rt, const MemOperand& src) {
  CHECK(src.rb().is_valid());
  int TX = 1;
  emit(LXSIHZX | rt.code() * B21 | src.ra().code() * B16 |
       src.rb().code() * B11 | TX);
}

void Assembler::lxsiwzx(const Simd128Register rt, const MemOperand& src) {
  CHECK(src.rb().is_valid());
  int TX = 1;
  emit(LXSIWZX | rt.code() * B21 | src.ra().code() * B16 |
       src.rb().code() * B11 | TX);
}

void Assembler::stxsdx(const Simd128Register rs, const MemOperand& dst) {
  CHECK(dst.rb().is_valid());
  int SX = 1;
  emit(STXSDX | rs.code() * B21 | dst.ra().code() * B16 |
       dst.rb().code() * B11 | SX);
}

void Assembler::stxsibx(const Simd128Register rs, const MemOperand& dst) {
  CHECK(dst.rb().is_valid());
  int SX = 1;
  emit(STXSIBX | rs.code() * B21 | dst.ra().code() * B16 |
       dst.rb().code() * B11 | SX);
}

void Assembler::stxsihx(const Simd128Register rs, const MemOperand& dst) {
  CHECK(dst.rb().is_valid());
  int SX = 1;
  emit(STXSIHX | rs.code() * B21 | dst.ra().code() * B16 |
       dst.rb().code() * B11 | SX);
}

void Assembler::stxsiwx(const Simd128Register rs, const MemOperand& dst) {
  CHECK(dst.rb().is_valid());
  int SX = 1;
  emit(STXSIWX | rs.code() * B21 | dst.ra().code() * B16 |
       dst.rb().code() * B11 | SX);
}

void Assembler::stxvd(const Simd128Register rt, const MemOperand& dst) {
  CHECK(dst.rb().is_valid());
  int SX = 1;
  emit(STXVD | rt.code() * B21 | dst.ra().code() * B16 | dst.rb().code() * B11 |
       SX);
}

void Assembler::stxvx(const Simd128Register rt, const MemOperand& dst) {
  CHECK(dst.rb().is_valid());
  int SX = 1;
  emit(STXVX | rt.code() * B21 | dst.ra().code() * B16 | dst.rb().code() * B11 |
       SX);
}

void Assembler::xxspltib(const Simd128Register rt, const Operand& imm) {
  int TX = 1;
  CHECK(is_uint8(imm.immediate()));
  emit(XXSPLTIB | (rt.code() & 0x1F) * B21 | (imm.immediate() & 0xFF) * B11 |
       TX);
}

// Pseudo instructions.
void Assembler::nop(int type) {
  Register reg = r0;
  switch (type) {
    case NON_MARKING_NOP:
      reg = r0;
      break;
    case GROUP_ENDING_NOP:
      reg = r2;
      break;
    case DEBUG_BREAK_NOP:
      reg = r3;
      break;
    default:
      UNIMPLEMENTED();
  }

  ori(reg, reg, Operand::Zero());
}

bool Assembler::IsNop(Instr instr, int type) {
  int reg = 0;
  switch (type) {
    case NON_MARKING_NOP:
      reg = 0;
      break;
    case GROUP_ENDING_NOP:
      reg = 2;
      break;
    case DEBUG_BREAK_NOP:
      reg = 3;
      break;
    default:
      UNIMPLEMENTED();
  }
  return instr == (ORI | reg * B21 | reg * B16);
}

void Assembler::GrowBuffer(int needed) {
  DCHECK_EQ(buffer_start_, buffer_->start());

  // Compute new buffer size.
  int old_size = buffer_->size();
  int new_size = std::min(2 * old_size, old_size + 1 * MB);
  int space = buffer_space() + (new_size - old_size);
  new_size += (space < needed) ? needed - space : 0;

  // Some internal data structures overflow for very large buffers,
  // they must ensure that kMaximalBufferSize is not too large.
  if (new_size > kMaximalBufferSize) {
    V8::FatalProcessOutOfMemory(nullptr, "Assembler::GrowBuffer");
  }

  // Set up new buffer.
  std::unique_ptr<AssemblerBuffer> new_buffer = buffer_->Grow(new_size);
  DCHECK_EQ(new_size, new_buffer->size());
  uint8_t* new_start = new_buffer->start();

  // Copy the data.
  intptr_t pc_delta = new_start - buffer_start_;
  intptr_t rc_delta = (new_start + new_size) - (buffer_start_ + old_size);
  size_t reloc_size = (buffer_start_ + old_size) - reloc_info_writer.pos();
  MemMove(new_start, buffer_start_, pc_offset());
  MemMove(reloc_info_writer.pos() + rc_delta, reloc_info_writer.pos(),
          reloc_size);

  // Switch buffers.
  buffer_ = std::move(new_buffer);
  buffer_start_ = new_start;
  pc_ += pc_delta;
  reloc_info_writer.Reposition(reloc_info_writer.pos() + rc_delta,
                               reloc_info_writer.last_pc() + pc_delta);

  // None of our relocation types are pc relative pointing outside the code
  // buffer nor pc absolute pointing inside the code buffer, so there is no need
  // to relocate any emitted relocation entries.
}

void Assembler::db(uint8_t data) {
  CheckBuffer();
  *reinterpret_cast<uint8_t*>(pc_) = data;
  pc_ += sizeof(uint8_t);
}

void Assembler::dd(uint32_t data) {
  CheckBuffer();
  *reinterpret_cast<uint32_t*>(pc_) = data;
  pc_ += sizeof(uint32_t);
}

void Assembler::dq(uint64_t value) {
  CheckBuffer();
  *reinterpret_cast<uint64_t*>(pc_) = value;
  pc_ += sizeof(uint64_t);
}

void Assembler::dp(uintptr_t data) {
  CheckBuffer();
  *reinterpret_cast<uintptr_t*>(pc_) = data;
  pc_ += sizeof(uintptr_t);
}

void Assembler::RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data) {
  if (!ShouldRecordRelocInfo(rmode)) return;
  DeferredRelocInfo rinfo(pc_offset(), rmode, data);
  relocations_.push_back(rinfo);
}

void Assembler::EmitRelocations() {
  EnsureSpaceFor(relocations_.size() * kMaxRelocSize);

  for (std::vector<DeferredRelocInfo>::iterator it = relocations_.begin();
       it != relocations_.end(); it++) {
    RelocInfo::Mode rmode = it->rmode();
    Address pc = reinterpret_cast<Address>(buffer_start_) + it->position();
    RelocInfo rinfo(pc, rmode, it->data());

    // Fix up internal references now that they are guaranteed to be bound.
    if (RelocInfo::IsInternalReference(rmode)) {
      // Jump table entry
      intptr_t pos = static_cast<intptr_t>(Memory<Address>(pc));
      Memory<Address>(pc) = reinterpret_cast<Address>(buffer_start_) + pos;
    } else if (RelocInfo::IsInternalReferenceEncoded(rmode)) {
      // mov sequence
      intptr_t pos = static_cast<intptr_t>(target_address_at(pc, kNullAddress));
      set_target_address_at(pc, 0,
                            reinterpret_cast<Address>(buffer_start_) + pos,
                            nullptr, SKIP_ICACHE_FLUSH);
    }

    reloc_info_writer.Write(&rinfo);
  }
}

void Assembler::BlockTrampolinePoolFor(int instructions) {
  BlockTrampolinePoolBefore(pc_offset() + instructions * kInstrSize);
}

void Assembler::CheckTrampolinePool() {
  // Some small sequences of instructions must not be broken up by the
  // insertion of a trampoline pool; such sequences are protected by setting
  // either trampoline_pool_blocked_nesting_ or no_trampoline_pool_before_,
  // which are both checked here. Also, recursive calls to CheckTrampolinePool
  // are blocked by trampoline_pool_blocked_nesting_.
  if (trampoline_pool_blocked_nesting_ > 0) return;
  if (pc_offset() < no_trampoline_pool_before_) {
    next_trampoline_check_ = no_trampoline_pool_before_;
    return;
  }

  DCHECK(!trampoline_emitted_);
  if (tracked_branch_count_ > 0) {
    int size = tracked_branch_count_ * kInstrSize;

    // As we are only going to emit trampoline once, we need to prevent any
    // further emission.
    trampoline_emitted_ = true;
    next_trampoline_check_ = kMaxInt;

    // First we emit jump, then we emit trampoline pool.
    b(size + kInstrSize, LeaveLK);
    for (int i = size; i > 0; i -= kInstrSize) {
      b(i, LeaveLK);
    }

    trampoline_ = Trampoline(pc_offset() - size, tracked_branch_count_);
  }
}

PatchingAssembler::PatchingAssembler(const AssemblerOptions& options,
                                     uint8_t* address, int instructions)
    : Assembler(options, ExternalAssemblerBuffer(
                             address, instructions * kInstrSize + kGap)) {
  DCHECK_EQ(reloc_info_writer.pos(), buffer_start_ + buffer_->size());
}

PatchingAssembler::~PatchingAssembler() {
  // Check that the code was patched as expected.
  DCHECK_EQ(pc_, buffer_start_ + buffer_->size() - kGap);
  DCHECK_EQ(reloc_info_writer.pos(), buffer_start_ + buffer_->size());
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_PPC64
