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

#include "src/codegen/s390/assembler-s390.h"
#include <set>
#include <string>

#if V8_TARGET_ARCH_S390X

#if V8_HOST_ARCH_S390X && !V8_OS_ZOS
#include <elf.h>  // Required for auxv checks for STFLE support
#include <sys/auxv.h>
#endif

#include "src/base/bits.h"
#include "src/base/cpu.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/s390/assembler-s390-inl.h"
#include "src/deoptimizer/deoptimizer.h"

namespace v8 {
namespace internal {

// Get the CPU features enabled by the build.
static unsigned CpuFeaturesImpliedByCompiler() {
  unsigned answer = 0;
  return answer;
}

static bool supportsCPUFeature(const char* feature) {
#if V8_OS_ZOS
  // TODO(gabylb): zos - use cpu_init() and cpu_supports() to test support of
  // z/OS features when the current compiler supports them.
  // Currently the only feature to be checked is Vector Extension Facility
  // ("vector128" on z/OS, "vx" on LoZ) - hence the assert in case that changed.
  assert(strcmp(feature, "vx") == 0);
  return __is_vxf_available();
#else
  static std::set<std::string>& features = *new std::set<std::string>();
  static std::set<std::string>& all_available_features =
      *new std::set<std::string>({"iesan3", "zarch", "stfle", "msa", "ldisp",
                                  "eimm", "dfp", "etf3eh", "highgprs", "te",
                                  "vx"});
  if (features.empty()) {
#if V8_HOST_ARCH_S390X

#ifndef HWCAP_S390_VX
#define HWCAP_S390_VX 2048
#endif
#define CHECK_AVAILABILITY_FOR(mask, value) \
  if (f & mask) features.insert(value);

    // initialize feature vector
    uint64_t f = getauxval(AT_HWCAP);
    CHECK_AVAILABILITY_FOR(HWCAP_S390_ESAN3, "iesan3")
    CHECK_AVAILABILITY_FOR(HWCAP_S390_ZARCH, "zarch")
    CHECK_AVAILABILITY_FOR(HWCAP_S390_STFLE, "stfle")
    CHECK_AVAILABILITY_FOR(HWCAP_S390_MSA, "msa")
    CHECK_AVAILABILITY_FOR(HWCAP_S390_LDISP, "ldisp")
    CHECK_AVAILABILITY_FOR(HWCAP_S390_EIMM, "eimm")
    CHECK_AVAILABILITY_FOR(HWCAP_S390_DFP, "dfp")
    CHECK_AVAILABILITY_FOR(HWCAP_S390_ETF3EH, "etf3eh")
    CHECK_AVAILABILITY_FOR(HWCAP_S390_HIGH_GPRS, "highgprs")
    CHECK_AVAILABILITY_FOR(HWCAP_S390_TE, "te")
    CHECK_AVAILABILITY_FOR(HWCAP_S390_VX, "vx")
#else
    // import all features
    features.insert(all_available_features.begin(),
                    all_available_features.end());
#endif
  }
  USE(all_available_features);
  return features.find(feature) != features.end();
#endif  // !V8_OS_ZOS
}

#undef CHECK_AVAILABILITY_FOR
#undef HWCAP_S390_VX

// Check whether Store Facility STFLE instruction is available on the platform.
// Instruction returns a bit vector of the enabled hardware facilities.
static bool supportsSTFLE() {
#if V8_OS_ZOS
  return __is_stfle_available();
#elif V8_HOST_ARCH_S390X
  static bool read_tried = false;
  static uint32_t auxv_hwcap = 0;

  if (!read_tried) {
    // Open the AUXV (auxiliary vector) pseudo-file
    int fd = open("/proc/self/auxv", O_RDONLY);

    read_tried = true;
    if (fd != -1) {
      static Elf64_auxv_t buffer[16];
      Elf64_auxv_t* auxv_element;
      int bytes_read = 0;
      while (bytes_read >= 0) {
        // Read a chunk of the AUXV
        bytes_read = read(fd, buffer, sizeof(buffer));
        // Locate and read the platform field of AUXV if it is in the chunk
        for (auxv_element = buffer;
             auxv_element + sizeof(auxv_element) <= buffer + bytes_read &&
             auxv_element->a_type != AT_NULL;
             auxv_element++) {
          // We are looking for HWCAP entry in AUXV to search for STFLE support
          if (auxv_element->a_type == AT_HWCAP) {
            /* Note: Both auxv_hwcap and buffer are static */
            auxv_hwcap = auxv_element->a_un.a_val;
            goto done_reading;
          }
        }
      }
    done_reading:
      close(fd);
    }
  }

  // Did not find result
  if (0 == auxv_hwcap) {
    return false;
  }

  // HWCAP_S390_STFLE is defined to be 4 in include/asm/elf.h.  Currently
  // hardcoded in case that include file does not exist.
  const uint32_t _HWCAP_S390_STFLE = 4;
  return (auxv_hwcap & _HWCAP_S390_STFLE);
#else
  // STFLE is not available on non-s390 hosts
  return false;
#endif
}

bool CpuFeatures::SupportsWasmSimd128() {
#if V8_ENABLE_WEBASSEMBLY
  return CpuFeatures::IsSupported(VECTOR_ENHANCE_FACILITY_1);
#else
  return false;
#endif  // V8_ENABLE_WEBASSEMBLY
}

void CpuFeatures::ProbeImpl(bool cross_compile) {
  supported_ |= CpuFeaturesImpliedByCompiler();
  icache_line_size_ = 256;

  // Only use statically determined features for cross compile (snapshot).
  if (cross_compile) return;

#ifdef DEBUG
  initialized_ = true;
#endif

  static bool performSTFLE = supportsSTFLE();

// Need to define host, as we are generating inlined S390 assembly to test
// for facilities.
#if V8_HOST_ARCH_S390X
  if (performSTFLE) {
    // STFLE D(B) requires:
    //    GPR0 to specify # of double words to update minus 1.
    //      i.e. GPR0 = 0 for 1 doubleword
    //    D(B) to specify to memory location to store the facilities bits
    // The facilities we are checking for are:
    //   Bit 45 - Distinct Operands for instructions like ARK, SRK, etc.
    // As such, we require only 1 double word
    int64_t facilities[3] = {0L};
#if V8_OS_ZOS
    int64_t reg0 = 2;
    asm volatile(" stfle %0" : "=m"(facilities), __ZL_NR("+", r0)(reg0)::"cc");
#else
    int16_t reg0;
    // LHI sets up GPR0
    // STFLE is specified as .insn, as opcode is not recognized.
    // We register the instructions kill r0 (LHI) and the CC (STFLE).
    asm volatile(
        "lhi   %%r0,2\n"
        ".insn s,0xb2b00000,%0\n"
        : "=Q"(facilities), "=r"(reg0)
        :
        : "cc", "r0");
#endif  // V8_OS_ZOS

    uint64_t one = static_cast<uint64_t>(1);
    // Test for Distinct Operands Facility - Bit 45
    if (facilities[0] & (one << (63 - 45))) {
      supported_ |= (1u << DISTINCT_OPS);
    }
    // Test for General Instruction Extension Facility - Bit 34
    if (facilities[0] & (one << (63 - 34))) {
      supported_ |= (1u << GENERAL_INSTR_EXT);
    }
    // Test for Floating Point Extension Facility - Bit 37
    if (facilities[0] & (one << (63 - 37))) {
      supported_ |= (1u << FLOATING_POINT_EXT);
    }
    // Test for Vector Facility - Bit 129
    if (facilities[2] & (one << (63 - (129 - 128))) &&
        supportsCPUFeature("vx")) {
      supported_ |= (1u << VECTOR_FACILITY);
    }
    // Test for Vector Enhancement Facility 1 - Bit 135
    if (facilities[2] & (one << (63 - (135 - 128))) &&
        supportsCPUFeature("vx")) {
      supported_ |= (1u << VECTOR_ENHANCE_FACILITY_1);
    }
    // Test for Vector Enhancement Facility 2 - Bit 148
    if (facilities[2] & (one << (63 - (148 - 128))) &&
        supportsCPUFeature("vx")) {
      supported_ |= (1u << VECTOR_ENHANCE_FACILITY_2);
    }
    // Test for Miscellaneous Instruction Extension Facility - Bit 58
    if (facilities[0] & (1lu << (63 - 58))) {
      supported_ |= (1u << MISC_INSTR_EXT2);
    }
  }
#else
  // All distinct ops instructions can be simulated
  supported_ |= (1u << DISTINCT_OPS);
  // RISBG can be simulated
  supported_ |= (1u << GENERAL_INSTR_EXT);
  supported_ |= (1u << FLOATING_POINT_EXT);
  supported_ |= (1u << MISC_INSTR_EXT2);
  USE(performSTFLE);  // To avoid assert
  USE(supportsCPUFeature);
  supported_ |= (1u << VECTOR_FACILITY);
  supported_ |= (1u << VECTOR_ENHANCE_FACILITY_1);
  supported_ |= (1u << VECTOR_ENHANCE_FACILITY_2);
#endif
  supported_ |= (1u << FPU);

  // Set a static value on whether Simd is supported.
  // This variable is only used for certain archs to query SupportWasmSimd128()
  // at runtime in builtins using an extern ref. Other callers should use
  // CpuFeatures::SupportWasmSimd128().
  CpuFeatures::supports_wasm_simd_128_ = CpuFeatures::SupportsWasmSimd128();
}

void CpuFeatures::PrintTarget() {
  const char* s390_arch = "s390x";
  PrintF("target %s\n", s390_arch);
}

void CpuFeatures::PrintFeatures() {
  PrintF("FPU=%d\n", CpuFeatures::IsSupported(FPU));
  PrintF("FPU_EXT=%d\n", CpuFeatures::IsSupported(FLOATING_POINT_EXT));
  PrintF("GENERAL_INSTR=%d\n", CpuFeatures::IsSupported(GENERAL_INSTR_EXT));
  PrintF("DISTINCT_OPS=%d\n", CpuFeatures::IsSupported(DISTINCT_OPS));
  PrintF("VECTOR_FACILITY=%d\n", CpuFeatures::IsSupported(VECTOR_FACILITY));
  PrintF("VECTOR_ENHANCE_FACILITY_1=%d\n",
         CpuFeatures::IsSupported(VECTOR_ENHANCE_FACILITY_1));
  PrintF("VECTOR_ENHANCE_FACILITY_2=%d\n",
         CpuFeatures::IsSupported(VECTOR_ENHANCE_FACILITY_2));
  PrintF("MISC_INSTR_EXT2=%d\n", CpuFeatures::IsSupported(MISC_INSTR_EXT2));
}

Register ToRegister(int num) {
  DCHECK(num >= 0 && num < kNumRegisters);
  const Register kRegisters[] = {r0, r1, r2,  r3, r4, r5,  r6,  r7,
                                 r8, r9, r10, fp, ip, r13, r14, sp};
  return kRegisters[num];
}

// -----------------------------------------------------------------------------
// Implementation of RelocInfo

const int RelocInfo::kApplyMask =
    RelocInfo::ModeMask(RelocInfo::CODE_TARGET) |
    RelocInfo::ModeMask(RelocInfo::INTERNAL_REFERENCE);

bool RelocInfo::IsCodedSpecially() {
  // The deserializer needs to know whether a pointer is specially
  // coded.  Being specially coded on S390 means that it is an iihf/iilf
  // instruction sequence, and that is always the case inside code
  // objects.
  return true;
}

bool RelocInfo::IsInConstantPool() { return false; }

uint32_t RelocInfo::wasm_call_tag() const {
  DCHECK(rmode_ == WASM_CALL || rmode_ == WASM_STUB_CALL);
  return static_cast<uint32_t>(
      Assembler::target_address_at(pc_, constant_pool_));
}

// -----------------------------------------------------------------------------
// Implementation of Operand and MemOperand
// See assembler-s390-inl.h for inlined constructors

Operand::Operand(Handle<HeapObject> handle) {
  AllowHandleDereference using_location;
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

MemOperand::MemOperand(Register rn, int32_t offset)
    : baseRegister(rn), indexRegister(r0), offset_(offset) {}

MemOperand::MemOperand(Register rx, Register rb, int32_t offset)
    : baseRegister(rb), indexRegister(rx), offset_(offset) {}

void Assembler::AllocateAndInstallRequestedHeapNumbers(LocalIsolate* isolate) {
  DCHECK_IMPLIES(isolate == nullptr, heap_number_requests_.empty());
  for (auto& request : heap_number_requests_) {
    Address pc = reinterpret_cast<Address>(buffer_start_) + request.offset();
    Handle<HeapObject> object =
        isolate->factory()->NewHeapNumber<AllocationType::kOld>(
            request.heap_number());
    set_target_address_at(pc, kNullAddress, object.address(), nullptr,
                          SKIP_ICACHE_FLUSH);
  }
}

// -----------------------------------------------------------------------------
// Specific instructions, constants, and masks.

Assembler::Assembler(const AssemblerOptions& options,
                     std::unique_ptr<AssemblerBuffer> buffer)
    : AssemblerBase(options, std::move(buffer)),
      scratch_register_list_(DefaultTmpList()),
      scratch_double_register_list_(DefaultFPTmpList()) {
  reloc_info_writer.Reposition(buffer_start_ + buffer_->size(), pc_);
  last_bound_pos_ = 0;
  relocations_.reserve(128);
}

void Assembler::GetCode(Isolate* isolate, CodeDesc* desc) {
  GetCode(isolate->main_thread_local_isolate(), desc);
}
void Assembler::GetCode(LocalIsolate* isolate, CodeDesc* desc,
                        SafepointTableBuilderBase* safepoint_table_builder,
                        int handler_table_offset) {
  // As a crutch to avoid having to add manual Align calls wherever we use a
  // raw workflow to create Code objects (mostly in tests), add another Align
  // call here. It does no harm - the end of the Code object is aligned to the
  // (larger) kCodeAlignment anyways.
  // TODO(jgruber): Consider moving responsibility for proper alignment to
  // metadata table builders (safepoint, handler, constant pool, code
  // comments).
  DataAlign(InstructionStream::kMetadataAlignment);

  EmitRelocations();

  int code_comments_size = WriteCodeComments();

  AllocateAndInstallRequestedHeapNumbers(isolate);

  // Set up code descriptor.
  // TODO(jgruber): Reconsider how these offsets and sizes are maintained up to
  // this point to make CodeDesc initialization less fiddly.

  static constexpr int kConstantPoolSize = 0;
  static constexpr int kBuiltinJumpTableInfoSize = 0;
  const int instruction_size = pc_offset();
  const int builtin_jump_table_info_offset =
      instruction_size - kBuiltinJumpTableInfoSize;
  const int code_comments_offset =
      builtin_jump_table_info_offset - code_comments_size;
  const int constant_pool_offset = code_comments_offset - kConstantPoolSize;
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
  while ((pc_offset() & (m - 1)) != 0) {
    nop(0);
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

// This code assumes a FIXED_SEQUENCE for 64bit loads (iihf/iilf)
bool Assembler::Is64BitLoadIntoIP(SixByteInstr instr1, SixByteInstr instr2) {
  // Check the instructions are the iihf/iilf load into ip
  return (((instr1 >> 32) == 0xC0C8) && ((instr2 >> 32) == 0xC0C9));
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

// Returns the target address of the relative instructions, typically
// of the form: pos + imm (where immediate is in # of halfwords for
// BR* and LARL).
int Assembler::target_at(int pos) {
  SixByteInstr instr = instr_at(pos);
  // check which type of branch this is 16 or 26 bit offset
  Opcode opcode = Instruction::S390OpcodeValue(buffer_start_ + pos);

  if (BRC == opcode || BRCT == opcode || BRCTG == opcode || BRXH == opcode) {
    int16_t imm16 = SIGN_EXT_IMM16((instr & kImm16Mask));
    imm16 <<= 1;  // immediate is in # of halfwords
    if (imm16 == 0) return kEndOfChain;
    return pos + imm16;
  } else if (LLILF == opcode || BRCL == opcode || LARL == opcode ||
             BRASL == opcode || LGRL == opcode) {
    int32_t imm32 =
        static_cast<int32_t>(instr & (static_cast<uint64_t>(0xFFFFFFFF)));
    if (LLILF != opcode)
      imm32 <<= 1;  // BR* + LARL treat immediate in # of halfwords
    if (imm32 == 0) return kEndOfChain;
    return pos + imm32;
  } else if (BRXHG == opcode) {
    // offset is in bits 16-31 of 48 bit instruction
    instr = instr >> 16;
    int16_t imm16 = SIGN_EXT_IMM16((instr & kImm16Mask));
    imm16 <<= 1;  // immediate is in # of halfwords
    if (imm16 == 0) return kEndOfChain;
    return pos + imm16;
  }

  // Unknown condition
  DCHECK(false);
  return -1;
}

// Update the target address of the current relative instruction.
void Assembler::target_at_put(int pos, int target_pos, bool* is_branch) {
  SixByteInstr instr = instr_at(pos);
  Opcode opcode = Instruction::S390OpcodeValue(buffer_start_ + pos);

  if (is_branch != nullptr) {
    *is_branch =
        (opcode == BRC || opcode == BRCT || opcode == BRCTG || opcode == BRCL ||
         opcode == BRASL || opcode == BRXH || opcode == BRXHG);
  }

  if (BRC == opcode || BRCT == opcode || BRCTG == opcode || BRXH == opcode) {
    int16_t imm16 = target_pos - pos;
    instr &= (~0xFFFF);
    DCHECK(is_int16(imm16));
    instr_at_put<FourByteInstr>(pos, instr | (imm16 >> 1));
    return;
  } else if (BRCL == opcode || LARL == opcode || BRASL == opcode ||
             LGRL == opcode) {
    // Immediate is in # of halfwords
    int32_t imm32 = target_pos - pos;
    instr &= (~static_cast<uint64_t>(0xFFFFFFFF));
    instr_at_put<SixByteInstr>(pos, instr | (imm32 >> 1));
    return;
  } else if (LLILF == opcode) {
    DCHECK(target_pos == kEndOfChain || target_pos >= 0);
    // Emitted label constant, not part of a branch.
    // Make label relative to InstructionStream pointer of generated
    // InstructionStream object.
    int32_t imm32 =
        target_pos + (InstructionStream::kHeaderSize - kHeapObjectTag);
    instr &= (~static_cast<uint64_t>(0xFFFFFFFF));
    instr_at_put<SixByteInstr>(pos, instr | imm32);
    return;
  } else if (BRXHG == opcode) {
    // Immediate is in bits 16-31 of 48 bit instruction
    int32_t imm16 = target_pos - pos;
    instr &= (0xFFFF0000FFFF);  // clear bits 16-31
    imm16 &= 0xFFFF;            // clear high halfword
    imm16 <<= 16;
    // Immediate is in # of halfwords
    instr_at_put<SixByteInstr>(pos, instr | (imm16 >> 1));
    return;
  }
  DCHECK(false);
}

// Returns the maximum number of bits given instruction can address.
int Assembler::max_reach_from(int pos) {
  Opcode opcode = Instruction::S390OpcodeValue(buffer_start_ + pos);
  // Check which type of instr.  In theory, we can return
  // the values below + 1, given offset is # of halfwords
  if (BRC == opcode || BRCT == opcode || BRCTG == opcode || BRXH == opcode ||
      BRXHG == opcode) {
    return 16;
  } else if (LLILF == opcode || BRCL == opcode || LARL == opcode ||
             BRASL == opcode || LGRL == opcode) {
    return 31;  // Using 31 as workaround instead of 32 as
                // is_intn(x,32) doesn't work on 32-bit platforms.
                // llilf: Emitted label constant, not part of
                //        a branch (regexp PushBacktrack).
  }
  DCHECK(false);
  return 16;
}

void Assembler::bind_to(Label* L, int pos) {
  DCHECK(0 <= pos && pos <= pc_offset());  // must have a valid binding position
  bool is_branch = false;
  while (L->is_linked()) {
    int fixup_pos = L->pos();
#ifdef DEBUG
    int32_t offset = pos - fixup_pos;
    int maxReach = max_reach_from(fixup_pos);
#endif
    next(L);  // call next before overwriting link with target at fixup_pos
    DCHECK(is_intn(offset, maxReach));
    target_at_put(fixup_pos, pos, &is_branch);
  }
  L->bind_to(pos);

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

void Assembler::load_label_offset(Register r1, Label* L) {
  int target_pos;
  int constant;
  if (L->is_bound()) {
    target_pos = L->pos();
    constant = target_pos + (InstructionStream::kHeaderSize - kHeapObjectTag);
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link
    } else {
      // was: target_pos = kEndOfChain;
      // However, using branch to self to mark the first reference
      // should avoid most instances of branch offset overflow.  See
      // target_at() for where this is converted back to kEndOfChain.
      target_pos = pc_offset();
    }
    L->link_to(pc_offset());

    constant = target_pos - pc_offset();
  }
  llilf(r1, Operand(constant));
}

// Pseudo op - branch on condition
void Assembler::branchOnCond(Condition c, int branch_offset, bool is_bound,
                             bool force_long_branch) {
  int offset_in_halfwords = branch_offset / 2;
  if (is_bound && is_int16(offset_in_halfwords) && !force_long_branch) {
    brc(c, Operand(offset_in_halfwords));  // short jump
  } else {
    brcl(c, Operand(offset_in_halfwords));  // long jump
  }
}

// Exception-generating instructions and debugging support.
// Stops with a non-negative code less than kNumOfWatchedStops support
// enabling/disabling and a counter feature. See simulator-s390.h .
void Assembler::stop(Condition cond, int32_t code, CRegister cr) {
  if (cond != al) {
    Label skip;
    b(NegateCondition(cond), &skip, Label::kNear);
    bkpt(0);
    bind(&skip);
  } else {
    bkpt(0);
  }
}

void Assembler::bkpt(uint32_t imm16) {
  // GDB software breakpoint instruction
  emit2bytes(0x0001);
}

// Pseudo instructions.
void Assembler::nop(int type) {
  switch (type) {
    case 0:
      lr(r0, r0);
      break;
    case DEBUG_BREAK_NOP:
      // TODO(john.yan): Use a better NOP break
      oill(r3, Operand::Zero());
      break;
#if V8_OS_ZOS
    case BASR_CALL_TYPE_NOP:
      emit2bytes(0x0000);
      break;
    case BRAS_CALL_TYPE_NOP:
      emit2bytes(0x0001);
      break;
    case BRASL_CALL_TYPE_NOP:
      emit2bytes(0x0011);
      break;
#endif
    default:
      UNIMPLEMENTED();
  }
}

// -------------------------
// Load Address Instructions
// -------------------------
// Load Address Relative Long
void Assembler::larl(Register r1, Label* l) {
  larl(r1, Operand(branch_offset(l)));
}

void Assembler::lgrl(Register r1, Label* l) {
  lgrl(r1, Operand(branch_offset(l)));
}

void Assembler::EnsureSpaceFor(int space_needed) {
  if (buffer_space() <= (kGap + space_needed)) {
    GrowBuffer(space_needed);
  }
}

void Assembler::call(Handle<Code> target, RelocInfo::Mode rmode) {
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  EnsureSpace ensure_space(this);

  RecordRelocInfo(rmode);
  int32_t target_index = AddCodeTarget(target);
  brasl(r14, Operand(target_index));
}

void Assembler::jump(Handle<Code> target, RelocInfo::Mode rmode,
                     Condition cond) {
  DCHECK(RelocInfo::IsRelativeCodeTarget(rmode));
  EnsureSpace ensure_space(this);

  RecordRelocInfo(rmode);
  int32_t target_index = AddCodeTarget(target);
  brcl(cond, Operand(target_index));
}

// end of S390instructions

bool Assembler::IsNop(SixByteInstr instr, int type) {
  DCHECK((0 == type) || (DEBUG_BREAK_NOP == type));
  if (DEBUG_BREAK_NOP == type) {
    return ((instr & 0xFFFFFFFF) == 0xA53B0000);  // oill r3, 0
  }
  return ((instr & 0xFFFF) == 0x1800);  // lr r0,r0
}

// dummy instruction reserved for special use.
void Assembler::dumy(int r1, int x2, int b2, int d2) {
#if defined(USE_SIMULATOR)
  int op = 0xE353;
  uint64_t code = (static_cast<uint64_t>(op & 0xFF00)) * B32 |
                  (static_cast<uint64_t>(r1) & 0xF) * B36 |
                  (static_cast<uint64_t>(x2) & 0xF) * B32 |
                  (static_cast<uint64_t>(b2) & 0xF) * B28 |
                  (static_cast<uint64_t>(d2 & 0x0FFF)) * B16 |
                  (static_cast<uint64_t>(d2 & 0x0FF000)) >> 4 |
                  (static_cast<uint64_t>(op & 0x00FF));
  emit6bytes(code);
#endif
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

void Assembler::dh(uint16_t data) {
  CheckBuffer();
  *reinterpret_cast<uint16_t*>(pc_) = data;
  pc_ += sizeof(uint16_t);
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

void Assembler::emit_label_addr(Label* label) {
  CheckBuffer();
  RecordRelocInfo(RelocInfo::INTERNAL_REFERENCE);
  int position = link(label);
  DCHECK(label->is_bound());
  // Keep internal references relative until EmitRelocations.
  dp(position);
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
      Address pos = Memory<Address>(pc);
      Memory<Address>(pc) = reinterpret_cast<Address>(buffer_start_) + pos;
    } else if (RelocInfo::IsInternalReferenceEncoded(rmode)) {
      // mov sequence
      Address pos = target_address_at(pc, 0);
      set_target_address_at(pc, 0,
                            reinterpret_cast<Address>(buffer_start_) + pos,
                            nullptr, SKIP_ICACHE_FLUSH);
    }

    reloc_info_writer.Write(&rinfo);
  }
}

RegList Assembler::DefaultTmpList() { return {r1, ip}; }
DoubleRegList Assembler::DefaultFPTmpList() {
  return {kScratchDoubleReg, kDoubleRegZero};
}

}  // namespace internal
}  // namespace v8
#endif  // V8_TARGET_ARCH_S390X
