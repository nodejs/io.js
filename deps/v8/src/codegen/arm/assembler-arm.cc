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
// Copyright 2012 the V8 project authors. All rights reserved.

#include "src/codegen/arm/assembler-arm.h"

#if V8_TARGET_ARCH_ARM

#include "src/base/bits.h"
#include "src/base/cpu.h"
#include "src/base/overflowing-math.h"
#include "src/codegen/arm/assembler-arm-inl.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/macro-assembler.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

static const unsigned kArmv6 = 0u;
static const unsigned kArmv7 = kArmv6 | (1u << ARMv7);
static const unsigned kArmv7WithSudiv = kArmv7 | (1u << ARMv7_SUDIV);
static const unsigned kArmv8 = kArmv7WithSudiv | (1u << ARMv8);

static unsigned CpuFeaturesFromCommandLine() {
  unsigned result;
  const char* arm_arch = v8_flags.arm_arch;
  if (strcmp(arm_arch, "armv8") == 0) {
    result = kArmv8;
  } else if (strcmp(arm_arch, "armv7+sudiv") == 0) {
    result = kArmv7WithSudiv;
  } else if (strcmp(arm_arch, "armv7") == 0) {
    result = kArmv7;
  } else if (strcmp(arm_arch, "armv6") == 0) {
    result = kArmv6;
  } else {
    fprintf(stderr, "Error: unrecognised value for --arm-arch ('%s').\n",
            arm_arch);
    fprintf(stderr,
            "Supported values are:  armv8\n"
            "                       armv7+sudiv\n"
            "                       armv7\n"
            "                       armv6\n");
    FATAL("arm-arch");
  }

  // If any of the old (deprecated) flags are specified, print a warning, but
  // otherwise try to respect them for now.
  // TODO(jbramley): When all the old bots have been updated, remove this.
  base::Optional<bool> maybe_enable_armv7 = v8_flags.enable_armv7;
  base::Optional<bool> maybe_enable_vfp3 = v8_flags.enable_vfp3;
  base::Optional<bool> maybe_enable_32dregs = v8_flags.enable_32dregs;
  base::Optional<bool> maybe_enable_neon = v8_flags.enable_neon;
  base::Optional<bool> maybe_enable_sudiv = v8_flags.enable_sudiv;
  base::Optional<bool> maybe_enable_armv8 = v8_flags.enable_armv8;
  if (maybe_enable_armv7.has_value() || maybe_enable_vfp3.has_value() ||
      maybe_enable_32dregs.has_value() || maybe_enable_neon.has_value() ||
      maybe_enable_sudiv.has_value() || maybe_enable_armv8.has_value()) {
    // As an approximation of the old behaviour, set the default values from the
    // arm_arch setting, then apply the flags over the top.
    bool enable_armv7 = (result & (1u << ARMv7)) != 0;
    bool enable_vfp3 = (result & (1u << ARMv7)) != 0;
    bool enable_32dregs = (result & (1u << ARMv7)) != 0;
    bool enable_neon = (result & (1u << ARMv7)) != 0;
    bool enable_sudiv = (result & (1u << ARMv7_SUDIV)) != 0;
    bool enable_armv8 = (result & (1u << ARMv8)) != 0;
    if (maybe_enable_armv7.has_value()) {
      fprintf(stderr,
              "Warning: --enable_armv7 is deprecated. "
              "Use --arm_arch instead.\n");
      enable_armv7 = maybe_enable_armv7.value();
    }
    if (maybe_enable_vfp3.has_value()) {
      fprintf(stderr,
              "Warning: --enable_vfp3 is deprecated. "
              "Use --arm_arch instead.\n");
      enable_vfp3 = maybe_enable_vfp3.value();
    }
    if (maybe_enable_32dregs.has_value()) {
      fprintf(stderr,
              "Warning: --enable_32dregs is deprecated. "
              "Use --arm_arch instead.\n");
      enable_32dregs = maybe_enable_32dregs.value();
    }
    if (maybe_enable_neon.has_value()) {
      fprintf(stderr,
              "Warning: --enable_neon is deprecated. "
              "Use --arm_arch instead.\n");
      enable_neon = maybe_enable_neon.value();
    }
    if (maybe_enable_sudiv.has_value()) {
      fprintf(stderr,
              "Warning: --enable_sudiv is deprecated. "
              "Use --arm_arch instead.\n");
      enable_sudiv = maybe_enable_sudiv.value();
    }
    if (maybe_enable_armv8.has_value()) {
      fprintf(stderr,
              "Warning: --enable_armv8 is deprecated. "
              "Use --arm_arch instead.\n");
      enable_armv8 = maybe_enable_armv8.value();
    }
    // Emulate the old implications.
    if (enable_armv8) {
      enable_vfp3 = true;
      enable_neon = true;
      enable_32dregs = true;
      enable_sudiv = true;
    }
    // Select the best available configuration.
    if (enable_armv7 && enable_vfp3 && enable_32dregs && enable_neon) {
      if (enable_sudiv) {
        if (enable_armv8) {
          result = kArmv8;
        } else {
          result = kArmv7WithSudiv;
        }
      } else {
        result = kArmv7;
      }
    } else {
      result = kArmv6;
    }
  }
  return result;
}

// Get the CPU features enabled by the build.
// For cross compilation the preprocessor symbols such as
// CAN_USE_ARMV7_INSTRUCTIONS and CAN_USE_VFP3_INSTRUCTIONS can be used to
// enable ARMv7 and VFPv3 instructions when building the snapshot. However,
// these flags should be consistent with a supported ARM configuration:
//  "armv6":       ARMv6 + VFPv2
//  "armv7":       ARMv7 + VFPv3-D32 + NEON
//  "armv7+sudiv": ARMv7 + VFPv4-D32 + NEON + SUDIV
//  "armv8":       ARMv8 (+ all of the above)
static constexpr unsigned CpuFeaturesFromCompiler() {
// TODO(jbramley): Once the build flags are simplified, these tests should
// also be simplified.

// Check *architectural* implications.
#if defined(CAN_USE_ARMV8_INSTRUCTIONS) && !defined(CAN_USE_ARMV7_INSTRUCTIONS)
#error "CAN_USE_ARMV8_INSTRUCTIONS should imply CAN_USE_ARMV7_INSTRUCTIONS"
#endif
#if defined(CAN_USE_ARMV8_INSTRUCTIONS) && !defined(CAN_USE_SUDIV)
#error "CAN_USE_ARMV8_INSTRUCTIONS should imply CAN_USE_SUDIV"
#endif
#if defined(CAN_USE_ARMV7_INSTRUCTIONS) != defined(CAN_USE_VFP3_INSTRUCTIONS)
// V8 requires VFP, and all ARMv7 devices with VFP have VFPv3. Similarly,
// VFPv3 isn't available before ARMv7.
#error "CAN_USE_ARMV7_INSTRUCTIONS should match CAN_USE_VFP3_INSTRUCTIONS"
#endif
#if defined(CAN_USE_NEON) && !defined(CAN_USE_ARMV7_INSTRUCTIONS)
#error "CAN_USE_NEON should imply CAN_USE_ARMV7_INSTRUCTIONS"
#endif

// Find compiler-implied features.
#if defined(CAN_USE_ARMV8_INSTRUCTIONS) &&                           \
    defined(CAN_USE_ARMV7_INSTRUCTIONS) && defined(CAN_USE_SUDIV) && \
    defined(CAN_USE_NEON) && defined(CAN_USE_VFP3_INSTRUCTIONS)
  return kArmv8;
#elif defined(CAN_USE_ARMV7_INSTRUCTIONS) && defined(CAN_USE_SUDIV) && \
    defined(CAN_USE_NEON) && defined(CAN_USE_VFP3_INSTRUCTIONS)
  return kArmv7WithSudiv;
#elif defined(CAN_USE_ARMV7_INSTRUCTIONS) && defined(CAN_USE_NEON) && \
    defined(CAN_USE_VFP3_INSTRUCTIONS)
  return kArmv7;
#else
  return kArmv6;
#endif
}

bool CpuFeatures::SupportsWasmSimd128() { return IsSupported(NEON); }

void CpuFeatures::ProbeImpl(bool cross_compile) {
  dcache_line_size_ = 64;

  unsigned command_line = CpuFeaturesFromCommandLine();
  // Only use statically determined features for cross compile (snapshot).
  if (cross_compile) {
    supported_ |= command_line & CpuFeaturesFromCompiler();
    return;
  }

#ifndef __arm__
  // For the simulator build, use whatever the flags specify.
  supported_ |= command_line;

#else  // __arm__
  // Probe for additional features at runtime.
  base::CPU cpu;
  // Runtime detection is slightly fuzzy, and some inferences are necessary.
  unsigned runtime = kArmv6;
  // NEON and VFPv3 imply at least ARMv7-A.
  if (cpu.has_neon() && cpu.has_vfp3_d32()) {
    DCHECK(cpu.has_vfp3());
    runtime |= kArmv7;
    if (cpu.has_idiva()) {
      runtime |= kArmv7WithSudiv;
      if (cpu.architecture() >= 8) {
        runtime |= kArmv8;
      }
    }
  }

  // Use the best of the features found by CPU detection and those inferred from
  // the build system. In both cases, restrict available features using the
  // command-line. Note that the command-line flags are very permissive (kArmv8)
  // by default.
  supported_ |= command_line & CpuFeaturesFromCompiler();
  supported_ |= command_line & runtime;

  // Additional tuning options.

  // ARM Cortex-A9 and Cortex-A5 have 32 byte cachelines.
  if (cpu.implementer() == base::CPU::kArm &&
      (cpu.part() == base::CPU::kArmCortexA5 ||
       cpu.part() == base::CPU::kArmCortexA9)) {
    dcache_line_size_ = 32;
  }
#endif

  DCHECK_IMPLIES(IsSupported(ARMv7_SUDIV), IsSupported(ARMv7));
  DCHECK_IMPLIES(IsSupported(ARMv8), IsSupported(ARMv7_SUDIV));

  // Set a static value on whether Simd is supported.
  // This variable is only used for certain archs to query SupportWasmSimd128()
  // at runtime in builtins using an extern ref. Other callers should use
  // CpuFeatures::SupportWasmSimd128().
  CpuFeatures::supports_wasm_simd_128_ = CpuFeatures::SupportsWasmSimd128();
}

void CpuFeatures::PrintTarget() {
  const char* arm_arch = nullptr;
  const char* arm_target_type = "";
  const char* arm_no_probe = "";
  const char* arm_fpu = "";
  const char* arm_thumb = "";
  const char* arm_float_abi = nullptr;

#if !defined __arm__
  arm_target_type = " simulator";
#endif

#if defined ARM_TEST_NO_FEATURE_PROBE
  arm_no_probe = " noprobe";
#endif

#if defined CAN_USE_ARMV8_INSTRUCTIONS
  arm_arch = "arm v8";
#elif defined CAN_USE_ARMV7_INSTRUCTIONS
  arm_arch = "arm v7";
#else
  arm_arch = "arm v6";
#endif

#if defined CAN_USE_NEON
  arm_fpu = " neon";
#elif defined CAN_USE_VFP3_INSTRUCTIONS
#if defined CAN_USE_VFP32DREGS
  arm_fpu = " vfp3";
#else
  arm_fpu = " vfp3-d16";
#endif
#else
  arm_fpu = " vfp2";
#endif

#ifdef __arm__
  arm_float_abi = base::OS::ArmUsingHardFloat() ? "hard" : "softfp";
#elif USE_EABI_HARDFLOAT
  arm_float_abi = "hard";
#else
  arm_float_abi = "softfp";
#endif

#if defined __arm__ && (defined __thumb__) || (defined __thumb2__)
  arm_thumb = " thumb";
#endif

  printf("target%s%s %s%s%s %s\n", arm_target_type, arm_no_probe, arm_arch,
         arm_fpu, arm_thumb, arm_float_abi);
}

void CpuFeatures::PrintFeatures() {
  printf("ARMv8=%d ARMv7=%d VFPv3=%d VFP32DREGS=%d NEON=%d SUDIV=%d",
         CpuFeatures::IsSupported(ARMv8), CpuFeatures::IsSupported(ARMv7),
         CpuFeatures::IsSupported(VFPv3), CpuFeatures::IsSupported(VFP32DREGS),
         CpuFeatures::IsSupported(NEON), CpuFeatures::IsSupported(SUDIV));
#ifdef __arm__
  bool eabi_hardfloat = base::OS::ArmUsingHardFloat();
#elif USE_EABI_HARDFLOAT
  bool eabi_hardfloat = true;
#else
  bool eabi_hardfloat = false;
#endif
  printf(" USE_EABI_HARDFLOAT=%d\n", eabi_hardfloat);
}

// -----------------------------------------------------------------------------
// Implementation of RelocInfo

// static
const int RelocInfo::kApplyMask =
    RelocInfo::ModeMask(RelocInfo::RELATIVE_CODE_TARGET);

bool RelocInfo::IsCodedSpecially() {
  // The deserializer needs to know whether a pointer is specially coded.  Being
  // specially coded on ARM means that it is a movw/movt instruction. We don't
  // generate those for relocatable pointers.
  return false;
}

bool RelocInfo::IsInConstantPool() {
  return Assembler::is_constant_pool_load(pc_);
}

uint32_t RelocInfo::wasm_call_tag() const {
  DCHECK(rmode_ == WASM_CALL || rmode_ == WASM_STUB_CALL);
  return static_cast<uint32_t>(
      Assembler::target_address_at(pc_, constant_pool_));
}

// -----------------------------------------------------------------------------
// Implementation of Operand and MemOperand
// See assembler-arm-inl.h for inlined constructors

Operand::Operand(Handle<HeapObject> handle) {
  rm_ = no_reg;
  value_.immediate = static_cast<intptr_t>(handle.address());
  rmode_ = RelocInfo::FULL_EMBEDDED_OBJECT;
}

Operand::Operand(Register rm, ShiftOp shift_op, int shift_imm) {
  DCHECK(is_uint5(shift_imm));

  rm_ = rm;
  rs_ = no_reg;
  shift_op_ = shift_op;
  shift_imm_ = shift_imm & 31;

  if ((shift_op == ROR) && (shift_imm == 0)) {
    // ROR #0 is functionally equivalent to LSL #0 and this allow us to encode
    // RRX as ROR #0 (See below).
    shift_op = LSL;
  } else if (shift_op == RRX) {
    // encoded as ROR with shift_imm == 0
    DCHECK_EQ(shift_imm, 0);
    shift_op_ = ROR;
    shift_imm_ = 0;
  }
}

Operand::Operand(Register rm, ShiftOp shift_op, Register rs) {
  DCHECK(shift_op != RRX);
  rm_ = rm;
  rs_ = no_reg;
  shift_op_ = shift_op;
  rs_ = rs;
}

Operand Operand::EmbeddedNumber(double value) {
  int32_t smi;
  if (DoubleToSmiInteger(value, &smi)) return Operand(Smi::FromInt(smi));
  Operand result(0, RelocInfo::FULL_EMBEDDED_OBJECT);
  result.is_heap_number_request_ = true;
  result.value_.heap_number_request = HeapNumberRequest(value);
  return result;
}

MemOperand::MemOperand(Register rn, int32_t offset, AddrMode am)
    : rn_(rn), rm_(no_reg), offset_(offset), am_(am) {
  // Accesses below the stack pointer are not safe, and are prohibited by the
  // ABI. We can check obvious violations here.
  if (rn == sp) {
    if (am == Offset) DCHECK_LE(0, offset);
    if (am == NegOffset) DCHECK_GE(0, offset);
  }
}

MemOperand::MemOperand(Register rn, Register rm, AddrMode am)
    : rn_(rn), rm_(rm), shift_op_(LSL), shift_imm_(0), am_(am) {}

MemOperand::MemOperand(Register rn, Register rm, ShiftOp shift_op,
                       int shift_imm, AddrMode am)
    : rn_(rn),
      rm_(rm),
      shift_op_(shift_op),
      shift_imm_(shift_imm & 31),
      am_(am) {
  DCHECK(is_uint5(shift_imm));
}

NeonMemOperand::NeonMemOperand(Register rn, AddrMode am, int align)
    : rn_(rn), rm_(am == Offset ? pc : sp) {
  DCHECK((am == Offset) || (am == PostIndex));
  SetAlignment(align);
}

NeonMemOperand::NeonMemOperand(Register rn, Register rm, int align)
    : rn_(rn), rm_(rm) {
  SetAlignment(align);
}

void NeonMemOperand::SetAlignment(int align) {
  switch (align) {
    case 0:
      align_ = 0;
      break;
    case 64:
      align_ = 1;
      break;
    case 128:
      align_ = 2;
      break;
    case 256:
      align_ = 3;
      break;
    default:
      UNREACHABLE();
  }
}

void Assembler::AllocateAndInstallRequestedHeapNumbers(LocalIsolate* isolate) {
  DCHECK_IMPLIES(isolate == nullptr, heap_number_requests_.empty());
  for (auto& request : heap_number_requests_) {
    Handle<HeapObject> object =
        isolate->factory()->NewHeapNumber<AllocationType::kOld>(
            request.heap_number());
    Address pc = reinterpret_cast<Address>(buffer_start_) + request.offset();
    Memory<Address>(constant_pool_entry_address(pc, 0 /* unused */)) =
        object.address();
  }
}

// -----------------------------------------------------------------------------
// Specific instructions, constants, and masks.

// str(r, MemOperand(sp, 4, NegPreIndex), al) instruction (aka push(r))
// register r is not encoded.
const Instr kPushRegPattern = al | B26 | 4 | NegPreIndex | sp.code() * B16;
// ldr(r, MemOperand(sp, 4, PostIndex), al) instruction (aka pop(r))
// register r is not encoded.
const Instr kPopRegPattern = al | B26 | L | 4 | PostIndex | sp.code() * B16;
// ldr rd, [pc, #offset]
const Instr kLdrPCImmedMask = 15 * B24 | 7 * B20 | 15 * B16;
const Instr kLdrPCImmedPattern = 5 * B24 | L | pc.code() * B16;
// Pc-relative call or jump to a signed imm24 offset.
// bl pc + #offset
// b  pc + #offset
const Instr kBOrBlPCImmedMask = 0xE * B24;
const Instr kBOrBlPCImmedPattern = 0xA * B24;
// vldr dd, [pc, #offset]
const Instr kVldrDPCMask = 15 * B24 | 3 * B20 | 15 * B16 | 15 * B8;
const Instr kVldrDPCPattern = 13 * B24 | L | pc.code() * B16 | 11 * B8;
// blxcc rm
const Instr kBlxRegMask =
    15 * B24 | 15 * B20 | 15 * B16 | 15 * B12 | 15 * B8 | 15 * B4;
const Instr kBlxRegPattern = B24 | B21 | 15 * B16 | 15 * B12 | 15 * B8 | BLX;
const Instr kBlxIp = al | kBlxRegPattern | ip.code();
const Instr kMovMvnMask = 0x6D * B21 | 0xF * B16;
const Instr kMovMvnPattern = 0xD * B21;
const Instr kMovMvnFlip = B22;
const Instr kMovLeaveCCMask = 0xDFF * B16;
const Instr kMovLeaveCCPattern = 0x1A0 * B16;
const Instr kMovwPattern = 0x30 * B20;
const Instr kMovtPattern = 0x34 * B20;
const Instr kMovwLeaveCCFlip = 0x5 * B21;
const Instr kMovImmedMask = 0x7F * B21;
const Instr kMovImmedPattern = 0x1D * B21;
const Instr kOrrImmedMask = 0x7F * B21;
const Instr kOrrImmedPattern = 0x1C * B21;
const Instr kCmpCmnMask = 0xDD * B20 | 0xF * B12;
const Instr kCmpCmnPattern = 0x15 * B20;
const Instr kCmpCmnFlip = B21;
const Instr kAddSubFlip = 0x6 * B21;
const Instr kAndBicFlip = 0xE * B21;

// A mask for the Rd register for push, pop, ldr, str instructions.
const Instr kLdrRegFpOffsetPattern = al | B26 | L | Offset | fp.code() * B16;
const Instr kStrRegFpOffsetPattern = al | B26 | Offset | fp.code() * B16;
const Instr kLdrRegFpNegOffsetPattern =
    al | B26 | L | NegOffset | fp.code() * B16;
const Instr kStrRegFpNegOffsetPattern = al | B26 | NegOffset | fp.code() * B16;
const Instr kLdrStrInstrTypeMask = 0xFFFF0000;

Assembler::Assembler(const AssemblerOptions& options,
                     std::unique_ptr<AssemblerBuffer> buffer)
    : AssemblerBase(options, std::move(buffer)),
      pending_32_bit_constants_(),
      scratch_register_list_(DefaultTmpList()),
      scratch_vfp_register_list_(DefaultFPTmpList()) {
  reloc_info_writer.Reposition(buffer_start_ + buffer_->size(), pc_);
  constant_pool_deadline_ = kMaxInt;
  const_pool_blocked_nesting_ = 0;
  no_const_pool_before_ = 0;
  first_const_pool_32_use_ = -1;
  last_bound_pos_ = 0;
  if (CpuFeatures::IsSupported(VFP32DREGS)) {
    // Register objects tend to be abstracted and survive between scopes, so
    // it's awkward to use CpuFeatures::VFP32DREGS with CpuFeatureScope. To make
    // its use consistent with other features, we always enable it if we can.
    EnableCpuFeature(VFP32DREGS);
  }
}

Assembler::~Assembler() {
  DCHECK_EQ(const_pool_blocked_nesting_, 0);
  DCHECK_EQ(first_const_pool_32_use_, -1);
}

// static
RegList Assembler::DefaultTmpList() { return {ip}; }

// static
VfpRegList Assembler::DefaultFPTmpList() {
  if (CpuFeatures::IsSupported(VFP32DREGS)) {
    // Make sure we pick two D registers which alias a Q register. This way, we
    // can use a Q as a scratch if NEON is supported.
    return d14.ToVfpRegList() | d15.ToVfpRegList();
  } else {
    // When VFP32DREGS is not supported, d15 become allocatable. Therefore we
    // cannot use it as a scratch.
    return d14.ToVfpRegList();
  }
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
  CheckConstPool(true, false);
  DCHECK(pending_32_bit_constants_.empty());

  int code_comments_size = WriteCodeComments();

  AllocateAndInstallRequestedHeapNumbers(isolate);

  // Set up code descriptor.
  // TODO(jgruber): Reconsider how these offsets and sizes are maintained up to
  // this point to make CodeDesc initialization less fiddly.

  static constexpr int kConstantPoolSize = 0;
  const int instruction_size = pc_offset();
  const int code_comments_offset = instruction_size - code_comments_size;
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
                       code_comments_offset, reloc_info_offset);
}

void Assembler::Align(int m) {
  DCHECK(m >= 4 && base::bits::IsPowerOfTwo(m));
  DCHECK_EQ(pc_offset() & (kInstrSize - 1), 0);
  while ((pc_offset() & (m - 1)) != 0) {
    nop();
  }
}

void Assembler::CodeTargetAlign() {
  // Preferred alignment of jump targets on some ARM chips.
  Align(8);
}

Condition Assembler::GetCondition(Instr instr) {
  return Instruction::ConditionField(instr);
}

bool Assembler::IsLdrRegisterImmediate(Instr instr) {
  return (instr & (B27 | B26 | B25 | B22 | B20)) == (B26 | B20);
}

bool Assembler::IsVldrDRegisterImmediate(Instr instr) {
  return (instr & (15 * B24 | 3 * B20 | 15 * B8)) == (13 * B24 | B20 | 11 * B8);
}

int Assembler::GetLdrRegisterImmediateOffset(Instr instr) {
  DCHECK(IsLdrRegisterImmediate(instr));
  bool positive = (instr & B23) == B23;
  int offset = instr & kOff12Mask;  // Zero extended offset.
  return positive ? offset : -offset;
}

int Assembler::GetVldrDRegisterImmediateOffset(Instr instr) {
  DCHECK(IsVldrDRegisterImmediate(instr));
  bool positive = (instr & B23) == B23;
  int offset = instr & kOff8Mask;  // Zero extended offset.
  offset <<= 2;
  return positive ? offset : -offset;
}

Instr Assembler::SetLdrRegisterImmediateOffset(Instr instr, int offset) {
  DCHECK(IsLdrRegisterImmediate(instr));
  bool positive = offset >= 0;
  if (!positive) offset = -offset;
  DCHECK(is_uint12(offset));
  // Set bit indicating whether the offset should be added.
  instr = (instr & ~B23) | (positive ? B23 : 0);
  // Set the actual offset.
  return (instr & ~kOff12Mask) | offset;
}

Instr Assembler::SetVldrDRegisterImmediateOffset(Instr instr, int offset) {
  DCHECK(IsVldrDRegisterImmediate(instr));
  DCHECK((offset & ~3) == offset);  // Must be 64-bit aligned.
  bool positive = offset >= 0;
  if (!positive) offset = -offset;
  DCHECK(is_uint10(offset));
  // Set bit indicating whether the offset should be added.
  instr = (instr & ~B23) | (positive ? B23 : 0);
  // Set the actual offset. Its bottom 2 bits are zero.
  return (instr & ~kOff8Mask) | (offset >> 2);
}

bool Assembler::IsStrRegisterImmediate(Instr instr) {
  return (instr & (B27 | B26 | B25 | B22 | B20)) == B26;
}

Instr Assembler::SetStrRegisterImmediateOffset(Instr instr, int offset) {
  DCHECK(IsStrRegisterImmediate(instr));
  bool positive = offset >= 0;
  if (!positive) offset = -offset;
  DCHECK(is_uint12(offset));
  // Set bit indicating whether the offset should be added.
  instr = (instr & ~B23) | (positive ? B23 : 0);
  // Set the actual offset.
  return (instr & ~kOff12Mask) | offset;
}

bool Assembler::IsAddRegisterImmediate(Instr instr) {
  return (instr & (B27 | B26 | B25 | B24 | B23 | B22 | B21)) == (B25 | B23);
}

Instr Assembler::SetAddRegisterImmediateOffset(Instr instr, int offset) {
  DCHECK(IsAddRegisterImmediate(instr));
  DCHECK_GE(offset, 0);
  DCHECK(is_uint12(offset));
  // Set the offset.
  return (instr & ~kOff12Mask) | offset;
}

Register Assembler::GetRd(Instr instr) {
  return Register::from_code(Instruction::RdValue(instr));
}

Register Assembler::GetRn(Instr instr) {
  return Register::from_code(Instruction::RnValue(instr));
}

Register Assembler::GetRm(Instr instr) {
  return Register::from_code(Instruction::RmValue(instr));
}

bool Assembler::IsPush(Instr instr) {
  return ((instr & ~kRdMask) == kPushRegPattern);
}

bool Assembler::IsPop(Instr instr) {
  return ((instr & ~kRdMask) == kPopRegPattern);
}

bool Assembler::IsStrRegFpOffset(Instr instr) {
  return ((instr & kLdrStrInstrTypeMask) == kStrRegFpOffsetPattern);
}

bool Assembler::IsLdrRegFpOffset(Instr instr) {
  return ((instr & kLdrStrInstrTypeMask) == kLdrRegFpOffsetPattern);
}

bool Assembler::IsStrRegFpNegOffset(Instr instr) {
  return ((instr & kLdrStrInstrTypeMask) == kStrRegFpNegOffsetPattern);
}

bool Assembler::IsLdrRegFpNegOffset(Instr instr) {
  return ((instr & kLdrStrInstrTypeMask) == kLdrRegFpNegOffsetPattern);
}

bool Assembler::IsLdrPcImmediateOffset(Instr instr) {
  // Check the instruction is indeed a
  // ldr<cond> <Rd>, [pc +/- offset_12].
  return (instr & kLdrPCImmedMask) == kLdrPCImmedPattern;
}

bool Assembler::IsBOrBlPcImmediateOffset(Instr instr) {
  return (instr & kBOrBlPCImmedMask) == kBOrBlPCImmedPattern;
}

bool Assembler::IsVldrDPcImmediateOffset(Instr instr) {
  // Check the instruction is indeed a
  // vldr<cond> <Dd>, [pc +/- offset_10].
  return (instr & kVldrDPCMask) == kVldrDPCPattern;
}

bool Assembler::IsBlxReg(Instr instr) {
  // Check the instruction is indeed a
  // blxcc <Rm>
  return (instr & kBlxRegMask) == kBlxRegPattern;
}

bool Assembler::IsBlxIp(Instr instr) {
  // Check the instruction is indeed a
  // blx ip
  return instr == kBlxIp;
}

bool Assembler::IsTstImmediate(Instr instr) {
  return (instr & (B27 | B26 | I | kOpCodeMask | S | kRdMask)) == (I | TST | S);
}

bool Assembler::IsCmpRegister(Instr instr) {
  return (instr & (B27 | B26 | I | kOpCodeMask | S | kRdMask | B4)) ==
         (CMP | S);
}

bool Assembler::IsCmpImmediate(Instr instr) {
  return (instr & (B27 | B26 | I | kOpCodeMask | S | kRdMask)) == (I | CMP | S);
}

Register Assembler::GetCmpImmediateRegister(Instr instr) {
  DCHECK(IsCmpImmediate(instr));
  return GetRn(instr);
}

int Assembler::GetCmpImmediateRawImmediate(Instr instr) {
  DCHECK(IsCmpImmediate(instr));
  return instr & kOff12Mask;
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
//
// The linked labels form a link chain by making the branch offset
// in the instruction steam to point to the previous branch
// instruction using the same label.
//
// The link chain is terminated by a branch offset pointing to the
// same position.

int Assembler::target_at(int pos) {
  Instr instr = instr_at(pos);
  if (is_uint24(instr)) {
    // Emitted link to a label, not part of a branch.
    return instr;
  }
  DCHECK_EQ(5 * B25, instr & 7 * B25);  // b, bl, or blx imm24
  int imm26 = ((instr & kImm24Mask) << 8) >> 6;
  if ((Instruction::ConditionField(instr) == kSpecialCondition) &&
      ((instr & B24) != 0)) {
    // blx uses bit 24 to encode bit 2 of imm26
    imm26 += 2;
  }
  return pos + Instruction::kPcLoadDelta + imm26;
}

void Assembler::target_at_put(int pos, int target_pos) {
  Instr instr = instr_at(pos);
  if (is_uint24(instr)) {
    DCHECK(target_pos == pos || target_pos >= 0);
    // Emitted link to a label, not part of a branch.
    // Load the position of the label relative to the generated code object
    // pointer in a register.

    // The existing code must be a single 24-bit label chain link, followed by
    // nops encoding the destination register. See mov_label_offset.

    // Extract the destination register from the first nop instructions.
    Register dst =
        Register::from_code(Instruction::RmValue(instr_at(pos + kInstrSize)));
    // In addition to the 24-bit label chain link, we expect to find one nop for
    // ARMv7 and above, or two nops for ARMv6. See mov_label_offset.
    DCHECK(IsNop(instr_at(pos + kInstrSize), dst.code()));
    if (!CpuFeatures::IsSupported(ARMv7)) {
      DCHECK(IsNop(instr_at(pos + 2 * kInstrSize), dst.code()));
    }

    // Here are the instructions we need to emit:
    //   For ARMv7: target24 => target16_1:target16_0
    //      movw dst, #target16_0
    //      movt dst, #target16_1
    //   For ARMv6: target24 => target8_2:target8_1:target8_0
    //      mov dst, #target8_0
    //      orr dst, dst, #target8_1 << 8
    //      orr dst, dst, #target8_2 << 16

    uint32_t target24 =
        target_pos + (InstructionStream::kHeaderSize - kHeapObjectTag);
    CHECK(is_uint24(target24));
    if (is_uint8(target24)) {
      // If the target fits in a byte then only patch with a mov
      // instruction.
      PatchingAssembler patcher(
          options(), reinterpret_cast<uint8_t*>(buffer_start_ + pos), 1);
      patcher.mov(dst, Operand(target24));
    } else {
      uint16_t target16_0 = target24 & kImm16Mask;
      uint16_t target16_1 = target24 >> 16;
      if (CpuFeatures::IsSupported(ARMv7)) {
        // Patch with movw/movt.
        if (target16_1 == 0) {
          PatchingAssembler patcher(
              options(), reinterpret_cast<uint8_t*>(buffer_start_ + pos), 1);
          CpuFeatureScope scope(&patcher, ARMv7);
          patcher.movw(dst, target16_0);
        } else {
          PatchingAssembler patcher(
              options(), reinterpret_cast<uint8_t*>(buffer_start_ + pos), 2);
          CpuFeatureScope scope(&patcher, ARMv7);
          patcher.movw(dst, target16_0);
          patcher.movt(dst, target16_1);
        }
      } else {
        // Patch with a sequence of mov/orr/orr instructions.
        uint8_t target8_0 = target16_0 & kImm8Mask;
        uint8_t target8_1 = target16_0 >> 8;
        uint8_t target8_2 = target16_1 & kImm8Mask;
        if (target8_2 == 0) {
          PatchingAssembler patcher(
              options(), reinterpret_cast<uint8_t*>(buffer_start_ + pos), 2);
          patcher.mov(dst, Operand(target8_0));
          patcher.orr(dst, dst, Operand(target8_1 << 8));
        } else {
          PatchingAssembler patcher(
              options(), reinterpret_cast<uint8_t*>(buffer_start_ + pos), 3);
          patcher.mov(dst, Operand(target8_0));
          patcher.orr(dst, dst, Operand(target8_1 << 8));
          patcher.orr(dst, dst, Operand(target8_2 << 16));
        }
      }
    }
    return;
  }
  int imm26 = target_pos - (pos + Instruction::kPcLoadDelta);
  DCHECK_EQ(5 * B25, instr & 7 * B25);  // b, bl, or blx imm24
  if (Instruction::ConditionField(instr) == kSpecialCondition) {
    // blx uses bit 24 to encode bit 2 of imm26
    DCHECK_EQ(0, imm26 & 1);
    instr = (instr & ~(B24 | kImm24Mask)) | ((imm26 & 2) >> 1) * B24;
  } else {
    DCHECK_EQ(0, imm26 & 3);
    instr &= ~kImm24Mask;
  }
  int imm24 = imm26 >> 2;
  CHECK(is_int24(imm24));
  instr_at_put(pos, instr | (imm24 & kImm24Mask));
}

void Assembler::print(const Label* L) {
  if (L->is_unused()) {
    PrintF("unused label\n");
  } else if (L->is_bound()) {
    PrintF("bound label to %d\n", L->pos());
  } else if (L->is_linked()) {
    Label l;
    l.link_to(L->pos());
    PrintF("unbound label");
    while (l.is_linked()) {
      PrintF("@ %d ", l.pos());
      Instr instr = instr_at(l.pos());
      if ((instr & ~kImm24Mask) == 0) {
        PrintF("value\n");
      } else {
        DCHECK_EQ(instr & 7 * B25, 5 * B25);  // b, bl, or blx
        Condition cond = Instruction::ConditionField(instr);
        const char* b;
        const char* c;
        if (cond == kSpecialCondition) {
          b = "blx";
          c = "";
        } else {
          if ((instr & B24) != 0)
            b = "bl";
          else
            b = "b";

          switch (cond) {
            case eq:
              c = "eq";
              break;
            case ne:
              c = "ne";
              break;
            case hs:
              c = "hs";
              break;
            case lo:
              c = "lo";
              break;
            case mi:
              c = "mi";
              break;
            case pl:
              c = "pl";
              break;
            case vs:
              c = "vs";
              break;
            case vc:
              c = "vc";
              break;
            case hi:
              c = "hi";
              break;
            case ls:
              c = "ls";
              break;
            case ge:
              c = "ge";
              break;
            case lt:
              c = "lt";
              break;
            case gt:
              c = "gt";
              break;
            case le:
              c = "le";
              break;
            case al:
              c = "";
              break;
            default:
              c = "";
              UNREACHABLE();
          }
        }
        PrintF("%s%s\n", b, c);
      }
      next(&l);
    }
  } else {
    PrintF("label in inconsistent state (pos = %d)\n", L->pos_);
  }
}

void Assembler::bind_to(Label* L, int pos) {
  DCHECK(0 <= pos && pos <= pc_offset());  // must have a valid binding position
  while (L->is_linked()) {
    int fixup_pos = L->pos();
    next(L);  // call next before overwriting link with target at fixup_pos
    target_at_put(fixup_pos, pos);
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
  if (link == L->pos()) {
    // Branch target points to the same instruction. This is the end of the link
    // chain.
    L->Unuse();
  } else {
    DCHECK_GE(link, 0);
    L->link_to(link);
  }
}

namespace {

// Low-level code emission routines depending on the addressing mode.
// If this returns true then you have to use the rotate_imm and immed_8
// that it returns, because it may have already changed the instruction
// to match them!
bool FitsShifter(uint32_t imm32, uint32_t* rotate_imm, uint32_t* immed_8,
                 Instr* instr) {
  // imm32 must be unsigned.
  {
    // 32-bit immediates can be encoded as:
    //   (8-bit value, 2*N bit left rotation)
    // e.g. 0xab00 can be encoded as 0xab shifted left by 8 == 2*4, i.e.
    //   (0xab, 4)
    //
    // Check three categories which cover all possible shifter fits:
    //   1. 0x000000FF: The value is already 8-bit (no shifting necessary),
    //   2. 0x000FF000: The 8-bit value is somewhere in the middle of the 32-bit
    //                  value, and
    //   3. 0xF000000F: The 8-bit value is split over the beginning and end of
    //                  the 32-bit value.

    // For 0x000000FF.
    if (imm32 <= 0xFF) {
      *rotate_imm = 0;
      *immed_8 = imm32;
      return true;
    }
    // For 0x000FF000, count trailing zeros and shift down to 0x000000FF. Note
    // that we have to round the trailing zeros down to the nearest multiple of
    // two, since we can only encode shifts of 2*N. Note also that we know that
    // imm32 isn't zero, since we already checked if it's less than 0xFF.
    int half_trailing_zeros = base::bits::CountTrailingZerosNonZero(imm32) / 2;
    uint32_t imm8 = imm32 >> (half_trailing_zeros * 2);
    if (imm8 <= 0xFF) {
      DCHECK_GT(half_trailing_zeros, 0);
      // Rotating right by trailing_zeros is equivalent to rotating left by
      // 32 - trailing_zeros. We return rotate_right / 2, so calculate
      // (32 - trailing_zeros)/2 == 16 - trailing_zeros/2.
      *rotate_imm = (16 - half_trailing_zeros);
      *immed_8 = imm8;
      return true;
    }
    // For 0xF000000F, rotate by 16 to get 0x000FF000 and continue as if it
    // were that case.
    uint32_t imm32_rot16 = base::bits::RotateLeft32(imm32, 16);
    half_trailing_zeros =
        base::bits::CountTrailingZerosNonZero(imm32_rot16) / 2;
    imm8 = imm32_rot16 >> (half_trailing_zeros * 2);
    if (imm8 <= 0xFF) {
      // We've rotated left by 2*8, so we can't have more than that many
      // trailing zeroes.
      DCHECK_LT(half_trailing_zeros, 8);
      // We've already rotated by 2*8, before calculating trailing_zeros/2,
      // so we need (32 - (16 + trailing_zeros))/2 == 8 - trailing_zeros/2.
      *rotate_imm = 8 - half_trailing_zeros;
      *immed_8 = imm8;
      return true;
    }
  }
  // If the opcode is one with a complementary version and the complementary
  // immediate fits, change the opcode.
  if (instr != nullptr) {
    if ((*instr & kMovMvnMask) == kMovMvnPattern) {
      if (FitsShifter(~imm32, rotate_imm, immed_8, nullptr)) {
        *instr ^= kMovMvnFlip;
        return true;
      } else if ((*instr & kMovLeaveCCMask) == kMovLeaveCCPattern) {
        if (CpuFeatures::IsSupported(ARMv7)) {
          if (imm32 < 0x10000) {
            *instr ^= kMovwLeaveCCFlip;
            *instr |= Assembler::EncodeMovwImmediate(imm32);
            *rotate_imm = *immed_8 = 0;  // Not used for movw.
            return true;
          }
        }
      }
    } else if ((*instr & kCmpCmnMask) == kCmpCmnPattern) {
      if (FitsShifter(-static_cast<int>(imm32), rotate_imm, immed_8, nullptr)) {
        *instr ^= kCmpCmnFlip;
        return true;
      }
    } else {
      Instr alu_insn = (*instr & kALUMask);
      if (alu_insn == ADD || alu_insn == SUB) {
        if (FitsShifter(-static_cast<int>(imm32), rotate_imm, immed_8,
                        nullptr)) {
          *instr ^= kAddSubFlip;
          return true;
        }
      } else if (alu_insn == AND || alu_insn == BIC) {
        if (FitsShifter(~imm32, rotate_imm, immed_8, nullptr)) {
          *instr ^= kAndBicFlip;
          return true;
        }
      }
    }
  }
  return false;
}

// We have to use the temporary register for things that can be relocated even
// if they can be encoded in the ARM's 12 bits of immediate-offset instruction
// space.  There is no guarantee that the relocated location can be similarly
// encoded.
bool MustOutputRelocInfo(RelocInfo::Mode rmode, const Assembler* assembler) {
  if (RelocInfo::IsOnlyForSerializer(rmode)) {
    if (assembler->predictable_code_size()) return true;
    return assembler->options().record_reloc_info_for_serialization;
  } else if (RelocInfo::IsNoInfo(rmode)) {
    return false;
  }
  return true;
}

bool UseMovImmediateLoad(const Operand& x, const Assembler* assembler) {
  DCHECK_NOT_NULL(assembler);
  if (x.MustOutputRelocInfo(assembler)) {
    // Prefer constant pool if data is likely to be patched.
    return false;
  } else {
    // Otherwise, use immediate load if movw / movt is available.
    return CpuFeatures::IsSupported(ARMv7);
  }
}

}  // namespace

bool Operand::MustOutputRelocInfo(const Assembler* assembler) const {
  return v8::internal::MustOutputRelocInfo(rmode_, assembler);
}

int Operand::InstructionsRequired(const Assembler* assembler,
                                  Instr instr) const {
  DCHECK_NOT_NULL(assembler);
  if (rm_.is_valid()) return 1;
  uint32_t dummy1, dummy2;
  if (MustOutputRelocInfo(assembler) ||
      !FitsShifter(immediate(), &dummy1, &dummy2, &instr)) {
    // The immediate operand cannot be encoded as a shifter operand, or use of
    // constant pool is required.  First account for the instructions required
    // for the constant pool or immediate load
    int instructions;
    if (UseMovImmediateLoad(*this, assembler)) {
      DCHECK(CpuFeatures::IsSupported(ARMv7));
      // A movw / movt immediate load.
      instructions = 2;
    } else {
      // A small constant pool load.
      instructions = 1;
    }
    if ((instr & ~kCondMask) != 13 * B21) {  // mov, S not set
      // For a mov or mvn instruction which doesn't set the condition
      // code, the constant pool or immediate load is enough, otherwise we need
      // to account for the actual instruction being requested.
      instructions += 1;
    }
    return instructions;
  } else {
    // No use of constant pool and the immediate operand can be encoded as a
    // shifter operand.
    return 1;
  }
}

void Assembler::Move32BitImmediate(Register rd, const Operand& x,
                                   Condition cond) {
  if (UseMovImmediateLoad(x, this)) {
    CpuFeatureScope scope(this, ARMv7);
    // UseMovImmediateLoad should return false when we need to output
    // relocation info, since we prefer the constant pool for values that
    // can be patched.
    DCHECK(!x.MustOutputRelocInfo(this));
    UseScratchRegisterScope temps(this);
    // Re-use the destination register as a scratch if possible.
    Register target = rd != pc && rd != sp ? rd : temps.Acquire();
    uint32_t imm32 = static_cast<uint32_t>(x.immediate());
    movw(target, imm32 & 0xFFFF, cond);
    movt(target, imm32 >> 16, cond);
    if (target.code() != rd.code()) {
      mov(rd, target, LeaveCC, cond);
    }
  } else {
    int32_t immediate;
    if (x.IsHeapNumberRequest()) {
      RequestHeapNumber(x.heap_number_request());
      immediate = 0;
    } else {
      immediate = x.immediate();
    }
    ConstantPoolAddEntry(pc_offset(), x.rmode_, immediate);
    ldr_pcrel(rd, 0, cond);
  }
}

void Assembler::AddrMode1(Instr instr, Register rd, Register rn,
                          const Operand& x) {
  CheckBuffer();
  uint32_t opcode = instr & kOpCodeMask;
  bool set_flags = (instr & S) != 0;
  DCHECK((opcode == ADC) || (opcode == ADD) || (opcode == AND) ||
         (opcode == BIC) || (opcode == EOR) || (opcode == ORR) ||
         (opcode == RSB) || (opcode == RSC) || (opcode == SBC) ||
         (opcode == SUB) || (opcode == CMN) || (opcode == CMP) ||
         (opcode == TEQ) || (opcode == TST) || (opcode == MOV) ||
         (opcode == MVN));
  // For comparison instructions, rd is not defined.
  DCHECK(rd.is_valid() || (opcode == CMN) || (opcode == CMP) ||
         (opcode == TEQ) || (opcode == TST));
  // For move instructions, rn is not defined.
  DCHECK(rn.is_valid() || (opcode == MOV) || (opcode == MVN));
  DCHECK(rd.is_valid() || rn.is_valid());
  DCHECK_EQ(instr & ~(kCondMask | kOpCodeMask | S), 0);
  if (!AddrMode1TryEncodeOperand(&instr, x)) {
    DCHECK(x.IsImmediate());
    // Upon failure to encode, the opcode should not have changed.
    DCHECK(opcode == (instr & kOpCodeMask));
    UseScratchRegisterScope temps(this);
    Condition cond = Instruction::ConditionField(instr);
    if ((opcode == MOV) && !set_flags) {
      // Generate a sequence of mov instructions or a load from the constant
      // pool only for a MOV instruction which does not set the flags.
      DCHECK(!rn.is_valid());
      Move32BitImmediate(rd, x, cond);
    } else if ((opcode == ADD || opcode == SUB) && !set_flags && (rd == rn) &&
               !temps.CanAcquire()) {
      // Split the operation into a sequence of additions if we cannot use a
      // scratch register. In this case, we cannot re-use rn and the assembler
      // does not have any scratch registers to spare.
      uint32_t imm = x.immediate();
      do {
        // The immediate encoding format is composed of 8 bits of data and 4
        // bits encoding a rotation. Each of the 16 possible rotations accounts
        // for a rotation by an even number.
        //   4 bits -> 16 rotations possible
        //          -> 16 rotations of 2 bits each fits in a 32-bit value.
        // This means that finding the even number of trailing zeroes of the
        // immediate allows us to more efficiently split it:
        int trailing_zeroes = base::bits::CountTrailingZeros(imm) & ~1u;
        uint32_t mask = (0xFF << trailing_zeroes);
        if (opcode == ADD) {
          add(rd, rd, Operand(imm & mask), LeaveCC, cond);
        } else {
          DCHECK_EQ(opcode, SUB);
          sub(rd, rd, Operand(imm & mask), LeaveCC, cond);
        }
        imm = imm & ~mask;
      } while (!ImmediateFitsAddrMode1Instruction(imm));
      if (opcode == ADD) {
        add(rd, rd, Operand(imm), LeaveCC, cond);
      } else {
        DCHECK_EQ(opcode, SUB);
        sub(rd, rd, Operand(imm), LeaveCC, cond);
      }
    } else {
      // The immediate operand cannot be encoded as a shifter operand, so load
      // it first to a scratch register and change the original instruction to
      // use it.
      // Re-use the destination register if possible.
      Register scratch = (rd.is_valid() && rd != rn && rd != pc && rd != sp)
                             ? rd
                             : temps.Acquire();
      mov(scratch, x, LeaveCC, cond);
      AddrMode1(instr, rd, rn, Operand(scratch));
    }
    return;
  }
  if (!rd.is_valid()) {
    // Emit a comparison instruction.
    emit(instr | rn.code() * B16);
  } else if (!rn.is_valid()) {
    // Emit a move instruction. If the operand is a register-shifted register,
    // then prevent the destination from being PC as this is unpredictable.
    DCHECK(!x.IsRegisterShiftedRegister() || rd != pc);
    emit(instr | rd.code() * B12);
  } else {
    emit(instr | rn.code() * B16 | rd.code() * B12);
  }
  if (rn == pc || x.rm_ == pc) {
    // Block constant pool emission for one instruction after reading pc.
    BlockConstPoolFor(1);
  }
}

bool Assembler::AddrMode1TryEncodeOperand(Instr* instr, const Operand& x) {
  if (x.IsImmediate()) {
    // Immediate.
    uint32_t rotate_imm;
    uint32_t immed_8;
    if (x.MustOutputRelocInfo(this) ||
        !FitsShifter(x.immediate(), &rotate_imm, &immed_8, instr)) {
      // Let the caller handle generating multiple instructions.
      return false;
    }
    *instr |= I | rotate_imm * B8 | immed_8;
  } else if (x.IsImmediateShiftedRegister()) {
    *instr |= x.shift_imm_ * B7 | x.shift_op_ | x.rm_.code();
  } else {
    DCHECK(x.IsRegisterShiftedRegister());
    // It is unpredictable to use the PC in this case.
    DCHECK(x.rm_ != pc && x.rs_ != pc);
    *instr |= x.rs_.code() * B8 | x.shift_op_ | B4 | x.rm_.code();
  }

  return true;
}

void Assembler::AddrMode2(Instr instr, Register rd, const MemOperand& x) {
  DCHECK((instr & ~(kCondMask | B | L)) == B26);
  // This method does not handle pc-relative addresses. ldr_pcrel() should be
  // used instead.
  DCHECK(x.rn_ != pc);
  int am = x.am_;
  if (!x.rm_.is_valid()) {
    // Immediate offset.
    int offset_12 = x.offset_;
    if (offset_12 < 0) {
      offset_12 = -offset_12;
      am ^= U;
    }
    if (!is_uint12(offset_12)) {
      // Immediate offset cannot be encoded, load it first to a scratch
      // register.
      UseScratchRegisterScope temps(this);
      // Allow re-using rd for load instructions if possible.
      bool is_load = (instr & L) == L;
      Register scratch = (is_load && rd != x.rn_ && rd != pc && rd != sp)
                             ? rd
                             : temps.Acquire();
      mov(scratch, Operand(x.offset_), LeaveCC,
          Instruction::ConditionField(instr));
      AddrMode2(instr, rd, MemOperand(x.rn_, scratch, x.am_));
      return;
    }
    DCHECK_GE(offset_12, 0);  // no masking needed
    instr |= offset_12;
  } else {
    // Register offset (shift_imm_ and shift_op_ are 0) or scaled
    // register offset the constructors make sure than both shift_imm_
    // and shift_op_ are initialized.
    DCHECK(x.rm_ != pc);
    instr |= B25 | x.shift_imm_ * B7 | x.shift_op_ | x.rm_.code();
  }
  DCHECK((am & (P | W)) == P || x.rn_ != pc);  // no pc base with writeback
  emit(instr | am | x.rn_.code() * B16 | rd.code() * B12);
}

void Assembler::AddrMode3(Instr instr, Register rd, const MemOperand& x) {
  DCHECK((instr & ~(kCondMask | L | S6 | H)) == (B4 | B7));
  DCHECK(x.rn_.is_valid());
  // This method does not handle pc-relative addresses. ldr_pcrel() should be
  // used instead.
  DCHECK(x.rn_ != pc);
  int am = x.am_;
  bool is_load = (instr & L) == L;
  if (!x.rm_.is_valid()) {
    // Immediate offset.
    int offset_8 = x.offset_;
    if (offset_8 < 0) {
      offset_8 = -offset_8;
      am ^= U;
    }
    if (!is_uint8(offset_8)) {
      // Immediate offset cannot be encoded, load it first to a scratch
      // register.
      UseScratchRegisterScope temps(this);
      // Allow re-using rd for load instructions if possible.
      Register scratch = (is_load && rd != x.rn_ && rd != pc && rd != sp)
                             ? rd
                             : temps.Acquire();
      mov(scratch, Operand(x.offset_), LeaveCC,
          Instruction::ConditionField(instr));
      AddrMode3(instr, rd, MemOperand(x.rn_, scratch, x.am_));
      return;
    }
    DCHECK_GE(offset_8, 0);  // no masking needed
    instr |= B | (offset_8 >> 4) * B8 | (offset_8 & 0xF);
  } else if (x.shift_imm_ != 0) {
    // Scaled register offsets are not supported, compute the offset separately
    // to a scratch register.
    UseScratchRegisterScope temps(this);
    // Allow re-using rd for load instructions if possible.
    Register scratch =
        (is_load && rd != x.rn_ && rd != pc && rd != sp) ? rd : temps.Acquire();
    mov(scratch, Operand(x.rm_, x.shift_op_, x.shift_imm_), LeaveCC,
        Instruction::ConditionField(instr));
    AddrMode3(instr, rd, MemOperand(x.rn_, scratch, x.am_));
    return;
  } else {
    // Register offset.
    DCHECK((am & (P | W)) == P || x.rm_ != pc);  // no pc index with writeback
    instr |= x.rm_.code();
  }
  DCHECK((am & (P | W)) == P || x.rn_ != pc);  // no pc base with writeback
  emit(instr | am | x.rn_.code() * B16 | rd.code() * B12);
}

void Assembler::AddrMode4(Instr instr, Register rn, RegList rl) {
  DCHECK((instr & ~(kCondMask | P | U | W | L)) == B27);
  DCHECK(!rl.is_empty());
  DCHECK(rn != pc);
  emit(instr | rn.code() * B16 | rl.bits());
}

void Assembler::AddrMode5(Instr instr, CRegister crd, const MemOperand& x) {
  // Unindexed addressing is not encoded by this function.
  DCHECK_EQ((B27 | B26),
            (instr & ~(kCondMask | kCoprocessorMask | P | U | N | W | L)));
  DCHECK(x.rn_.is_valid() && !x.rm_.is_valid());
  int am = x.am_;
  int offset_8 = x.offset_;
  DCHECK_EQ(offset_8 & 3, 0);  // offset must be an aligned word offset
  offset_8 >>= 2;
  if (offset_8 < 0) {
    offset_8 = -offset_8;
    am ^= U;
  }
  DCHECK(is_uint8(offset_8));  // unsigned word offset must fit in a byte
  DCHECK((am & (P | W)) == P || x.rn_ != pc);  // no pc base with writeback

  // Post-indexed addressing requires W == 1; different than in AddrMode2/3.
  if ((am & P) == 0) am |= W;

  DCHECK_GE(offset_8, 0);  // no masking needed
  emit(instr | am | x.rn_.code() * B16 | crd.code() * B12 | offset_8);
}

int Assembler::branch_offset(Label* L) {
  int target_pos;
  if (L->is_bound()) {
    target_pos = L->pos();
  } else {
    if (L->is_linked()) {
      // Point to previous instruction that uses the link.
      target_pos = L->pos();
    } else {
      // First entry of the link chain points to itself.
      target_pos = pc_offset();
    }
    L->link_to(pc_offset());
  }

  return target_pos - (pc_offset() + Instruction::kPcLoadDelta);
}

// Branch instructions.
void Assembler::b(int branch_offset, Condition cond, RelocInfo::Mode rmode) {
  if (!RelocInfo::IsNoInfo(rmode)) RecordRelocInfo(rmode);
  DCHECK_EQ(branch_offset & 3, 0);
  int imm24 = branch_offset >> 2;
  const bool b_imm_check = is_int24(imm24);
  CHECK(b_imm_check);

  // Block the emission of the constant pool before the next instruction.
  // Otherwise the passed-in branch offset would be off.
  BlockConstPoolFor(1);

  emit(cond | B27 | B25 | (imm24 & kImm24Mask));

  if (cond == al) {
    // Dead code is a good location to emit the constant pool.
    CheckConstPool(false, false);
  }
}

void Assembler::bl(int branch_offset, Condition cond, RelocInfo::Mode rmode) {
  if (!RelocInfo::IsNoInfo(rmode)) RecordRelocInfo(rmode);
  DCHECK_EQ(branch_offset & 3, 0);
  int imm24 = branch_offset >> 2;
  const bool bl_imm_check = is_int24(imm24);
  CHECK(bl_imm_check);

  // Block the emission of the constant pool before the next instruction.
  // Otherwise the passed-in branch offset would be off.
  BlockConstPoolFor(1);

  emit(cond | B27 | B25 | B24 | (imm24 & kImm24Mask));
}

void Assembler::blx(int branch_offset) {
  DCHECK_EQ(branch_offset & 1, 0);
  int h = ((branch_offset & 2) >> 1) * B24;
  int imm24 = branch_offset >> 2;
  const bool blx_imm_check = is_int24(imm24);
  CHECK(blx_imm_check);

  // Block the emission of the constant pool before the next instruction.
  // Otherwise the passed-in branch offset would be off.
  BlockConstPoolFor(1);

  emit(kSpecialCondition | B27 | B25 | h | (imm24 & kImm24Mask));
}

void Assembler::blx(Register target, Condition cond) {
  DCHECK(target != pc);
  emit(cond | B24 | B21 | 15 * B16 | 15 * B12 | 15 * B8 | BLX | target.code());
}

void Assembler::bx(Register target, Condition cond) {
  DCHECK(target != pc);  // use of pc is actually allowed, but discouraged
  emit(cond | B24 | B21 | 15 * B16 | 15 * B12 | 15 * B8 | BX | target.code());
}

void Assembler::b(Label* L, Condition cond) {
  CheckBuffer();
  b(branch_offset(L), cond);
}

void Assembler::bl(Label* L, Condition cond) {
  CheckBuffer();
  bl(branch_offset(L), cond);
}

void Assembler::blx(Label* L) {
  CheckBuffer();
  blx(branch_offset(L));
}

// Data-processing instructions.

void Assembler::and_(Register dst, Register src1, const Operand& src2, SBit s,
                     Condition cond) {
  AddrMode1(cond | AND | s, dst, src1, src2);
}

void Assembler::and_(Register dst, Register src1, Register src2, SBit s,
                     Condition cond) {
  and_(dst, src1, Operand(src2), s, cond);
}

void Assembler::eor(Register dst, Register src1, const Operand& src2, SBit s,
                    Condition cond) {
  AddrMode1(cond | EOR | s, dst, src1, src2);
}

void Assembler::eor(Register dst, Register src1, Register src2, SBit s,
                    Condition cond) {
  AddrMode1(cond | EOR | s, dst, src1, Operand(src2));
}

void Assembler::sub(Register dst, Register src1, const Operand& src2, SBit s,
                    Condition cond) {
  AddrMode1(cond | SUB | s, dst, src1, src2);
}

void Assembler::sub(Register dst, Register src1, Register src2, SBit s,
                    Condition cond) {
  sub(dst, src1, Operand(src2), s, cond);
}

void Assembler::rsb(Register dst, Register src1, const Operand& src2, SBit s,
                    Condition cond) {
  AddrMode1(cond | RSB | s, dst, src1, src2);
}

void Assembler::add(Register dst, Register src1, const Operand& src2, SBit s,
                    Condition cond) {
  AddrMode1(cond | ADD | s, dst, src1, src2);
}

void Assembler::add(Register dst, Register src1, Register src2, SBit s,
                    Condition cond) {
  add(dst, src1, Operand(src2), s, cond);
}

void Assembler::adc(Register dst, Register src1, const Operand& src2, SBit s,
                    Condition cond) {
  AddrMode1(cond | ADC | s, dst, src1, src2);
}

void Assembler::sbc(Register dst, Register src1, const Operand& src2, SBit s,
                    Condition cond) {
  AddrMode1(cond | SBC | s, dst, src1, src2);
}

void Assembler::rsc(Register dst, Register src1, const Operand& src2, SBit s,
                    Condition cond) {
  AddrMode1(cond | RSC | s, dst, src1, src2);
}

void Assembler::tst(Register src1, const Operand& src2, Condition cond) {
  AddrMode1(cond | TST | S, no_reg, src1, src2);
}

void Assembler::tst(Register src1, Register src2, Condition cond) {
  tst(src1, Operand(src2), cond);
}

void Assembler::teq(Register src1, const Operand& src2, Condition cond) {
  AddrMode1(cond | TEQ | S, no_reg, src1, src2);
}

void Assembler::cmp(Register src1, const Operand& src2, Condition cond) {
  AddrMode1(cond | CMP | S, no_reg, src1, src2);
}

void Assembler::cmp(Register src1, Register src2, Condition cond) {
  cmp(src1, Operand(src2), cond);
}

void Assembler::cmp_raw_immediate(Register src, int raw_immediate,
                                  Condition cond) {
  DCHECK(is_uint12(raw_immediate));
  emit(cond | I | CMP | S | src.code() << 16 | raw_immediate);
}

void Assembler::cmn(Register src1, const Operand& src2, Condition cond) {
  AddrMode1(cond | CMN | S, no_reg, src1, src2);
}

void Assembler::orr(Register dst, Register src1, const Operand& src2, SBit s,
                    Condition cond) {
  AddrMode1(cond | ORR | s, dst, src1, src2);
}

void Assembler::orr(Register dst, Register src1, Register src2, SBit s,
                    Condition cond) {
  orr(dst, src1, Operand(src2), s, cond);
}

void Assembler::mov(Register dst, const Operand& src, SBit s, Condition cond) {
  // Don't allow nop instructions in the form mov rn, rn to be generated using
  // the mov instruction. They must be generated using nop(int/NopMarkerTypes).
  DCHECK(!(src.IsRegister() && src.rm() == dst && s == LeaveCC && cond == al));
  AddrMode1(cond | MOV | s, dst, no_reg, src);
}

void Assembler::mov(Register dst, Register src, SBit s, Condition cond) {
  mov(dst, Operand(src), s, cond);
}

void Assembler::mov_label_offset(Register dst, Label* label) {
  if (label->is_bound()) {
    mov(dst, Operand(label->pos() +
                     (InstructionStream::kHeaderSize - kHeapObjectTag)));
  } else {
    // Emit the link to the label in the code stream followed by extra nop
    // instructions.
    // If the label is not linked, then start a new link chain by linking it to
    // itself, emitting pc_offset().
    int link = label->is_linked() ? label->pos() : pc_offset();
    label->link_to(pc_offset());

    // When the label is bound, these instructions will be patched with a
    // sequence of movw/movt or mov/orr/orr instructions. They will load the
    // destination register with the position of the label from the beginning
    // of the code.
    //
    // The link will be extracted from the first instruction and the destination
    // register from the second.
    //   For ARMv7:
    //      link
    //      mov dst, dst
    //   For ARMv6:
    //      link
    //      mov dst, dst
    //      mov dst, dst
    //
    // When the label gets bound: target_at extracts the link and target_at_put
    // patches the instructions.
    CHECK(is_uint24(link));
    BlockConstPoolScope block_const_pool(this);
    emit(link);
    nop(dst.code());
    if (!CpuFeatures::IsSupported(ARMv7)) {
      nop(dst.code());
    }
  }
}

void Assembler::movw(Register reg, uint32_t immediate, Condition cond) {
  DCHECK(IsEnabled(ARMv7));
  emit(cond | 0x30 * B20 | reg.code() * B12 | EncodeMovwImmediate(immediate));
}

void Assembler::movt(Register reg, uint32_t immediate, Condition cond) {
  DCHECK(IsEnabled(ARMv7));
  emit(cond | 0x34 * B20 | reg.code() * B12 | EncodeMovwImmediate(immediate));
}

void Assembler::bic(Register dst, Register src1, const Operand& src2, SBit s,
                    Condition cond) {
  AddrMode1(cond | BIC | s, dst, src1, src2);
}

void Assembler::mvn(Register dst, const Operand& src, SBit s, Condition cond) {
  AddrMode1(cond | MVN | s, dst, no_reg, src);
}

void Assembler::asr(Register dst, Register src1, const Operand& src2, SBit s,
                    Condition cond) {
  if (src2.IsRegister()) {
    mov(dst, Operand(src1, ASR, src2.rm()), s, cond);
  } else {
    mov(dst, Operand(src1, ASR, src2.immediate()), s, cond);
  }
}

void Assembler::lsl(Register dst, Register src1, const Operand& src2, SBit s,
                    Condition cond) {
  if (src2.IsRegister()) {
    mov(dst, Operand(src1, LSL, src2.rm()), s, cond);
  } else {
    mov(dst, Operand(src1, LSL, src2.immediate()), s, cond);
  }
}

void Assembler::lsr(Register dst, Register src1, const Operand& src2, SBit s,
                    Condition cond) {
  if (src2.IsRegister()) {
    mov(dst, Operand(src1, LSR, src2.rm()), s, cond);
  } else {
    mov(dst, Operand(src1, LSR, src2.immediate()), s, cond);
  }
}

// Multiply instructions.
void Assembler::mla(Register dst, Register src1, Register src2, Register srcA,
                    SBit s, Condition cond) {
  DCHECK(dst != pc && src1 != pc && src2 != pc && srcA != pc);
  emit(cond | A | s | dst.code() * B16 | srcA.code() * B12 | src2.code() * B8 |
       B7 | B4 | src1.code());
}

void Assembler::mls(Register dst, Register src1, Register src2, Register srcA,
                    Condition cond) {
  DCHECK(dst != pc && src1 != pc && src2 != pc && srcA != pc);
  DCHECK(IsEnabled(ARMv7));
  emit(cond | B22 | B21 | dst.code() * B16 | srcA.code() * B12 |
       src2.code() * B8 | B7 | B4 | src1.code());
}

void Assembler::sdiv(Register dst, Register src1, Register src2,
                     Condition cond) {
  DCHECK(dst != pc && src1 != pc && src2 != pc);
  DCHECK(IsEnabled(SUDIV));
  emit(cond | B26 | B25 | B24 | B20 | dst.code() * B16 | 0xF * B12 |
       src2.code() * B8 | B4 | src1.code());
}

void Assembler::udiv(Register dst, Register src1, Register src2,
                     Condition cond) {
  DCHECK(dst != pc && src1 != pc && src2 != pc);
  DCHECK(IsEnabled(SUDIV));
  emit(cond | B26 | B25 | B24 | B21 | B20 | dst.code() * B16 | 0xF * B12 |
       src2.code() * B8 | B4 | src1.code());
}

void Assembler::mul(Register dst, Register src1, Register src2, SBit s,
                    Condition cond) {
  DCHECK(dst != pc && src1 != pc && src2 != pc);
  // dst goes in bits 16-19 for this instruction!
  emit(cond | s | dst.code() * B16 | src2.code() * B8 | B7 | B4 | src1.code());
}

void Assembler::smmla(Register dst, Register src1, Register src2, Register srcA,
                      Condition cond) {
  DCHECK(dst != pc && src1 != pc && src2 != pc && srcA != pc);
  emit(cond | B26 | B25 | B24 | B22 | B20 | dst.code() * B16 |
       srcA.code() * B12 | src2.code() * B8 | B4 | src1.code());
}

void Assembler::smmul(Register dst, Register src1, Register src2,
                      Condition cond) {
  DCHECK(dst != pc && src1 != pc && src2 != pc);
  emit(cond | B26 | B25 | B24 | B22 | B20 | dst.code() * B16 | 0xF * B12 |
       src2.code() * B8 | B4 | src1.code());
}

void Assembler::smlal(Register dstL, Register dstH, Register src1,
                      Register src2, SBit s, Condition cond) {
  DCHECK(dstL != pc && dstH != pc && src1 != pc && src2 != pc);
  DCHECK(dstL != dstH);
  emit(cond | B23 | B22 | A | s | dstH.code() * B16 | dstL.code() * B12 |
       src2.code() * B8 | B7 | B4 | src1.code());
}

void Assembler::smull(Register dstL, Register dstH, Register src1,
                      Register src2, SBit s, Condition cond) {
  DCHECK(dstL != pc && dstH != pc && src1 != pc && src2 != pc);
  DCHECK(dstL != dstH);
  emit(cond | B23 | B22 | s | dstH.code() * B16 | dstL.code() * B12 |
       src2.code() * B8 | B7 | B4 | src1.code());
}

void Assembler::umlal(Register dstL, Register dstH, Register src1,
                      Register src2, SBit s, Condition cond) {
  DCHECK(dstL != pc && dstH != pc && src1 != pc && src2 != pc);
  DCHECK(dstL != dstH);
  emit(cond | B23 | A | s | dstH.code() * B16 | dstL.code() * B12 |
       src2.code() * B8 | B7 | B4 | src1.code());
}

void Assembler::umull(Register dstL, Register dstH, Register src1,
                      Register src2, SBit s, Condition cond) {
  DCHECK(dstL != pc && dstH != pc && src1 != pc && src2 != pc);
  DCHECK(dstL != dstH);
  emit(cond | B23 | s | dstH.code() * B16 | dstL.code() * B12 |
       src2.code() * B8 | B7 | B4 | src1.code());
}

// Miscellaneous arithmetic instructions.
void Assembler::clz(Register dst, Register src, Condition cond) {
  DCHECK(dst != pc && src != pc);
  emit(cond | B24 | B22 | B21 | 15 * B16 | dst.code() * B12 | 15 * B8 | CLZ |
       src.code());
}

// Saturating instructions.

// Unsigned saturate.
void Assembler::usat(Register dst, int satpos, const Operand& src,
                     Condition cond) {
  DCHECK(dst != pc && src.rm_ != pc);
  DCHECK((satpos >= 0) && (satpos <= 31));
  DCHECK(src.IsImmediateShiftedRegister());
  DCHECK((src.shift_op_ == ASR) || (src.shift_op_ == LSL));

  int sh = 0;
  if (src.shift_op_ == ASR) {
    sh = 1;
  }

  emit(cond | 0x6 * B24 | 0xE * B20 | satpos * B16 | dst.code() * B12 |
       src.shift_imm_ * B7 | sh * B6 | 0x1 * B4 | src.rm_.code());
}

// Bitfield manipulation instructions.

// Unsigned bit field extract.
// Extracts #width adjacent bits from position #lsb in a register, and
// writes them to the low bits of a destination register.
//   ubfx dst, src, #lsb, #width
void Assembler::ubfx(Register dst, Register src, int lsb, int width,
                     Condition cond) {
  DCHECK(IsEnabled(ARMv7));
  DCHECK(dst != pc && src != pc);
  DCHECK((lsb >= 0) && (lsb <= 31));
  DCHECK((width >= 1) && (width <= (32 - lsb)));
  emit(cond | 0xF * B23 | B22 | B21 | (width - 1) * B16 | dst.code() * B12 |
       lsb * B7 | B6 | B4 | src.code());
}

// Signed bit field extract.
// Extracts #width adjacent bits from position #lsb in a register, and
// writes them to the low bits of a destination register. The extracted
// value is sign extended to fill the destination register.
//   sbfx dst, src, #lsb, #width
void Assembler::sbfx(Register dst, Register src, int lsb, int width,
                     Condition cond) {
  DCHECK(IsEnabled(ARMv7));
  DCHECK(dst != pc && src != pc);
  DCHECK((lsb >= 0) && (lsb <= 31));
  DCHECK((width >= 1) && (width <= (32 - lsb)));
  emit(cond | 0xF * B23 | B21 | (width - 1) * B16 | dst.code() * B12 |
       lsb * B7 | B6 | B4 | src.code());
}

// Bit field clear.
// Sets #width adjacent bits at position #lsb in the destination register
// to zero, preserving the value of the other bits.
//   bfc dst, #lsb, #width
void Assembler::bfc(Register dst, int lsb, int width, Condition cond) {
  DCHECK(IsEnabled(ARMv7));
  DCHECK(dst != pc);
  DCHECK((lsb >= 0) && (lsb <= 31));
  DCHECK((width >= 1) && (width <= (32 - lsb)));
  int msb = lsb + width - 1;
  emit(cond | 0x1F * B22 | msb * B16 | dst.code() * B12 | lsb * B7 | B4 | 0xF);
}

// Bit field insert.
// Inserts #width adjacent bits from the low bits of the source register
// into position #lsb of the destination register.
//   bfi dst, src, #lsb, #width
void Assembler::bfi(Register dst, Register src, int lsb, int width,
                    Condition cond) {
  DCHECK(IsEnabled(ARMv7));
  DCHECK(dst != pc && src != pc);
  DCHECK((lsb >= 0) && (lsb <= 31));
  DCHECK((width >= 1) && (width <= (32 - lsb)));
  int msb = lsb + width - 1;
  emit(cond | 0x1F * B22 | msb * B16 | dst.code() * B12 | lsb * B7 | B4 |
       src.code());
}

void Assembler::pkhbt(Register dst, Register src1, const Operand& src2,
                      Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.125.
  // cond(31-28) | 01101000(27-20) | Rn(19-16) |
  // Rd(15-12) | imm5(11-7) | 0(6) | 01(5-4) | Rm(3-0)
  DCHECK(dst != pc);
  DCHECK(src1 != pc);
  DCHECK(src2.IsImmediateShiftedRegister());
  DCHECK(src2.rm() != pc);
  DCHECK((src2.shift_imm_ >= 0) && (src2.shift_imm_ <= 31));
  DCHECK(src2.shift_op() == LSL);
  emit(cond | 0x68 * B20 | src1.code() * B16 | dst.code() * B12 |
       src2.shift_imm_ * B7 | B4 | src2.rm().code());
}

void Assembler::pkhtb(Register dst, Register src1, const Operand& src2,
                      Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.125.
  // cond(31-28) | 01101000(27-20) | Rn(19-16) |
  // Rd(15-12) | imm5(11-7) | 1(6) | 01(5-4) | Rm(3-0)
  DCHECK(dst != pc);
  DCHECK(src1 != pc);
  DCHECK(src2.IsImmediateShiftedRegister());
  DCHECK(src2.rm() != pc);
  DCHECK((src2.shift_imm_ >= 1) && (src2.shift_imm_ <= 32));
  DCHECK(src2.shift_op() == ASR);
  int asr = (src2.shift_imm_ == 32) ? 0 : src2.shift_imm_;
  emit(cond | 0x68 * B20 | src1.code() * B16 | dst.code() * B12 | asr * B7 |
       B6 | B4 | src2.rm().code());
}

void Assembler::sxtb(Register dst, Register src, int rotate, Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.233.
  // cond(31-28) | 01101010(27-20) | 1111(19-16) |
  // Rd(15-12) | rotate(11-10) | 00(9-8)| 0111(7-4) | Rm(3-0)
  DCHECK(dst != pc);
  DCHECK(src != pc);
  DCHECK(rotate == 0 || rotate == 8 || rotate == 16 || rotate == 24);
  emit(cond | 0x6A * B20 | 0xF * B16 | dst.code() * B12 |
       ((rotate >> 1) & 0xC) * B8 | 7 * B4 | src.code());
}

void Assembler::sxtab(Register dst, Register src1, Register src2, int rotate,
                      Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.233.
  // cond(31-28) | 01101010(27-20) | Rn(19-16) |
  // Rd(15-12) | rotate(11-10) | 00(9-8)| 0111(7-4) | Rm(3-0)
  DCHECK(dst != pc);
  DCHECK(src1 != pc);
  DCHECK(src2 != pc);
  DCHECK(rotate == 0 || rotate == 8 || rotate == 16 || rotate == 24);
  emit(cond | 0x6A * B20 | src1.code() * B16 | dst.code() * B12 |
       ((rotate >> 1) & 0xC) * B8 | 7 * B4 | src2.code());
}

void Assembler::sxth(Register dst, Register src, int rotate, Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.235.
  // cond(31-28) | 01101011(27-20) | 1111(19-16) |
  // Rd(15-12) | rotate(11-10) | 00(9-8)| 0111(7-4) | Rm(3-0)
  DCHECK(dst != pc);
  DCHECK(src != pc);
  DCHECK(rotate == 0 || rotate == 8 || rotate == 16 || rotate == 24);
  emit(cond | 0x6B * B20 | 0xF * B16 | dst.code() * B12 |
       ((rotate >> 1) & 0xC) * B8 | 7 * B4 | src.code());
}

void Assembler::sxtah(Register dst, Register src1, Register src2, int rotate,
                      Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.235.
  // cond(31-28) | 01101011(27-20) | Rn(19-16) |
  // Rd(15-12) | rotate(11-10) | 00(9-8)| 0111(7-4) | Rm(3-0)
  DCHECK(dst != pc);
  DCHECK(src1 != pc);
  DCHECK(src2 != pc);
  DCHECK(rotate == 0 || rotate == 8 || rotate == 16 || rotate == 24);
  emit(cond | 0x6B * B20 | src1.code() * B16 | dst.code() * B12 |
       ((rotate >> 1) & 0xC) * B8 | 7 * B4 | src2.code());
}

void Assembler::uxtb(Register dst, Register src, int rotate, Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.274.
  // cond(31-28) | 01101110(27-20) | 1111(19-16) |
  // Rd(15-12) | rotate(11-10) | 00(9-8)| 0111(7-4) | Rm(3-0)
  DCHECK(dst != pc);
  DCHECK(src != pc);
  DCHECK(rotate == 0 || rotate == 8 || rotate == 16 || rotate == 24);
  emit(cond | 0x6E * B20 | 0xF * B16 | dst.code() * B12 |
       ((rotate >> 1) & 0xC) * B8 | 7 * B4 | src.code());
}

void Assembler::uxtab(Register dst, Register src1, Register src2, int rotate,
                      Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.271.
  // cond(31-28) | 01101110(27-20) | Rn(19-16) |
  // Rd(15-12) | rotate(11-10) | 00(9-8)| 0111(7-4) | Rm(3-0)
  DCHECK(dst != pc);
  DCHECK(src1 != pc);
  DCHECK(src2 != pc);
  DCHECK(rotate == 0 || rotate == 8 || rotate == 16 || rotate == 24);
  emit(cond | 0x6E * B20 | src1.code() * B16 | dst.code() * B12 |
       ((rotate >> 1) & 0xC) * B8 | 7 * B4 | src2.code());
}

void Assembler::uxtb16(Register dst, Register src, int rotate, Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.275.
  // cond(31-28) | 01101100(27-20) | 1111(19-16) |
  // Rd(15-12) | rotate(11-10) | 00(9-8)| 0111(7-4) | Rm(3-0)
  DCHECK(dst != pc);
  DCHECK(src != pc);
  DCHECK(rotate == 0 || rotate == 8 || rotate == 16 || rotate == 24);
  emit(cond | 0x6C * B20 | 0xF * B16 | dst.code() * B12 |
       ((rotate >> 1) & 0xC) * B8 | 7 * B4 | src.code());
}

void Assembler::uxth(Register dst, Register src, int rotate, Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.276.
  // cond(31-28) | 01101111(27-20) | 1111(19-16) |
  // Rd(15-12) | rotate(11-10) | 00(9-8)| 0111(7-4) | Rm(3-0)
  DCHECK(dst != pc);
  DCHECK(src != pc);
  DCHECK(rotate == 0 || rotate == 8 || rotate == 16 || rotate == 24);
  emit(cond | 0x6F * B20 | 0xF * B16 | dst.code() * B12 |
       ((rotate >> 1) & 0xC) * B8 | 7 * B4 | src.code());
}

void Assembler::uxtah(Register dst, Register src1, Register src2, int rotate,
                      Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.273.
  // cond(31-28) | 01101111(27-20) | Rn(19-16) |
  // Rd(15-12) | rotate(11-10) | 00(9-8)| 0111(7-4) | Rm(3-0)
  DCHECK(dst != pc);
  DCHECK(src1 != pc);
  DCHECK(src2 != pc);
  DCHECK(rotate == 0 || rotate == 8 || rotate == 16 || rotate == 24);
  emit(cond | 0x6F * B20 | src1.code() * B16 | dst.code() * B12 |
       ((rotate >> 1) & 0xC) * B8 | 7 * B4 | src2.code());
}

void Assembler::rbit(Register dst, Register src, Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.144.
  // cond(31-28) | 011011111111(27-16) | Rd(15-12) | 11110011(11-4) | Rm(3-0)
  DCHECK(IsEnabled(ARMv7));
  DCHECK(dst != pc);
  DCHECK(src != pc);
  emit(cond | 0x6FF * B16 | dst.code() * B12 | 0xF3 * B4 | src.code());
}

void Assembler::rev(Register dst, Register src, Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.144.
  // cond(31-28) | 011010111111(27-16) | Rd(15-12) | 11110011(11-4) | Rm(3-0)
  DCHECK(dst != pc);
  DCHECK(src != pc);
  emit(cond | 0x6BF * B16 | dst.code() * B12 | 0xF3 * B4 | src.code());
}

// Status register access instructions.
void Assembler::mrs(Register dst, SRegister s, Condition cond) {
  DCHECK(dst != pc);
  emit(cond | B24 | s | 15 * B16 | dst.code() * B12);
}

void Assembler::msr(SRegisterFieldMask fields, const Operand& src,
                    Condition cond) {
  DCHECK_NE(fields & 0x000F0000, 0);  // At least one field must be set.
  DCHECK(((fields & 0xFFF0FFFF) == CPSR) || ((fields & 0xFFF0FFFF) == SPSR));
  Instr instr;
  if (src.IsImmediate()) {
    // Immediate.
    uint32_t rotate_imm;
    uint32_t immed_8;
    if (src.MustOutputRelocInfo(this) ||
        !FitsShifter(src.immediate(), &rotate_imm, &immed_8, nullptr)) {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      // Immediate operand cannot be encoded, load it first to a scratch
      // register.
      Move32BitImmediate(scratch, src);
      msr(fields, Operand(scratch), cond);
      return;
    }
    instr = I | rotate_imm * B8 | immed_8;
  } else {
    DCHECK(src.IsRegister());  // Only rm is allowed.
    instr = src.rm_.code();
  }
  emit(cond | instr | B24 | B21 | fields | 15 * B12);
}

// Load/Store instructions.
void Assembler::ldr(Register dst, const MemOperand& src, Condition cond) {
  AddrMode2(cond | B26 | L, dst, src);
}

void Assembler::str(Register src, const MemOperand& dst, Condition cond) {
  AddrMode2(cond | B26, src, dst);
}

void Assembler::ldrb(Register dst, const MemOperand& src, Condition cond) {
  AddrMode2(cond | B26 | B | L, dst, src);
}

void Assembler::strb(Register src, const MemOperand& dst, Condition cond) {
  AddrMode2(cond | B26 | B, src, dst);
}

void Assembler::ldrh(Register dst, const MemOperand& src, Condition cond) {
  AddrMode3(cond | L | B7 | H | B4, dst, src);
}

void Assembler::strh(Register src, const MemOperand& dst, Condition cond) {
  AddrMode3(cond | B7 | H | B4, src, dst);
}

void Assembler::ldrsb(Register dst, const MemOperand& src, Condition cond) {
  AddrMode3(cond | L | B7 | S6 | B4, dst, src);
}

void Assembler::ldrsh(Register dst, const MemOperand& src, Condition cond) {
  AddrMode3(cond | L | B7 | S6 | H | B4, dst, src);
}

void Assembler::ldrd(Register dst1, Register dst2, const MemOperand& src,
                     Condition cond) {
  DCHECK(src.rm() == no_reg);
  DCHECK(dst1 != lr);  // r14.
  DCHECK_EQ(0, dst1.code() % 2);
  DCHECK_EQ(dst1.code() + 1, dst2.code());
  AddrMode3(cond | B7 | B6 | B4, dst1, src);
}

void Assembler::strd(Register src1, Register src2, const MemOperand& dst,
                     Condition cond) {
  DCHECK(dst.rm() == no_reg);
  DCHECK(src1 != lr);  // r14.
  DCHECK_EQ(0, src1.code() % 2);
  DCHECK_EQ(src1.code() + 1, src2.code());
  AddrMode3(cond | B7 | B6 | B5 | B4, src1, dst);
}

void Assembler::ldr_pcrel(Register dst, int imm12, Condition cond) {
  AddrMode am = Offset;
  if (imm12 < 0) {
    imm12 = -imm12;
    am = NegOffset;
  }
  DCHECK(is_uint12(imm12));
  emit(cond | B26 | am | L | pc.code() * B16 | dst.code() * B12 | imm12);
}

// Load/Store exclusive instructions.
void Assembler::ldrex(Register dst, Register src, Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.75.
  // cond(31-28) | 00011001(27-20) | Rn(19-16) | Rt(15-12) | 111110011111(11-0)
  DCHECK(dst != pc);
  DCHECK(src != pc);
  emit(cond | B24 | B23 | B20 | src.code() * B16 | dst.code() * B12 | 0xF9F);
}

void Assembler::strex(Register src1, Register src2, Register dst,
                      Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.212.
  // cond(31-28) | 00011000(27-20) | Rn(19-16) | Rd(15-12) | 11111001(11-4) |
  // Rt(3-0)
  DCHECK(dst != pc);
  DCHECK(src1 != pc);
  DCHECK(src2 != pc);
  DCHECK(src1 != dst);
  DCHECK(src1 != src2);
  emit(cond | B24 | B23 | dst.code() * B16 | src1.code() * B12 | 0xF9 * B4 |
       src2.code());
}

void Assembler::ldrexb(Register dst, Register src, Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.76.
  // cond(31-28) | 00011101(27-20) | Rn(19-16) | Rt(15-12) | 111110011111(11-0)
  DCHECK(dst != pc);
  DCHECK(src != pc);
  emit(cond | B24 | B23 | B22 | B20 | src.code() * B16 | dst.code() * B12 |
       0xF9F);
}

void Assembler::strexb(Register src1, Register src2, Register dst,
                       Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.213.
  // cond(31-28) | 00011100(27-20) | Rn(19-16) | Rd(15-12) | 11111001(11-4) |
  // Rt(3-0)
  DCHECK(dst != pc);
  DCHECK(src1 != pc);
  DCHECK(src2 != pc);
  DCHECK(src1 != dst);
  DCHECK(src1 != src2);
  emit(cond | B24 | B23 | B22 | dst.code() * B16 | src1.code() * B12 |
       0xF9 * B4 | src2.code());
}

void Assembler::ldrexh(Register dst, Register src, Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.78.
  // cond(31-28) | 00011111(27-20) | Rn(19-16) | Rt(15-12) | 111110011111(11-0)
  DCHECK(dst != pc);
  DCHECK(src != pc);
  emit(cond | B24 | B23 | B22 | B21 | B20 | src.code() * B16 |
       dst.code() * B12 | 0xF9F);
}

void Assembler::strexh(Register src1, Register src2, Register dst,
                       Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.215.
  // cond(31-28) | 00011110(27-20) | Rn(19-16) | Rd(15-12) | 11111001(11-4) |
  // Rt(3-0)
  DCHECK(dst != pc);
  DCHECK(src1 != pc);
  DCHECK(src2 != pc);
  DCHECK(src1 != dst);
  DCHECK(src1 != src2);
  emit(cond | B24 | B23 | B22 | B21 | dst.code() * B16 | src1.code() * B12 |
       0xF9 * B4 | src2.code());
}

void Assembler::ldrexd(Register dst1, Register dst2, Register src,
                       Condition cond) {
  // cond(31-28) | 00011011(27-20) | Rn(19-16) | Rt(15-12) | 111110011111(11-0)
  DCHECK(dst1 != lr);  // r14.
  // The pair of destination registers is restricted to being an even-numbered
  // register and the odd-numbered register that immediately follows it.
  DCHECK_EQ(0, dst1.code() % 2);
  DCHECK_EQ(dst1.code() + 1, dst2.code());
  emit(cond | B24 | B23 | B21 | B20 | src.code() * B16 | dst1.code() * B12 |
       0xF9F);
}

void Assembler::strexd(Register res, Register src1, Register src2, Register dst,
                       Condition cond) {
  // cond(31-28) | 00011010(27-20) | Rn(19-16) | Rt(15-12) | 111110011111(11-0)
  DCHECK(src1 != lr);  // r14.
  // The pair of source registers is restricted to being an even-numbered
  // register and the odd-numbered register that immediately follows it.
  DCHECK_EQ(0, src1.code() % 2);
  DCHECK_EQ(src1.code() + 1, src2.code());
  emit(cond | B24 | B23 | B21 | dst.code() * B16 | res.code() * B12 |
       0xF9 * B4 | src1.code());
}

// Preload instructions.
void Assembler::pld(const MemOperand& address) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.128.
  // 1111(31-28) | 0111(27-24) | U(23) | R(22) | 01(21-20) | Rn(19-16) |
  // 1111(15-12) | imm5(11-07) | type(6-5) | 0(4)| Rm(3-0) |
  DCHECK(address.rm() == no_reg);
  DCHECK(address.am() == Offset);
  int U = B23;
  int offset = address.offset();
  if (offset < 0) {
    offset = -offset;
    U = 0;
  }
  DCHECK_LT(offset, 4096);
  emit(kSpecialCondition | B26 | B24 | U | B22 | B20 |
       address.rn().code() * B16 | 0xF * B12 | offset);
}

// Load/Store multiple instructions.
void Assembler::ldm(BlockAddrMode am, Register base, RegList dst,
                    Condition cond) {
  // ABI stack constraint: ldmxx base, {..sp..}  base != sp  is not restartable.
  DCHECK(base == sp || !dst.has(sp));

  AddrMode4(cond | B27 | am | L, base, dst);

  // Emit the constant pool after a function return implemented by ldm ..{..pc}.
  if (cond == al && dst.has(pc)) {
    // There is a slight chance that the ldm instruction was actually a call,
    // in which case it would be wrong to return into the constant pool; we
    // recognize this case by checking if the emission of the pool was blocked
    // at the pc of the ldm instruction by a mov lr, pc instruction; if this is
    // the case, we emit a jump over the pool.
    CheckConstPool(true, no_const_pool_before_ == pc_offset() - kInstrSize);
  }
}

void Assembler::stm(BlockAddrMode am, Register base, RegList src,
                    Condition cond) {
  AddrMode4(cond | B27 | am, base, src);
}

// Exception-generating instructions and debugging support.
// Stops with a non-negative code less than kNumOfWatchedStops support
// enabling/disabling and a counter feature. See simulator-arm.h .
void Assembler::stop(Condition cond, int32_t code) {
#ifndef __arm__
  DCHECK_GE(code, kDefaultStopCode);
  {
    BlockConstPoolScope block_const_pool(this);
    if (code >= 0) {
      svc(kStopCode + code, cond);
    } else {
      svc(kStopCode + kMaxStopCode, cond);
    }
  }
#else   // def __arm__
  if (cond != al) {
    Label skip;
    b(&skip, NegateCondition(cond));
    bkpt(0);
    bind(&skip);
  } else {
    bkpt(0);
  }
#endif  // def __arm__
}

void Assembler::bkpt(uint32_t imm16) {
  DCHECK(is_uint16(imm16));
  emit(al | B24 | B21 | (imm16 >> 4) * B8 | BKPT | (imm16 & 0xF));
}

void Assembler::svc(uint32_t imm24, Condition cond) {
  CHECK(is_uint24(imm24));
  emit(cond | 15 * B24 | imm24);
}

void Assembler::dmb(BarrierOption option) {
  if (CpuFeatures::IsSupported(ARMv7)) {
    // Details available in ARM DDI 0406C.b, A8-378.
    emit(kSpecialCondition | 0x57FF * B12 | 5 * B4 | option);
  } else {
    // Details available in ARM DDI 0406C.b, B3-1750.
    // CP15DMB: CRn=c7, opc1=0, CRm=c10, opc2=5, Rt is ignored.
    mcr(p15, 0, r0, cr7, cr10, 5);
  }
}

void Assembler::dsb(BarrierOption option) {
  if (CpuFeatures::IsSupported(ARMv7)) {
    // Details available in ARM DDI 0406C.b, A8-380.
    emit(kSpecialCondition | 0x57FF * B12 | 4 * B4 | option);
  } else {
    // Details available in ARM DDI 0406C.b, B3-1750.
    // CP15DSB: CRn=c7, opc1=0, CRm=c10, opc2=4, Rt is ignored.
    mcr(p15, 0, r0, cr7, cr10, 4);
  }
}

void Assembler::isb(BarrierOption option) {
  if (CpuFeatures::IsSupported(ARMv7)) {
    // Details available in ARM DDI 0406C.b, A8-389.
    emit(kSpecialCondition | 0x57FF * B12 | 6 * B4 | option);
  } else {
    // Details available in ARM DDI 0406C.b, B3-1750.
    // CP15ISB: CRn=c7, opc1=0, CRm=c5, opc2=4, Rt is ignored.
    mcr(p15, 0, r0, cr7, cr5, 4);
  }
}

void Assembler::csdb() {
  // Details available in Arm Cache Speculation Side-channels white paper,
  // version 1.1, page 4.
  emit(0xE320F014);
}

// Coprocessor instructions.
void Assembler::cdp(Coprocessor coproc, int opcode_1, CRegister crd,
                    CRegister crn, CRegister crm, int opcode_2,
                    Condition cond) {
  DCHECK(is_uint4(opcode_1) && is_uint3(opcode_2));
  emit(cond | B27 | B26 | B25 | (opcode_1 & 15) * B20 | crn.code() * B16 |
       crd.code() * B12 | coproc * B8 | (opcode_2 & 7) * B5 | crm.code());
}

void Assembler::cdp2(Coprocessor coproc, int opcode_1, CRegister crd,
                     CRegister crn, CRegister crm, int opcode_2) {
  cdp(coproc, opcode_1, crd, crn, crm, opcode_2, kSpecialCondition);
}

void Assembler::mcr(Coprocessor coproc, int opcode_1, Register rd,
                    CRegister crn, CRegister crm, int opcode_2,
                    Condition cond) {
  DCHECK(is_uint3(opcode_1) && is_uint3(opcode_2));
  emit(cond | B27 | B26 | B25 | (opcode_1 & 7) * B21 | crn.code() * B16 |
       rd.code() * B12 | coproc * B8 | (opcode_2 & 7) * B5 | B4 | crm.code());
}

void Assembler::mcr2(Coprocessor coproc, int opcode_1, Register rd,
                     CRegister crn, CRegister crm, int opcode_2) {
  mcr(coproc, opcode_1, rd, crn, crm, opcode_2, kSpecialCondition);
}

void Assembler::mrc(Coprocessor coproc, int opcode_1, Register rd,
                    CRegister crn, CRegister crm, int opcode_2,
                    Condition cond) {
  DCHECK(is_uint3(opcode_1) && is_uint3(opcode_2));
  emit(cond | B27 | B26 | B25 | (opcode_1 & 7) * B21 | L | crn.code() * B16 |
       rd.code() * B12 | coproc * B8 | (opcode_2 & 7) * B5 | B4 | crm.code());
}

void Assembler::mrc2(Coprocessor coproc, int opcode_1, Register rd,
                     CRegister crn, CRegister crm, int opcode_2) {
  mrc(coproc, opcode_1, rd, crn, crm, opcode_2, kSpecialCondition);
}

void Assembler::ldc(Coprocessor coproc, CRegister crd, const MemOperand& src,
                    LFlag l, Condition cond) {
  AddrMode5(cond | B27 | B26 | l | L | coproc * B8, crd, src);
}

void Assembler::ldc(Coprocessor coproc, CRegister crd, Register rn, int option,
                    LFlag l, Condition cond) {
  // Unindexed addressing.
  DCHECK(is_uint8(option));
  emit(cond | B27 | B26 | U | l | L | rn.code() * B16 | crd.code() * B12 |
       coproc * B8 | (option & 255));
}

void Assembler::ldc2(Coprocessor coproc, CRegister crd, const MemOperand& src,
                     LFlag l) {
  ldc(coproc, crd, src, l, kSpecialCondition);
}

void Assembler::ldc2(Coprocessor coproc, CRegister crd, Register rn, int option,
                     LFlag l) {
  ldc(coproc, crd, rn, option, l, kSpecialCondition);
}

// Support for VFP.

void Assembler::vldr(const DwVfpRegister dst, const Register base, int offset,
                     const Condition cond) {
  // Ddst = MEM(Rbase + offset).
  // Instruction details available in ARM DDI 0406C.b, A8-924.
  // cond(31-28) | 1101(27-24)| U(23) | D(22) | 01(21-20) | Rbase(19-16) |
  // Vd(15-12) | 1011(11-8) | offset
  DCHECK(VfpRegisterIsAvailable(dst));
  int u = 1;
  if (offset < 0) {
    CHECK_NE(offset, kMinInt);
    offset = -offset;
    u = 0;
  }
  int vd, d;
  dst.split_code(&vd, &d);

  DCHECK_GE(offset, 0);
  if ((offset % 4) == 0 && (offset / 4) < 256) {
    emit(cond | 0xD * B24 | u * B23 | d * B22 | B20 | base.code() * B16 |
         vd * B12 | 0xB * B8 | ((offset / 4) & 255));
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    // Larger offsets must be handled by computing the correct address in a
    // scratch register.
    DCHECK(base != scratch);
    if (u == 1) {
      add(scratch, base, Operand(offset));
    } else {
      sub(scratch, base, Operand(offset));
    }
    emit(cond | 0xD * B24 | d * B22 | B20 | scratch.code() * B16 | vd * B12 |
         0xB * B8);
  }
}

void Assembler::vldr(const DwVfpRegister dst, const MemOperand& operand,
                     const Condition cond) {
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(operand.am_ == Offset);
  if (operand.rm().is_valid()) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    add(scratch, operand.rn(),
        Operand(operand.rm(), operand.shift_op_, operand.shift_imm_));
    vldr(dst, scratch, 0, cond);
  } else {
    vldr(dst, operand.rn(), operand.offset(), cond);
  }
}

void Assembler::vldr(const SwVfpRegister dst, const Register base, int offset,
                     const Condition cond) {
  // Sdst = MEM(Rbase + offset).
  // Instruction details available in ARM DDI 0406A, A8-628.
  // cond(31-28) | 1101(27-24)| U001(23-20) | Rbase(19-16) |
  // Vdst(15-12) | 1010(11-8) | offset
  int u = 1;
  if (offset < 0) {
    offset = -offset;
    u = 0;
  }
  int sd, d;
  dst.split_code(&sd, &d);
  DCHECK_GE(offset, 0);

  if ((offset % 4) == 0 && (offset / 4) < 256) {
    emit(cond | u * B23 | d * B22 | 0xD1 * B20 | base.code() * B16 | sd * B12 |
         0xA * B8 | ((offset / 4) & 255));
  } else {
    // Larger offsets must be handled by computing the correct address in a
    // scratch register.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(base != scratch);
    if (u == 1) {
      add(scratch, base, Operand(offset));
    } else {
      sub(scratch, base, Operand(offset));
    }
    emit(cond | d * B22 | 0xD1 * B20 | scratch.code() * B16 | sd * B12 |
         0xA * B8);
  }
}

void Assembler::vldr(const SwVfpRegister dst, const MemOperand& operand,
                     const Condition cond) {
  DCHECK(operand.am_ == Offset);
  if (operand.rm().is_valid()) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    add(scratch, operand.rn(),
        Operand(operand.rm(), operand.shift_op_, operand.shift_imm_));
    vldr(dst, scratch, 0, cond);
  } else {
    vldr(dst, operand.rn(), operand.offset(), cond);
  }
}

void Assembler::vstr(const DwVfpRegister src, const Register base, int offset,
                     const Condition cond) {
  // MEM(Rbase + offset) = Dsrc.
  // Instruction details available in ARM DDI 0406C.b, A8-1082.
  // cond(31-28) | 1101(27-24)| U(23) | D(22) | 00(21-20) | Rbase(19-16) |
  // Vd(15-12) | 1011(11-8) | (offset/4)
  DCHECK(VfpRegisterIsAvailable(src));
  int u = 1;
  if (offset < 0) {
    CHECK_NE(offset, kMinInt);
    offset = -offset;
    u = 0;
  }
  DCHECK_GE(offset, 0);
  int vd, d;
  src.split_code(&vd, &d);

  if ((offset % 4) == 0 && (offset / 4) < 256) {
    emit(cond | 0xD * B24 | u * B23 | d * B22 | base.code() * B16 | vd * B12 |
         0xB * B8 | ((offset / 4) & 255));
  } else {
    // Larger offsets must be handled by computing the correct address in the a
    // scratch register.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(base != scratch);
    if (u == 1) {
      add(scratch, base, Operand(offset));
    } else {
      sub(scratch, base, Operand(offset));
    }
    emit(cond | 0xD * B24 | d * B22 | scratch.code() * B16 | vd * B12 |
         0xB * B8);
  }
}

void Assembler::vstr(const DwVfpRegister src, const MemOperand& operand,
                     const Condition cond) {
  DCHECK(VfpRegisterIsAvailable(src));
  DCHECK(operand.am_ == Offset);
  if (operand.rm().is_valid()) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    add(scratch, operand.rn(),
        Operand(operand.rm(), operand.shift_op_, operand.shift_imm_));
    vstr(src, scratch, 0, cond);
  } else {
    vstr(src, operand.rn(), operand.offset(), cond);
  }
}

void Assembler::vstr(const SwVfpRegister src, const Register base, int offset,
                     const Condition cond) {
  // MEM(Rbase + offset) = SSrc.
  // Instruction details available in ARM DDI 0406A, A8-786.
  // cond(31-28) | 1101(27-24)| U000(23-20) | Rbase(19-16) |
  // Vdst(15-12) | 1010(11-8) | (offset/4)
  int u = 1;
  if (offset < 0) {
    CHECK_NE(offset, kMinInt);
    offset = -offset;
    u = 0;
  }
  int sd, d;
  src.split_code(&sd, &d);
  DCHECK_GE(offset, 0);
  if ((offset % 4) == 0 && (offset / 4) < 256) {
    emit(cond | u * B23 | d * B22 | 0xD0 * B20 | base.code() * B16 | sd * B12 |
         0xA * B8 | ((offset / 4) & 255));
  } else {
    // Larger offsets must be handled by computing the correct address in a
    // scratch register.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(base != scratch);
    if (u == 1) {
      add(scratch, base, Operand(offset));
    } else {
      sub(scratch, base, Operand(offset));
    }
    emit(cond | d * B22 | 0xD0 * B20 | scratch.code() * B16 | sd * B12 |
         0xA * B8);
  }
}

void Assembler::vstr(const SwVfpRegister src, const MemOperand& operand,
                     const Condition cond) {
  DCHECK(operand.am_ == Offset);
  if (operand.rm().is_valid()) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    add(scratch, operand.rn(),
        Operand(operand.rm(), operand.shift_op_, operand.shift_imm_));
    vstr(src, scratch, 0, cond);
  } else {
    vstr(src, operand.rn(), operand.offset(), cond);
  }
}

void Assembler::vldm(BlockAddrMode am, Register base, DwVfpRegister first,
                     DwVfpRegister last, Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8-922.
  // cond(31-28) | 110(27-25)| PUDW1(24-20) | Rbase(19-16) |
  // first(15-12) | 1011(11-8) | (count * 2)
  DCHECK_LE(first.code(), last.code());
  DCHECK(VfpRegisterIsAvailable(last));
  DCHECK(am == ia || am == ia_w || am == db_w);
  DCHECK(base != pc);

  int sd, d;
  first.split_code(&sd, &d);
  int count = last.code() - first.code() + 1;
  DCHECK_LE(count, 16);
  emit(cond | B27 | B26 | am | d * B22 | B20 | base.code() * B16 | sd * B12 |
       0xB * B8 | count * 2);
}

void Assembler::vstm(BlockAddrMode am, Register base, DwVfpRegister first,
                     DwVfpRegister last, Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8-1080.
  // cond(31-28) | 110(27-25)| PUDW0(24-20) | Rbase(19-16) |
  // first(15-12) | 1011(11-8) | (count * 2)
  DCHECK_LE(first.code(), last.code());
  DCHECK(VfpRegisterIsAvailable(last));
  DCHECK(am == ia || am == ia_w || am == db_w);
  DCHECK(base != pc);

  int sd, d;
  first.split_code(&sd, &d);
  int count = last.code() - first.code() + 1;
  DCHECK_LE(count, 16);
  emit(cond | B27 | B26 | am | d * B22 | base.code() * B16 | sd * B12 |
       0xB * B8 | count * 2);
}

void Assembler::vldm(BlockAddrMode am, Register base, SwVfpRegister first,
                     SwVfpRegister last, Condition cond) {
  // Instruction details available in ARM DDI 0406A, A8-626.
  // cond(31-28) | 110(27-25)| PUDW1(24-20) | Rbase(19-16) |
  // first(15-12) | 1010(11-8) | (count/2)
  DCHECK_LE(first.code(), last.code());
  DCHECK(am == ia || am == ia_w || am == db_w);
  DCHECK(base != pc);

  int sd, d;
  first.split_code(&sd, &d);
  int count = last.code() - first.code() + 1;
  emit(cond | B27 | B26 | am | d * B22 | B20 | base.code() * B16 | sd * B12 |
       0xA * B8 | count);
}

void Assembler::vstm(BlockAddrMode am, Register base, SwVfpRegister first,
                     SwVfpRegister last, Condition cond) {
  // Instruction details available in ARM DDI 0406A, A8-784.
  // cond(31-28) | 110(27-25)| PUDW0(24-20) | Rbase(19-16) |
  // first(15-12) | 1011(11-8) | (count/2)
  DCHECK_LE(first.code(), last.code());
  DCHECK(am == ia || am == ia_w || am == db_w);
  DCHECK(base != pc);

  int sd, d;
  first.split_code(&sd, &d);
  int count = last.code() - first.code() + 1;
  emit(cond | B27 | B26 | am | d * B22 | base.code() * B16 | sd * B12 |
       0xA * B8 | count);
}

static void DoubleAsTwoUInt32(base::Double d, uint32_t* lo, uint32_t* hi) {
  uint64_t i = d.AsUint64();

  *lo = i & 0xFFFFFFFF;
  *hi = i >> 32;
}

static void WriteVmovIntImmEncoding(uint8_t imm, uint32_t* encoding) {
  // Integer promotion from uint8_t to int makes these all okay.
  *encoding = ((imm & 0x80) << (24 - 7));   // a
  *encoding |= ((imm & 0x70) << (16 - 4));  // bcd
  *encoding |= (imm & 0x0f);                //  efgh
}

// This checks if imm can be encoded into an immediate for vmov.
// See Table A7-15 in ARM DDI 0406C.d.
// Currently only supports the first row and op=0 && cmode=1110.
static bool FitsVmovIntImm(uint64_t imm, uint32_t* encoding, uint8_t* cmode) {
  uint32_t lo = imm & 0xFFFFFFFF;
  uint32_t hi = imm >> 32;
  if ((lo == hi && ((lo & 0xffffff00) == 0))) {
    WriteVmovIntImmEncoding(imm & 0xff, encoding);
    *cmode = 0;
    return true;
  } else if ((lo == hi) && ((lo & 0xffff) == (lo >> 16)) &&
             ((lo & 0xff) == (lo >> 24))) {
    // Check that all bytes in imm are the same.
    WriteVmovIntImmEncoding(imm & 0xff, encoding);
    *cmode = 0xe;
    return true;
  }

  return false;
}

void Assembler::vmov(const DwVfpRegister dst, uint64_t imm) {
  uint32_t enc;
  uint8_t cmode;
  uint8_t op = 0;
  if (CpuFeatures::IsSupported(NEON) && FitsVmovIntImm(imm, &enc, &cmode)) {
    CpuFeatureScope scope(this, NEON);
    // Instruction details available in ARM DDI 0406C.b, A8-937.
    // 001i1(27-23) | D(22) | 000(21-19) | imm3(18-16) | Vd(15-12) | cmode(11-8)
    // | 0(7) | 0(6) | op(5) | 4(1) | imm4(3-0)
    int vd, d;
    dst.split_code(&vd, &d);
    emit(kSpecialCondition | 0x05 * B23 | d * B22 | vd * B12 | cmode * B8 |
         op * B5 | 0x1 * B4 | enc);
  } else {
    UNIMPLEMENTED();
  }
}

void Assembler::vmov(const QwNeonRegister dst, uint64_t imm) {
  uint32_t enc;
  uint8_t cmode;
  uint8_t op = 0;
  if (CpuFeatures::IsSupported(NEON) && FitsVmovIntImm(imm, &enc, &cmode)) {
    CpuFeatureScope scope(this, NEON);
    // Instruction details available in ARM DDI 0406C.b, A8-937.
    // 001i1(27-23) | D(22) | 000(21-19) | imm3(18-16) | Vd(15-12) | cmode(11-8)
    // | 0(7) | Q(6) | op(5) | 4(1) | imm4(3-0)
    int vd, d;
    dst.split_code(&vd, &d);
    emit(kSpecialCondition | 0x05 * B23 | d * B22 | vd * B12 | cmode * B8 |
         0x1 * B6 | op * B5 | 0x1 * B4 | enc);
  } else {
    UNIMPLEMENTED();
  }
}

// Only works for little endian floating point formats.
// We don't support VFP on the mixed endian floating point platform.
static bool FitsVmovFPImmediate(base::Double d, uint32_t* encoding) {
  // VMOV can accept an immediate of the form:
  //
  //  +/- m * 2^(-n) where 16 <= m <= 31 and 0 <= n <= 7
  //
  // The immediate is encoded using an 8-bit quantity, comprised of two
  // 4-bit fields. For an 8-bit immediate of the form:
  //
  //  [abcdefgh]
  //
  // where a is the MSB and h is the LSB, an immediate 64-bit double can be
  // created of the form:
  //
  //  [aBbbbbbb,bbcdefgh,00000000,00000000,
  //      00000000,00000000,00000000,00000000]
  //
  // where B = ~b.
  //

  uint32_t lo, hi;
  DoubleAsTwoUInt32(d, &lo, &hi);

  // The most obvious constraint is the long block of zeroes.
  if ((lo != 0) || ((hi & 0xFFFF) != 0)) {
    return false;
  }

  // Bits 61:54 must be all clear or all set.
  if (((hi & 0x3FC00000) != 0) && ((hi & 0x3FC00000) != 0x3FC00000)) {
    return false;
  }

  // Bit 62 must be NOT bit 61.
  if (((hi ^ (hi << 1)) & (0x40000000)) == 0) {
    return false;
  }

  // Create the encoded immediate in the form:
  //  [00000000,0000abcd,00000000,0000efgh]
  *encoding = (hi >> 16) & 0xF;       // Low nybble.
  *encoding |= (hi >> 4) & 0x70000;   // Low three bits of the high nybble.
  *encoding |= (hi >> 12) & 0x80000;  // Top bit of the high nybble.

  return true;
}

void Assembler::vmov(const SwVfpRegister dst, Float32 imm) {
  uint32_t enc;
  if (CpuFeatures::IsSupported(VFPv3) &&
      FitsVmovFPImmediate(base::Double(imm.get_scalar()), &enc)) {
    CpuFeatureScope scope(this, VFPv3);
    // The float can be encoded in the instruction.
    //
    // Sd = immediate
    // Instruction details available in ARM DDI 0406C.b, A8-936.
    // cond(31-28) | 11101(27-23) | D(22) | 11(21-20) | imm4H(19-16) |
    // Vd(15-12) | 101(11-9) | sz=0(8) | imm4L(3-0)
    int vd, d;
    dst.split_code(&vd, &d);
    emit(al | 0x1D * B23 | d * B22 | 0x3 * B20 | vd * B12 | 0x5 * B9 | enc);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    mov(scratch, Operand(imm.get_bits()));
    vmov(dst, scratch);
  }
}

void Assembler::vmov(const DwVfpRegister dst, base::Double imm,
                     const Register extra_scratch) {
  DCHECK(VfpRegisterIsAvailable(dst));
  uint32_t enc;
  if (CpuFeatures::IsSupported(VFPv3) && FitsVmovFPImmediate(imm, &enc)) {
    CpuFeatureScope scope(this, VFPv3);
    // The double can be encoded in the instruction.
    //
    // Dd = immediate
    // Instruction details available in ARM DDI 0406C.b, A8-936.
    // cond(31-28) | 11101(27-23) | D(22) | 11(21-20) | imm4H(19-16) |
    // Vd(15-12) | 101(11-9) | sz=1(8) | imm4L(3-0)
    int vd, d;
    dst.split_code(&vd, &d);
    emit(al | 0x1D * B23 | d * B22 | 0x3 * B20 | vd * B12 | 0x5 * B9 | B8 |
         enc);
  } else {
    // Synthesise the double from ARM immediates.
    uint32_t lo, hi;
    DoubleAsTwoUInt32(imm, &lo, &hi);
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();

    if (lo == hi) {
      // Move the low and high parts of the double to a D register in one
      // instruction.
      mov(scratch, Operand(lo));
      vmov(dst, scratch, scratch);
    } else if (extra_scratch == no_reg) {
      // We only have one spare scratch register.
      mov(scratch, Operand(lo));
      vmov(NeonS32, dst, 0, scratch);
      if (((lo & 0xFFFF) == (hi & 0xFFFF)) && CpuFeatures::IsSupported(ARMv7)) {
        CpuFeatureScope scope(this, ARMv7);
        movt(scratch, hi >> 16);
      } else {
        mov(scratch, Operand(hi));
      }
      vmov(NeonS32, dst, 1, scratch);
    } else {
      // Move the low and high parts of the double to a D register in one
      // instruction.
      mov(scratch, Operand(lo));
      mov(extra_scratch, Operand(hi));
      vmov(dst, scratch, extra_scratch);
    }
  }
}

void Assembler::vmov(const SwVfpRegister dst, const SwVfpRegister src,
                     const Condition cond) {
  // Sd = Sm
  // Instruction details available in ARM DDI 0406B, A8-642.
  int sd, d, sm, m;
  dst.split_code(&sd, &d);
  src.split_code(&sm, &m);
  emit(cond | 0xE * B24 | d * B22 | 0xB * B20 | sd * B12 | 0xA * B8 | B6 |
       m * B5 | sm);
}

void Assembler::vmov(const DwVfpRegister dst, const DwVfpRegister src,
                     const Condition cond) {
  // Dd = Dm
  // Instruction details available in ARM DDI 0406C.b, A8-938.
  // cond(31-28) | 11101(27-23) | D(22) | 11(21-20) | 0000(19-16) | Vd(15-12) |
  // 101(11-9) | sz=1(8) | 0(7) | 1(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(VfpRegisterIsAvailable(src));
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  emit(cond | 0x1D * B23 | d * B22 | 0x3 * B20 | vd * B12 | 0x5 * B9 | B8 | B6 |
       m * B5 | vm);
}

void Assembler::vmov(const DwVfpRegister dst, const Register src1,
                     const Register src2, const Condition cond) {
  // Dm = <Rt,Rt2>.
  // Instruction details available in ARM DDI 0406C.b, A8-948.
  // cond(31-28) | 1100(27-24)| 010(23-21) | op=0(20) | Rt2(19-16) |
  // Rt(15-12) | 1011(11-8) | 00(7-6) | M(5) | 1(4) | Vm
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(src1 != pc && src2 != pc);
  int vm, m;
  dst.split_code(&vm, &m);
  emit(cond | 0xC * B24 | B22 | src2.code() * B16 | src1.code() * B12 |
       0xB * B8 | m * B5 | B4 | vm);
}

void Assembler::vmov(const Register dst1, const Register dst2,
                     const DwVfpRegister src, const Condition cond) {
  // <Rt,Rt2> = Dm.
  // Instruction details available in ARM DDI 0406C.b, A8-948.
  // cond(31-28) | 1100(27-24)| 010(23-21) | op=1(20) | Rt2(19-16) |
  // Rt(15-12) | 1011(11-8) | 00(7-6) | M(5) | 1(4) | Vm
  DCHECK(VfpRegisterIsAvailable(src));
  DCHECK(dst1 != pc && dst2 != pc);
  int vm, m;
  src.split_code(&vm, &m);
  emit(cond | 0xC * B24 | B22 | B20 | dst2.code() * B16 | dst1.code() * B12 |
       0xB * B8 | m * B5 | B4 | vm);
}

void Assembler::vmov(const SwVfpRegister dst, const Register src,
                     const Condition cond) {
  // Sn = Rt.
  // Instruction details available in ARM DDI 0406A, A8-642.
  // cond(31-28) | 1110(27-24)| 000(23-21) | op=0(20) | Vn(19-16) |
  // Rt(15-12) | 1010(11-8) | N(7)=0 | 00(6-5) | 1(4) | 0000(3-0)
  DCHECK(src != pc);
  int sn, n;
  dst.split_code(&sn, &n);
  emit(cond | 0xE * B24 | sn * B16 | src.code() * B12 | 0xA * B8 | n * B7 | B4);
}

void Assembler::vmov(const Register dst, const SwVfpRegister src,
                     const Condition cond) {
  // Rt = Sn.
  // Instruction details available in ARM DDI 0406A, A8-642.
  // cond(31-28) | 1110(27-24)| 000(23-21) | op=1(20) | Vn(19-16) |
  // Rt(15-12) | 1010(11-8) | N(7)=0 | 00(6-5) | 1(4) | 0000(3-0)
  DCHECK(dst != pc);
  int sn, n;
  src.split_code(&sn, &n);
  emit(cond | 0xE * B24 | B20 | sn * B16 | dst.code() * B12 | 0xA * B8 |
       n * B7 | B4);
}

// Type of data to read from or write to VFP register.
// Used as specifier in generic vcvt instruction.
enum VFPType { S32, U32, F32, F64 };

static bool IsSignedVFPType(VFPType type) {
  switch (type) {
    case S32:
      return true;
    case U32:
      return false;
    default:
      UNREACHABLE();
  }
}

static bool IsIntegerVFPType(VFPType type) {
  switch (type) {
    case S32:
    case U32:
      return true;
    case F32:
    case F64:
      return false;
    default:
      UNREACHABLE();
  }
}

static bool IsDoubleVFPType(VFPType type) {
  switch (type) {
    case F32:
      return false;
    case F64:
      return true;
    default:
      UNREACHABLE();
  }
}

// Split five bit reg_code based on size of reg_type.
//  32-bit register codes are Vm:M
//  64-bit register codes are M:Vm
// where Vm is four bits, and M is a single bit.
static void SplitRegCode(VFPType reg_type, int reg_code, int* vm, int* m) {
  DCHECK((reg_code >= 0) && (reg_code <= 31));
  if (IsIntegerVFPType(reg_type) || !IsDoubleVFPType(reg_type)) {
    SwVfpRegister::split_code(reg_code, vm, m);
  } else {
    DwVfpRegister::split_code(reg_code, vm, m);
  }
}

// Encode vcvt.src_type.dst_type instruction.
static Instr EncodeVCVT(const VFPType dst_type, const int dst_code,
                        const VFPType src_type, const int src_code,
                        VFPConversionMode mode, const Condition cond) {
  DCHECK(src_type != dst_type);
  int D, Vd, M, Vm;
  SplitRegCode(src_type, src_code, &Vm, &M);
  SplitRegCode(dst_type, dst_code, &Vd, &D);

  if (IsIntegerVFPType(dst_type) || IsIntegerVFPType(src_type)) {
    // Conversion between IEEE floating point and 32-bit integer.
    // Instruction details available in ARM DDI 0406B, A8.6.295.
    // cond(31-28) | 11101(27-23)| D(22) | 11(21-20) | 1(19) | opc2(18-16) |
    // Vd(15-12) | 101(11-9) | sz(8) | op(7) | 1(6) | M(5) | 0(4) | Vm(3-0)
    DCHECK(!IsIntegerVFPType(dst_type) || !IsIntegerVFPType(src_type));

    int sz, opc2, op;

    if (IsIntegerVFPType(dst_type)) {
      opc2 = IsSignedVFPType(dst_type) ? 0x5 : 0x4;
      sz = IsDoubleVFPType(src_type) ? 0x1 : 0x0;
      op = mode;
    } else {
      DCHECK(IsIntegerVFPType(src_type));
      opc2 = 0x0;
      sz = IsDoubleVFPType(dst_type) ? 0x1 : 0x0;
      op = IsSignedVFPType(src_type) ? 0x1 : 0x0;
    }

    return (cond | 0xE * B24 | B23 | D * B22 | 0x3 * B20 | B19 | opc2 * B16 |
            Vd * B12 | 0x5 * B9 | sz * B8 | op * B7 | B6 | M * B5 | Vm);
  } else {
    // Conversion between IEEE double and single precision.
    // Instruction details available in ARM DDI 0406B, A8.6.298.
    // cond(31-28) | 11101(27-23)| D(22) | 11(21-20) | 0111(19-16) |
    // Vd(15-12) | 101(11-9) | sz(8) | 1(7) | 1(6) | M(5) | 0(4) | Vm(3-0)
    int sz = IsDoubleVFPType(src_type) ? 0x1 : 0x0;
    return (cond | 0xE * B24 | B23 | D * B22 | 0x3 * B20 | 0x7 * B16 |
            Vd * B12 | 0x5 * B9 | sz * B8 | B7 | B6 | M * B5 | Vm);
  }
}

void Assembler::vcvt_f64_s32(const DwVfpRegister dst, const SwVfpRegister src,
                             VFPConversionMode mode, const Condition cond) {
  DCHECK(VfpRegisterIsAvailable(dst));
  emit(EncodeVCVT(F64, dst.code(), S32, src.code(), mode, cond));
}

void Assembler::vcvt_f32_s32(const SwVfpRegister dst, const SwVfpRegister src,
                             VFPConversionMode mode, const Condition cond) {
  emit(EncodeVCVT(F32, dst.code(), S32, src.code(), mode, cond));
}

void Assembler::vcvt_f64_u32(const DwVfpRegister dst, const SwVfpRegister src,
                             VFPConversionMode mode, const Condition cond) {
  DCHECK(VfpRegisterIsAvailable(dst));
  emit(EncodeVCVT(F64, dst.code(), U32, src.code(), mode, cond));
}

void Assembler::vcvt_f32_u32(const SwVfpRegister dst, const SwVfpRegister src,
                             VFPConversionMode mode, const Condition cond) {
  emit(EncodeVCVT(F32, dst.code(), U32, src.code(), mode, cond));
}

void Assembler::vcvt_s32_f32(const SwVfpRegister dst, const SwVfpRegister src,
                             VFPConversionMode mode, const Condition cond) {
  emit(EncodeVCVT(S32, dst.code(), F32, src.code(), mode, cond));
}

void Assembler::vcvt_u32_f32(const SwVfpRegister dst, const SwVfpRegister src,
                             VFPConversionMode mode, const Condition cond) {
  emit(EncodeVCVT(U32, dst.code(), F32, src.code(), mode, cond));
}

void Assembler::vcvt_s32_f64(const SwVfpRegister dst, const DwVfpRegister src,
                             VFPConversionMode mode, const Condition cond) {
  DCHECK(VfpRegisterIsAvailable(src));
  emit(EncodeVCVT(S32, dst.code(), F64, src.code(), mode, cond));
}

void Assembler::vcvt_u32_f64(const SwVfpRegister dst, const DwVfpRegister src,
                             VFPConversionMode mode, const Condition cond) {
  DCHECK(VfpRegisterIsAvailable(src));
  emit(EncodeVCVT(U32, dst.code(), F64, src.code(), mode, cond));
}

void Assembler::vcvt_f64_f32(const DwVfpRegister dst, const SwVfpRegister src,
                             VFPConversionMode mode, const Condition cond) {
  DCHECK(VfpRegisterIsAvailable(dst));
  emit(EncodeVCVT(F64, dst.code(), F32, src.code(), mode, cond));
}

void Assembler::vcvt_f32_f64(const SwVfpRegister dst, const DwVfpRegister src,
                             VFPConversionMode mode, const Condition cond) {
  DCHECK(VfpRegisterIsAvailable(src));
  emit(EncodeVCVT(F32, dst.code(), F64, src.code(), mode, cond));
}

void Assembler::vcvt_f64_s32(const DwVfpRegister dst, int fraction_bits,
                             const Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8-874.
  // cond(31-28) | 11101(27-23) | D(22) | 11(21-20) | 1010(19-16) | Vd(15-12) |
  // 101(11-9) | sf=1(8) | sx=1(7) | 1(6) | i(5) | 0(4) | imm4(3-0)
  DCHECK(IsEnabled(VFPv3));
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(fraction_bits > 0 && fraction_bits <= 32);
  int vd, d;
  dst.split_code(&vd, &d);
  int imm5 = 32 - fraction_bits;
  int i = imm5 & 1;
  int imm4 = (imm5 >> 1) & 0xF;
  emit(cond | 0xE * B24 | B23 | d * B22 | 0x3 * B20 | B19 | 0x2 * B16 |
       vd * B12 | 0x5 * B9 | B8 | B7 | B6 | i * B5 | imm4);
}

void Assembler::vneg(const DwVfpRegister dst, const DwVfpRegister src,
                     const Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8-968.
  // cond(31-28) | 11101(27-23) | D(22) | 11(21-20) | 0001(19-16) | Vd(15-12) |
  // 101(11-9) | sz=1(8) | 0(7) | 1(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(VfpRegisterIsAvailable(src));
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);

  emit(cond | 0x1D * B23 | d * B22 | 0x3 * B20 | B16 | vd * B12 | 0x5 * B9 |
       B8 | B6 | m * B5 | vm);
}

void Assembler::vneg(const SwVfpRegister dst, const SwVfpRegister src,
                     const Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8-968.
  // cond(31-28) | 11101(27-23) | D(22) | 11(21-20) | 0001(19-16) | Vd(15-12) |
  // 101(11-9) | sz=0(8) | 0(7) | 1(6) | M(5) | 0(4) | Vm(3-0)
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);

  emit(cond | 0x1D * B23 | d * B22 | 0x3 * B20 | B16 | vd * B12 | 0x5 * B9 |
       B6 | m * B5 | vm);
}

void Assembler::vabs(const DwVfpRegister dst, const DwVfpRegister src,
                     const Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8-524.
  // cond(31-28) | 11101(27-23) | D(22) | 11(21-20) | 0000(19-16) | Vd(15-12) |
  // 101(11-9) | sz=1(8) | 1(7) | 1(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(VfpRegisterIsAvailable(src));
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  emit(cond | 0x1D * B23 | d * B22 | 0x3 * B20 | vd * B12 | 0x5 * B9 | B8 | B7 |
       B6 | m * B5 | vm);
}

void Assembler::vabs(const SwVfpRegister dst, const SwVfpRegister src,
                     const Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8-524.
  // cond(31-28) | 11101(27-23) | D(22) | 11(21-20) | 0000(19-16) | Vd(15-12) |
  // 101(11-9) | sz=0(8) | 1(7) | 1(6) | M(5) | 0(4) | Vm(3-0)
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  emit(cond | 0x1D * B23 | d * B22 | 0x3 * B20 | vd * B12 | 0x5 * B9 | B7 | B6 |
       m * B5 | vm);
}

void Assembler::vadd(const DwVfpRegister dst, const DwVfpRegister src1,
                     const DwVfpRegister src2, const Condition cond) {
  // Dd = vadd(Dn, Dm) double precision floating point addition.
  // Dd = D:Vd; Dm=M:Vm; Dn=N:Vm.
  // Instruction details available in ARM DDI 0406C.b, A8-830.
  // cond(31-28) | 11100(27-23)| D(22) | 11(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz=1(8) | N(7) | 0(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(VfpRegisterIsAvailable(src1));
  DCHECK(VfpRegisterIsAvailable(src2));
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  emit(cond | 0x1C * B23 | d * B22 | 0x3 * B20 | vn * B16 | vd * B12 |
       0x5 * B9 | B8 | n * B7 | m * B5 | vm);
}

void Assembler::vadd(const SwVfpRegister dst, const SwVfpRegister src1,
                     const SwVfpRegister src2, const Condition cond) {
  // Sd = vadd(Sn, Sm) single precision floating point addition.
  // Sd = D:Vd; Sm=M:Vm; Sn=N:Vm.
  // Instruction details available in ARM DDI 0406C.b, A8-830.
  // cond(31-28) | 11100(27-23)| D(22) | 11(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz=0(8) | N(7) | 0(6) | M(5) | 0(4) | Vm(3-0)
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  emit(cond | 0x1C * B23 | d * B22 | 0x3 * B20 | vn * B16 | vd * B12 |
       0x5 * B9 | n * B7 | m * B5 | vm);
}

void Assembler::vsub(const DwVfpRegister dst, const DwVfpRegister src1,
                     const DwVfpRegister src2, const Condition cond) {
  // Dd = vsub(Dn, Dm) double precision floating point subtraction.
  // Dd = D:Vd; Dm=M:Vm; Dn=N:Vm.
  // Instruction details available in ARM DDI 0406C.b, A8-1086.
  // cond(31-28) | 11100(27-23)| D(22) | 11(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz=1(8) | N(7) | 1(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(VfpRegisterIsAvailable(src1));
  DCHECK(VfpRegisterIsAvailable(src2));
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  emit(cond | 0x1C * B23 | d * B22 | 0x3 * B20 | vn * B16 | vd * B12 |
       0x5 * B9 | B8 | n * B7 | B6 | m * B5 | vm);
}

void Assembler::vsub(const SwVfpRegister dst, const SwVfpRegister src1,
                     const SwVfpRegister src2, const Condition cond) {
  // Sd = vsub(Sn, Sm) single precision floating point subtraction.
  // Sd = D:Vd; Sm=M:Vm; Sn=N:Vm.
  // Instruction details available in ARM DDI 0406C.b, A8-1086.
  // cond(31-28) | 11100(27-23)| D(22) | 11(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz=0(8) | N(7) | 1(6) | M(5) | 0(4) | Vm(3-0)
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  emit(cond | 0x1C * B23 | d * B22 | 0x3 * B20 | vn * B16 | vd * B12 |
       0x5 * B9 | n * B7 | B6 | m * B5 | vm);
}

void Assembler::vmul(const DwVfpRegister dst, const DwVfpRegister src1,
                     const DwVfpRegister src2, const Condition cond) {
  // Dd = vmul(Dn, Dm) double precision floating point multiplication.
  // Dd = D:Vd; Dm=M:Vm; Dn=N:Vm.
  // Instruction details available in ARM DDI 0406C.b, A8-960.
  // cond(31-28) | 11100(27-23)| D(22) | 10(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz=1(8) | N(7) | 0(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(VfpRegisterIsAvailable(src1));
  DCHECK(VfpRegisterIsAvailable(src2));
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  emit(cond | 0x1C * B23 | d * B22 | 0x2 * B20 | vn * B16 | vd * B12 |
       0x5 * B9 | B8 | n * B7 | m * B5 | vm);
}

void Assembler::vmul(const SwVfpRegister dst, const SwVfpRegister src1,
                     const SwVfpRegister src2, const Condition cond) {
  // Sd = vmul(Sn, Sm) single precision floating point multiplication.
  // Sd = D:Vd; Sm=M:Vm; Sn=N:Vm.
  // Instruction details available in ARM DDI 0406C.b, A8-960.
  // cond(31-28) | 11100(27-23)| D(22) | 10(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz=0(8) | N(7) | 0(6) | M(5) | 0(4) | Vm(3-0)
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  emit(cond | 0x1C * B23 | d * B22 | 0x2 * B20 | vn * B16 | vd * B12 |
       0x5 * B9 | n * B7 | m * B5 | vm);
}

void Assembler::vmla(const DwVfpRegister dst, const DwVfpRegister src1,
                     const DwVfpRegister src2, const Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8-932.
  // cond(31-28) | 11100(27-23) | D(22) | 00(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz=1(8) | N(7) | op=0(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(VfpRegisterIsAvailable(src1));
  DCHECK(VfpRegisterIsAvailable(src2));
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  emit(cond | 0x1C * B23 | d * B22 | vn * B16 | vd * B12 | 0x5 * B9 | B8 |
       n * B7 | m * B5 | vm);
}

void Assembler::vmla(const SwVfpRegister dst, const SwVfpRegister src1,
                     const SwVfpRegister src2, const Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8-932.
  // cond(31-28) | 11100(27-23) | D(22) | 00(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz=0(8) | N(7) | op=0(6) | M(5) | 0(4) | Vm(3-0)
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  emit(cond | 0x1C * B23 | d * B22 | vn * B16 | vd * B12 | 0x5 * B9 | n * B7 |
       m * B5 | vm);
}

void Assembler::vmls(const DwVfpRegister dst, const DwVfpRegister src1,
                     const DwVfpRegister src2, const Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8-932.
  // cond(31-28) | 11100(27-23) | D(22) | 00(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz=1(8) | N(7) | op=1(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(VfpRegisterIsAvailable(src1));
  DCHECK(VfpRegisterIsAvailable(src2));
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  emit(cond | 0x1C * B23 | d * B22 | vn * B16 | vd * B12 | 0x5 * B9 | B8 |
       n * B7 | B6 | m * B5 | vm);
}

void Assembler::vmls(const SwVfpRegister dst, const SwVfpRegister src1,
                     const SwVfpRegister src2, const Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8-932.
  // cond(31-28) | 11100(27-23) | D(22) | 00(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz=0(8) | N(7) | op=1(6) | M(5) | 0(4) | Vm(3-0)
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  emit(cond | 0x1C * B23 | d * B22 | vn * B16 | vd * B12 | 0x5 * B9 | n * B7 |
       B6 | m * B5 | vm);
}

void Assembler::vdiv(const DwVfpRegister dst, const DwVfpRegister src1,
                     const DwVfpRegister src2, const Condition cond) {
  // Dd = vdiv(Dn, Dm) double precision floating point division.
  // Dd = D:Vd; Dm=M:Vm; Dn=N:Vm.
  // Instruction details available in ARM DDI 0406C.b, A8-882.
  // cond(31-28) | 11101(27-23)| D(22) | 00(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz=1(8) | N(7) | 0(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(VfpRegisterIsAvailable(src1));
  DCHECK(VfpRegisterIsAvailable(src2));
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  emit(cond | 0x1D * B23 | d * B22 | vn * B16 | vd * B12 | 0x5 * B9 | B8 |
       n * B7 | m * B5 | vm);
}

void Assembler::vdiv(const SwVfpRegister dst, const SwVfpRegister src1,
                     const SwVfpRegister src2, const Condition cond) {
  // Sd = vdiv(Sn, Sm) single precision floating point division.
  // Sd = D:Vd; Sm=M:Vm; Sn=N:Vm.
  // Instruction details available in ARM DDI 0406C.b, A8-882.
  // cond(31-28) | 11101(27-23)| D(22) | 00(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz=0(8) | N(7) | 0(6) | M(5) | 0(4) | Vm(3-0)
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  emit(cond | 0x1D * B23 | d * B22 | vn * B16 | vd * B12 | 0x5 * B9 | n * B7 |
       m * B5 | vm);
}

void Assembler::vcmp(const DwVfpRegister src1, const DwVfpRegister src2,
                     const Condition cond) {
  // vcmp(Dd, Dm) double precision floating point comparison.
  // Instruction details available in ARM DDI 0406C.b, A8-864.
  // cond(31-28) | 11101(27-23)| D(22) | 11(21-20) | 0100(19-16) |
  // Vd(15-12) | 101(11-9) | sz=1(8) | E=0(7) | 1(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(VfpRegisterIsAvailable(src1));
  DCHECK(VfpRegisterIsAvailable(src2));
  int vd, d;
  src1.split_code(&vd, &d);
  int vm, m;
  src2.split_code(&vm, &m);
  emit(cond | 0x1D * B23 | d * B22 | 0x3 * B20 | 0x4 * B16 | vd * B12 |
       0x5 * B9 | B8 | B6 | m * B5 | vm);
}

void Assembler::vcmp(const SwVfpRegister src1, const SwVfpRegister src2,
                     const Condition cond) {
  // vcmp(Sd, Sm) single precision floating point comparison.
  // Instruction details available in ARM DDI 0406C.b, A8-864.
  // cond(31-28) | 11101(27-23)| D(22) | 11(21-20) | 0100(19-16) |
  // Vd(15-12) | 101(11-9) | sz=0(8) | E=0(7) | 1(6) | M(5) | 0(4) | Vm(3-0)
  int vd, d;
  src1.split_code(&vd, &d);
  int vm, m;
  src2.split_code(&vm, &m);
  emit(cond | 0x1D * B23 | d * B22 | 0x3 * B20 | 0x4 * B16 | vd * B12 |
       0x5 * B9 | B6 | m * B5 | vm);
}

void Assembler::vcmp(const DwVfpRegister src1, const double src2,
                     const Condition cond) {
  // vcmp(Dd, #0.0) double precision floating point comparison.
  // Instruction details available in ARM DDI 0406C.b, A8-864.
  // cond(31-28) | 11101(27-23)| D(22) | 11(21-20) | 0101(19-16) |
  // Vd(15-12) | 101(11-9) | sz=1(8) | E=0(7) | 1(6) | 0(5) | 0(4) | 0000(3-0)
  DCHECK(VfpRegisterIsAvailable(src1));
  DCHECK_EQ(src2, 0.0);
  int vd, d;
  src1.split_code(&vd, &d);
  emit(cond | 0x1D * B23 | d * B22 | 0x3 * B20 | 0x5 * B16 | vd * B12 |
       0x5 * B9 | B8 | B6);
}

void Assembler::vcmp(const SwVfpRegister src1, const float src2,
                     const Condition cond) {
  // vcmp(Sd, #0.0) single precision floating point comparison.
  // Instruction details available in ARM DDI 0406C.b, A8-864.
  // cond(31-28) | 11101(27-23)| D(22) | 11(21-20) | 0101(19-16) |
  // Vd(15-12) | 101(11-9) | sz=0(8) | E=0(7) | 1(6) | 0(5) | 0(4) | 0000(3-0)
  DCHECK_EQ(src2, 0.0);
  int vd, d;
  src1.split_code(&vd, &d);
  emit(cond | 0x1D * B23 | d * B22 | 0x3 * B20 | 0x5 * B16 | vd * B12 |
       0x5 * B9 | B6);
}

void Assembler::vmaxnm(const DwVfpRegister dst, const DwVfpRegister src1,
                       const DwVfpRegister src2) {
  // kSpecialCondition(31-28) | 11101(27-23) | D(22) | 00(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz=1(8) | N(7) | 0(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(IsEnabled(ARMv8));
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);

  emit(kSpecialCondition | 0x1D * B23 | d * B22 | vn * B16 | vd * B12 |
       0x5 * B9 | B8 | n * B7 | m * B5 | vm);
}

void Assembler::vmaxnm(const SwVfpRegister dst, const SwVfpRegister src1,
                       const SwVfpRegister src2) {
  // kSpecialCondition(31-28) | 11101(27-23) | D(22) | 00(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz=0(8) | N(7) | 0(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(IsEnabled(ARMv8));
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);

  emit(kSpecialCondition | 0x1D * B23 | d * B22 | vn * B16 | vd * B12 |
       0x5 * B9 | n * B7 | m * B5 | vm);
}

void Assembler::vminnm(const DwVfpRegister dst, const DwVfpRegister src1,
                       const DwVfpRegister src2) {
  // kSpecialCondition(31-28) | 11101(27-23) | D(22) | 00(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz=1(8) | N(7) | 1(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(IsEnabled(ARMv8));
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);

  emit(kSpecialCondition | 0x1D * B23 | d * B22 | vn * B16 | vd * B12 |
       0x5 * B9 | B8 | n * B7 | B6 | m * B5 | vm);
}

void Assembler::vminnm(const SwVfpRegister dst, const SwVfpRegister src1,
                       const SwVfpRegister src2) {
  // kSpecialCondition(31-28) | 11101(27-23) | D(22) | 00(21-20) | Vn(19-16) |
  // Vd(15-12) | 101(11-9) | sz=0(8) | N(7) | 1(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(IsEnabled(ARMv8));
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);

  emit(kSpecialCondition | 0x1D * B23 | d * B22 | vn * B16 | vd * B12 |
       0x5 * B9 | n * B7 | B6 | m * B5 | vm);
}

void Assembler::vsel(Condition cond, const DwVfpRegister dst,
                     const DwVfpRegister src1, const DwVfpRegister src2) {
  // cond=kSpecialCondition(31-28) | 11100(27-23) | D(22) |
  // vsel_cond=XX(21-20) | Vn(19-16) | Vd(15-12) | 101(11-9) | sz=1(8) | N(7) |
  // 0(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(IsEnabled(ARMv8));
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  int sz = 1;

  // VSEL has a special (restricted) condition encoding.
  //   eq(0b0000)... -> 0b00
  //   ge(0b1010)... -> 0b10
  //   gt(0b1100)... -> 0b11
  //   vs(0b0110)... -> 0b01
  // No other conditions are supported.
  int vsel_cond = (cond >> 30) & 0x3;
  if ((cond != eq) && (cond != ge) && (cond != gt) && (cond != vs)) {
    // We can implement some other conditions by swapping the inputs.
    DCHECK((cond == ne) | (cond == lt) | (cond == le) | (cond == vc));
    std::swap(vn, vm);
    std::swap(n, m);
  }

  emit(kSpecialCondition | 0x1C * B23 | d * B22 | vsel_cond * B20 | vn * B16 |
       vd * B12 | 0x5 * B9 | sz * B8 | n * B7 | m * B5 | vm);
}

void Assembler::vsel(Condition cond, const SwVfpRegister dst,
                     const SwVfpRegister src1, const SwVfpRegister src2) {
  // cond=kSpecialCondition(31-28) | 11100(27-23) | D(22) |
  // vsel_cond=XX(21-20) | Vn(19-16) | Vd(15-12) | 101(11-9) | sz=0(8) | N(7) |
  // 0(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(IsEnabled(ARMv8));
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  int sz = 0;

  // VSEL has a special (restricted) condition encoding.
  //   eq(0b0000)... -> 0b00
  //   ge(0b1010)... -> 0b10
  //   gt(0b1100)... -> 0b11
  //   vs(0b0110)... -> 0b01
  // No other conditions are supported.
  int vsel_cond = (cond >> 30) & 0x3;
  if ((cond != eq) && (cond != ge) && (cond != gt) && (cond != vs)) {
    // We can implement some other conditions by swapping the inputs.
    DCHECK((cond == ne) | (cond == lt) | (cond == le) | (cond == vc));
    std::swap(vn, vm);
    std::swap(n, m);
  }

  emit(kSpecialCondition | 0x1C * B23 | d * B22 | vsel_cond * B20 | vn * B16 |
       vd * B12 | 0x5 * B9 | sz * B8 | n * B7 | m * B5 | vm);
}

void Assembler::vsqrt(const DwVfpRegister dst, const DwVfpRegister src,
                      const Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8-1058.
  // cond(31-28) | 11101(27-23)| D(22) | 11(21-20) | 0001(19-16) |
  // Vd(15-12) | 101(11-9) | sz=1(8) | 11(7-6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(VfpRegisterIsAvailable(src));
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  emit(cond | 0x1D * B23 | d * B22 | 0x3 * B20 | B16 | vd * B12 | 0x5 * B9 |
       B8 | 0x3 * B6 | m * B5 | vm);
}

void Assembler::vsqrt(const SwVfpRegister dst, const SwVfpRegister src,
                      const Condition cond) {
  // Instruction details available in ARM DDI 0406C.b, A8-1058.
  // cond(31-28) | 11101(27-23)| D(22) | 11(21-20) | 0001(19-16) |
  // Vd(15-12) | 101(11-9) | sz=0(8) | 11(7-6) | M(5) | 0(4) | Vm(3-0)
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  emit(cond | 0x1D * B23 | d * B22 | 0x3 * B20 | B16 | vd * B12 | 0x5 * B9 |
       0x3 * B6 | m * B5 | vm);
}

void Assembler::vmsr(Register dst, Condition cond) {
  // Instruction details available in ARM DDI 0406A, A8-652.
  // cond(31-28) | 1110 (27-24) | 1110(23-20)| 0001 (19-16) |
  // Rt(15-12) | 1010 (11-8) | 0(7) | 00 (6-5) | 1(4) | 0000(3-0)
  emit(cond | 0xE * B24 | 0xE * B20 | B16 | dst.code() * B12 | 0xA * B8 | B4);
}

void Assembler::vmrs(Register dst, Condition cond) {
  // Instruction details available in ARM DDI 0406A, A8-652.
  // cond(31-28) | 1110 (27-24) | 1111(23-20)| 0001 (19-16) |
  // Rt(15-12) | 1010 (11-8) | 0(7) | 00 (6-5) | 1(4) | 0000(3-0)
  emit(cond | 0xE * B24 | 0xF * B20 | B16 | dst.code() * B12 | 0xA * B8 | B4);
}

void Assembler::vrinta(const SwVfpRegister dst, const SwVfpRegister src) {
  // cond=kSpecialCondition(31-28) | 11101(27-23)| D(22) | 11(21-20) |
  // 10(19-18) | RM=00(17-16) |  Vd(15-12) | 101(11-9) | sz=0(8) | 01(7-6) |
  // M(5) | 0(4) | Vm(3-0)
  DCHECK(IsEnabled(ARMv8));
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  emit(kSpecialCondition | 0x1D * B23 | d * B22 | 0x3 * B20 | B19 | vd * B12 |
       0x5 * B9 | B6 | m * B5 | vm);
}

void Assembler::vrinta(const DwVfpRegister dst, const DwVfpRegister src) {
  // cond=kSpecialCondition(31-28) | 11101(27-23)| D(22) | 11(21-20) |
  // 10(19-18) | RM=00(17-16) |  Vd(15-12) | 101(11-9) | sz=1(8) | 01(7-6) |
  // M(5) | 0(4) | Vm(3-0)
  DCHECK(IsEnabled(ARMv8));
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  emit(kSpecialCondition | 0x1D * B23 | d * B22 | 0x3 * B20 | B19 | vd * B12 |
       0x5 * B9 | B8 | B6 | m * B5 | vm);
}

void Assembler::vrintn(const SwVfpRegister dst, const SwVfpRegister src) {
  // cond=kSpecialCondition(31-28) | 11101(27-23)| D(22) | 11(21-20) |
  // 10(19-18) | RM=01(17-16) |  Vd(15-12) | 101(11-9) | sz=0(8) | 01(7-6) |
  // M(5) | 0(4) | Vm(3-0)
  DCHECK(IsEnabled(ARMv8));
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  emit(kSpecialCondition | 0x1D * B23 | d * B22 | 0x3 * B20 | B19 | 0x1 * B16 |
       vd * B12 | 0x5 * B9 | B6 | m * B5 | vm);
}

void Assembler::vrintn(const DwVfpRegister dst, const DwVfpRegister src) {
  // cond=kSpecialCondition(31-28) | 11101(27-23)| D(22) | 11(21-20) |
  // 10(19-18) | RM=01(17-16) |  Vd(15-12) | 101(11-9) | sz=1(8) | 01(7-6) |
  // M(5) | 0(4) | Vm(3-0)
  DCHECK(IsEnabled(ARMv8));
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  emit(kSpecialCondition | 0x1D * B23 | d * B22 | 0x3 * B20 | B19 | 0x1 * B16 |
       vd * B12 | 0x5 * B9 | B8 | B6 | m * B5 | vm);
}

void Assembler::vrintp(const SwVfpRegister dst, const SwVfpRegister src) {
  // cond=kSpecialCondition(31-28) | 11101(27-23)| D(22) | 11(21-20) |
  // 10(19-18) | RM=10(17-16) |  Vd(15-12) | 101(11-9) | sz=0(8) | 01(7-6) |
  // M(5) | 0(4) | Vm(3-0)
  DCHECK(IsEnabled(ARMv8));
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  emit(kSpecialCondition | 0x1D * B23 | d * B22 | 0x3 * B20 | B19 | 0x2 * B16 |
       vd * B12 | 0x5 * B9 | B6 | m * B5 | vm);
}

void Assembler::vrintp(const DwVfpRegister dst, const DwVfpRegister src) {
  // cond=kSpecialCondition(31-28) | 11101(27-23)| D(22) | 11(21-20) |
  // 10(19-18) | RM=10(17-16) |  Vd(15-12) | 101(11-9) | sz=1(8) | 01(7-6) |
  // M(5) | 0(4) | Vm(3-0)
  DCHECK(IsEnabled(ARMv8));
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  emit(kSpecialCondition | 0x1D * B23 | d * B22 | 0x3 * B20 | B19 | 0x2 * B16 |
       vd * B12 | 0x5 * B9 | B8 | B6 | m * B5 | vm);
}

void Assembler::vrintm(const SwVfpRegister dst, const SwVfpRegister src) {
  // cond=kSpecialCondition(31-28) | 11101(27-23)| D(22) | 11(21-20) |
  // 10(19-18) | RM=11(17-16) |  Vd(15-12) | 101(11-9) | sz=0(8) | 01(7-6) |
  // M(5) | 0(4) | Vm(3-0)
  DCHECK(IsEnabled(ARMv8));
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  emit(kSpecialCondition | 0x1D * B23 | d * B22 | 0x3 * B20 | B19 | 0x3 * B16 |
       vd * B12 | 0x5 * B9 | B6 | m * B5 | vm);
}

void Assembler::vrintm(const DwVfpRegister dst, const DwVfpRegister src) {
  // cond=kSpecialCondition(31-28) | 11101(27-23)| D(22) | 11(21-20) |
  // 10(19-18) | RM=11(17-16) |  Vd(15-12) | 101(11-9) | sz=1(8) | 01(7-6) |
  // M(5) | 0(4) | Vm(3-0)
  DCHECK(IsEnabled(ARMv8));
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  emit(kSpecialCondition | 0x1D * B23 | d * B22 | 0x3 * B20 | B19 | 0x3 * B16 |
       vd * B12 | 0x5 * B9 | B8 | B6 | m * B5 | vm);
}

void Assembler::vrintz(const SwVfpRegister dst, const SwVfpRegister src,
                       const Condition cond) {
  // cond(31-28) | 11101(27-23)| D(22) | 11(21-20) | 011(19-17) | 0(16) |
  // Vd(15-12) | 101(11-9) | sz=0(8) | op=1(7) | 1(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(IsEnabled(ARMv8));
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  emit(cond | 0x1D * B23 | d * B22 | 0x3 * B20 | 0x3 * B17 | vd * B12 |
       0x5 * B9 | B7 | B6 | m * B5 | vm);
}

void Assembler::vrintz(const DwVfpRegister dst, const DwVfpRegister src,
                       const Condition cond) {
  // cond(31-28) | 11101(27-23)| D(22) | 11(21-20) | 011(19-17) | 0(16) |
  // Vd(15-12) | 101(11-9) | sz=1(8) | op=1(7) | 1(6) | M(5) | 0(4) | Vm(3-0)
  DCHECK(IsEnabled(ARMv8));
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  emit(cond | 0x1D * B23 | d * B22 | 0x3 * B20 | 0x3 * B17 | vd * B12 |
       0x5 * B9 | B8 | B7 | B6 | m * B5 | vm);
}

// Support for NEON.

void Assembler::vld1(NeonSize size, const NeonListOperand& dst,
                     const NeonMemOperand& src) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.320.
  // 1111(31-28) | 01000(27-23) | D(22) | 10(21-20) | Rn(19-16) |
  // Vd(15-12) | type(11-8) | size(7-6) | align(5-4) | Rm(3-0)
  DCHECK(IsEnabled(NEON));
  int vd, d;
  dst.base().split_code(&vd, &d);
  emit(0xFU * B28 | 4 * B24 | d * B22 | 2 * B20 | src.rn().code() * B16 |
       vd * B12 | dst.type() * B8 | size * B6 | src.align() * B4 |
       src.rm().code());
}

// vld1s(ingle element to one lane).
void Assembler::vld1s(NeonSize size, const NeonListOperand& dst, uint8_t index,
                      const NeonMemOperand& src) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.322.
  // 1111(31-28) | 01001(27-23) | D(22) | 10(21-20) | Rn(19-16) |
  // Vd(15-12) | size(11-10) | index_align(7-4) | Rm(3-0)
  // See vld1 (single element to all lanes) if size == 0x3, implemented as
  // vld1r(eplicate).
  DCHECK_NE(size, 0x3);
  // Check for valid lane indices.
  DCHECK_GT(1 << (3 - size), index);
  // Specifying alignment not supported, use standard alignment.
  uint8_t index_align = index << (size + 1);

  DCHECK(IsEnabled(NEON));
  int vd, d;
  dst.base().split_code(&vd, &d);
  emit(0xFU * B28 | 4 * B24 | 1 * B23 | d * B22 | 2 * B20 |
       src.rn().code() * B16 | vd * B12 | size * B10 | index_align * B4 |
       src.rm().code());
}

// vld1r(eplicate)
void Assembler::vld1r(NeonSize size, const NeonListOperand& dst,
                      const NeonMemOperand& src) {
  DCHECK(IsEnabled(NEON));
  int vd, d;
  dst.base().split_code(&vd, &d);
  emit(0xFU * B28 | 4 * B24 | 1 * B23 | d * B22 | 2 * B20 |
       src.rn().code() * B16 | vd * B12 | 0xC * B8 | size * B6 |
       dst.length() * B5 | src.rm().code());
}

void Assembler::vst1(NeonSize size, const NeonListOperand& src,
                     const NeonMemOperand& dst) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.404.
  // 1111(31-28) | 01000(27-23) | D(22) | 00(21-20) | Rn(19-16) |
  // Vd(15-12) | type(11-8) | size(7-6) | align(5-4) | Rm(3-0)
  DCHECK(IsEnabled(NEON));
  int vd, d;
  src.base().split_code(&vd, &d);
  emit(0xFU * B28 | 4 * B24 | d * B22 | dst.rn().code() * B16 | vd * B12 |
       src.type() * B8 | size * B6 | dst.align() * B4 | dst.rm().code());
}

void Assembler::vst1s(NeonSize size, const NeonListOperand& src, uint8_t index,
                      const NeonMemOperand& dst) {
  // Instruction details available in ARM DDI 0487F.b F6.1.236.
  // 1111(31-28) | 01001(27-23) | D(22) | 00(21-20) | Rn(19-16) |
  // Vd(15-12) | size(11-10) | 00(9-8) | index_align(7-4) | Rm(3-0)
  DCHECK(IsEnabled(NEON));
  DCHECK_NE(size, 0x3);
  DCHECK_GT(1 << (3 - size), index);
  // Specifying alignment not supported, use standard alignment.
  uint8_t index_align = index << (size + 1);
  int vd, d;
  src.base().split_code(&vd, &d);
  emit(0xFU * B28 | 9 * B23 | d * B22 | dst.rn().code() * B16 | vd * B12 |
       size * B10 | index_align * B4 | dst.rm().code());
}

void Assembler::vmovl(NeonDataType dt, QwNeonRegister dst, DwVfpRegister src) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.346.
  // 1111(31-28) | 001(27-25) | U(24) | 1(23) | D(22) | imm3(21-19) |
  // 000(18-16) | Vd(15-12) | 101000(11-6) | M(5) | 1(4) | Vm(3-0)
  DCHECK(IsEnabled(NEON));
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  int U = NeonU(dt);
  int imm3 = 1 << NeonSz(dt);
  emit(0xFU * B28 | B25 | U * B24 | B23 | d * B22 | imm3 * B19 | vd * B12 |
       0xA * B8 | m * B5 | B4 | vm);
}

void Assembler::vqmovn(NeonDataType dst_dt, NeonDataType src_dt,
                       DwVfpRegister dst, QwNeonRegister src) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.1004.
  // vqmovn.<type><size> Dd, Qm. ARM vector narrowing move with saturation.
  // vqmovun.<type><size> Dd, Qm. Same as above, but produces unsigned results.
  DCHECK(IsEnabled(NEON));
  DCHECK_IMPLIES(NeonU(src_dt), NeonU(dst_dt));
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);
  int size = NeonSz(dst_dt);
  DCHECK_NE(3, size);
  int op = NeonU(src_dt) ? 0b11 : NeonU(dst_dt) ? 0b01 : 0b10;
  emit(0x1E7U * B23 | d * B22 | 0x3 * B20 | size * B18 | 0x2 * B16 | vd * B12 |
       0x2 * B8 | op * B6 | m * B5 | vm);
}

static int EncodeScalar(NeonDataType dt, int index) {
  int opc1_opc2 = 0;
  DCHECK_LE(0, index);
  switch (dt) {
    case NeonS8:
    case NeonU8:
      DCHECK_GT(8, index);
      opc1_opc2 = 0x8 | index;
      break;
    case NeonS16:
    case NeonU16:
      DCHECK_GT(4, index);
      opc1_opc2 = 0x1 | (index << 1);
      break;
    case NeonS32:
    case NeonU32:
      DCHECK_GT(2, index);
      opc1_opc2 = index << 2;
      break;
    default:
      UNREACHABLE();
  }
  return (opc1_opc2 >> 2) * B21 | (opc1_opc2 & 0x3) * B5;
}

void Assembler::vmov(NeonDataType dt, DwVfpRegister dst, int index,
                     Register src) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.940.
  // vmov ARM core register to scalar.
  DCHECK(dt == NeonS32 || dt == NeonU32 || IsEnabled(NEON));
  int vd, d;
  dst.split_code(&vd, &d);
  int opc1_opc2 = EncodeScalar(dt, index);
  emit(0xEEu * B24 | vd * B16 | src.code() * B12 | 0xB * B8 | d * B7 | B4 |
       opc1_opc2);
}

void Assembler::vmov(NeonDataType dt, Register dst, DwVfpRegister src,
                     int index) {
  // Instruction details available in ARM DDI 0406C.b, A8.8.942.
  // vmov Arm scalar to core register.
  DCHECK(dt == NeonS32 || dt == NeonU32 || IsEnabled(NEON));
  int vn, n;
  src.split_code(&vn, &n);
  int opc1_opc2 = EncodeScalar(dt, index);
  // NeonS32 and NeonU32 both encoded as u = 0.
  int u = NeonDataTypeToSize(dt) == Neon32 ? 0 : NeonU(dt);
  emit(0xEEu * B24 | u * B23 | B20 | vn * B16 | dst.code() * B12 | 0xB * B8 |
       n * B7 | B4 | opc1_opc2);
}

void Assembler::vmov(QwNeonRegister dst, QwNeonRegister src) {
  // Instruction details available in ARM DDI 0406C.b, A8-938.
  // vmov is encoded as vorr.
  vorr(dst, src, src);
}

void Assembler::vdup(NeonSize size, QwNeonRegister dst, Register src) {
  DCHECK(IsEnabled(NEON));
  // Instruction details available in ARM DDI 0406C.b, A8-886.
  int B = 0, E = 0;
  switch (size) {
    case Neon8:
      B = 1;
      break;
    case Neon16:
      E = 1;
      break;
    case Neon32:
      break;
    default:
      UNREACHABLE();
  }
  int vd, d;
  dst.split_code(&vd, &d);

  emit(al | 0x1D * B23 | B * B22 | B21 | vd * B16 | src.code() * B12 |
       0xB * B8 | d * B7 | E * B5 | B4);
}

enum NeonRegType { NEON_D, NEON_Q };

void NeonSplitCode(NeonRegType type, int code, int* vm, int* m, int* encoding) {
  if (type == NEON_D) {
    DwVfpRegister::split_code(code, vm, m);
  } else {
    DCHECK_EQ(type, NEON_Q);
    QwNeonRegister::split_code(code, vm, m);
    *encoding |= B6;
  }
}

static Instr EncodeNeonDupOp(NeonSize size, NeonRegType reg_type, int dst_code,
                             DwVfpRegister src, int index) {
  DCHECK_NE(Neon64, size);
  int sz = static_cast<int>(size);
  DCHECK_LE(0, index);
  DCHECK_GT(kSimd128Size / (1 << sz), index);
  int imm4 = (1 << sz) | ((index << (sz + 1)) & 0xF);
  int qbit = 0;
  int vd, d;
  NeonSplitCode(reg_type, dst_code, &vd, &d, &qbit);
  int vm, m;
  src.split_code(&vm, &m);

  return 0x1E7U * B23 | d * B22 | 0x3 * B20 | imm4 * B16 | vd * B12 |
         0x18 * B7 | qbit | m * B5 | vm;
}

void Assembler::vdup(NeonSize size, DwVfpRegister dst, DwVfpRegister src,
                     int index) {
  DCHECK(IsEnabled(NEON));
  // Instruction details available in ARM DDI 0406C.b, A8-884.
  emit(EncodeNeonDupOp(size, NEON_D, dst.code(), src, index));
}

void Assembler::vdup(NeonSize size, QwNeonRegister dst, DwVfpRegister src,
                     int index) {
  // Instruction details available in ARM DDI 0406C.b, A8-884.
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonDupOp(size, NEON_Q, dst.code(), src, index));
}

// Encode NEON vcvt.src_type.dst_type instruction.
static Instr EncodeNeonVCVT(VFPType dst_type, QwNeonRegister dst,
                            VFPType src_type, QwNeonRegister src) {
  DCHECK(src_type != dst_type);
  DCHECK(src_type == F32 || dst_type == F32);
  // Instruction details available in ARM DDI 0406C.b, A8.8.868.
  int vd, d;
  dst.split_code(&vd, &d);
  int vm, m;
  src.split_code(&vm, &m);

  int op = 0;
  if (src_type == F32) {
    DCHECK(dst_type == S32 || dst_type == U32);
    op = dst_type == U32 ? 3 : 2;
  } else {
    DCHECK(src_type == S32 || src_type == U32);
    op = src_type == U32 ? 1 : 0;
  }

  return 0x1E7U * B23 | d * B22 | 0x3B * B16 | vd * B12 | 0x3 * B9 | op * B7 |
         B6 | m * B5 | vm;
}

void Assembler::vcvt_f32_s32(QwNeonRegister dst, QwNeonRegister src) {
  DCHECK(IsEnabled(NEON));
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(VfpRegisterIsAvailable(src));
  emit(EncodeNeonVCVT(F32, dst, S32, src));
}

void Assembler::vcvt_f32_u32(QwNeonRegister dst, QwNeonRegister src) {
  DCHECK(IsEnabled(NEON));
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(VfpRegisterIsAvailable(src));
  emit(EncodeNeonVCVT(F32, dst, U32, src));
}

void Assembler::vcvt_s32_f32(QwNeonRegister dst, QwNeonRegister src) {
  DCHECK(IsEnabled(NEON));
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(VfpRegisterIsAvailable(src));
  emit(EncodeNeonVCVT(S32, dst, F32, src));
}

void Assembler::vcvt_u32_f32(QwNeonRegister dst, QwNeonRegister src) {
  DCHECK(IsEnabled(NEON));
  DCHECK(VfpRegisterIsAvailable(dst));
  DCHECK(VfpRegisterIsAvailable(src));
  emit(EncodeNeonVCVT(U32, dst, F32, src));
}

enum UnaryOp {
  VMVN,
  VSWP,
  VABS,
  VABSF,
  VNEG,
  VNEGF,
  VRINTM,
  VRINTN,
  VRINTP,
  VRINTZ,
  VZIP,
  VUZP,
  VREV16,
  VREV32,
  VREV64,
  VTRN,
  VRECPE,
  VRSQRTE,
  VPADAL_S,
  VPADAL_U,
  VPADDL_S,
  VPADDL_U,
  VCEQ0,
  VCLT0,
  VCNT
};

// Encoding helper for "Advanced SIMD two registers misc" decode group. See ARM
// DDI 0487F.b, F4-4228.
static Instr EncodeNeonUnaryOp(UnaryOp op, NeonRegType reg_type, NeonSize size,
                               int dst_code, int src_code) {
  int op_encoding = 0;
  switch (op) {
    case VMVN:
      DCHECK_EQ(Neon8, size);  // size == 0 for vmvn
      op_encoding = B10 | 0x3 * B7;
      break;
    case VSWP:
      DCHECK_EQ(Neon8, size);  // size == 0 for vswp
      op_encoding = B17;
      break;
    case VABS:
      op_encoding = B16 | 0x6 * B7;
      break;
    case VABSF:
      DCHECK_EQ(Neon32, size);
      op_encoding = B16 | B10 | 0x6 * B7;
      break;
    case VNEG:
      op_encoding = B16 | 0x7 * B7;
      break;
    case VNEGF:
      DCHECK_EQ(Neon32, size);
      op_encoding = B16 | B10 | 0x7 * B7;
      break;
    case VRINTM:
      op_encoding = B17 | 0xD * B7;
      break;
    case VRINTN:
      op_encoding = B17 | 0x8 * B7;
      break;
    case VRINTP:
      op_encoding = B17 | 0xF * B7;
      break;
    case VRINTZ:
      op_encoding = B17 | 0xB * B7;
      break;
    case VZIP:
      op_encoding = 0x2 * B16 | 0x3 * B7;
      break;
    case VUZP:
      op_encoding = 0x2 * B16 | 0x2 * B7;
      break;
    case VREV16:
      op_encoding = 0x2 * B7;
      break;
    case VREV32:
      op_encoding = 0x1 * B7;
      break;
    case VREV64:
      // op_encoding is 0;
      break;
    case VTRN:
      op_encoding = 0x2 * B16 | B7;
      break;
    case VRECPE:
      // Only support floating point.
      op_encoding = 0x3 * B16 | 0xA * B7;
      break;
    case VRSQRTE:
      // Only support floating point.
      op_encoding = 0x3 * B16 | 0xB * B7;
      break;
    case VPADAL_S:
      op_encoding = 0xC * B7;
      break;
    case VPADAL_U:
      op_encoding = 0xD * B7;
      break;
    case VPADDL_S:
      op_encoding = 0x4 * B7;
      break;
    case VPADDL_U:
      op_encoding = 0x5 * B7;
      break;
    case VCEQ0:
      // Only support integers.
      op_encoding = 0x1 * B16 | 0x2 * B7;
      break;
    case VCLT0:
      // Only support signed integers.
      op_encoding = 0x1 * B16 | 0x4 * B7;
      break;
    case VCNT:
      op_encoding = 0xA * B7;
      break;
  }
  int vd, d;
  NeonSplitCode(reg_type, dst_code, &vd, &d, &op_encoding);
  int vm, m;
  NeonSplitCode(reg_type, src_code, &vm, &m, &op_encoding);

  return 0x1E7U * B23 | d * B22 | 0x3 * B20 | size * B18 | vd * B12 | m * B5 |
         vm | op_encoding;
}

void Assembler::vmvn(QwNeonRegister dst, QwNeonRegister src) {
  // Qd = vmvn(Qn, Qm) SIMD bitwise negate.
  // Instruction details available in ARM DDI 0406C.b, A8-966.
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonUnaryOp(VMVN, NEON_Q, Neon8, dst.code(), src.code()));
}

void Assembler::vswp(DwVfpRegister dst, DwVfpRegister src) {
  DCHECK(IsEnabled(NEON));
  // Dd = vswp(Dn, Dm) SIMD d-register swap.
  // Instruction details available in ARM DDI 0406C.b, A8.8.418.
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonUnaryOp(VSWP, NEON_D, Neon8, dst.code(), src.code()));
}

void Assembler::vswp(QwNeonRegister dst, QwNeonRegister src) {
  // Qd = vswp(Qn, Qm) SIMD q-register swap.
  // Instruction details available in ARM DDI 0406C.b, A8.8.418.
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonUnaryOp(VSWP, NEON_Q, Neon8, dst.code(), src.code()));
}

void Assembler::vabs(QwNeonRegister dst, QwNeonRegister src) {
  // Qd = vabs.f<size>(Qn, Qm) SIMD floating point absolute value.
  // Instruction details available in ARM DDI 0406C.b, A8.8.824.
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonUnaryOp(VABSF, NEON_Q, Neon32, dst.code(), src.code()));
}

void Assembler::vabs(NeonSize size, QwNeonRegister dst, QwNeonRegister src) {
  // Qd = vabs.s<size>(Qn, Qm) SIMD integer absolute value.
  // Instruction details available in ARM DDI 0406C.b, A8.8.824.
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonUnaryOp(VABS, NEON_Q, size, dst.code(), src.code()));
}

void Assembler::vneg(QwNeonRegister dst, QwNeonRegister src) {
  // Qd = vabs.f<size>(Qn, Qm) SIMD floating point negate.
  // Instruction details available in ARM DDI 0406C.b, A8.8.968.
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonUnaryOp(VNEGF, NEON_Q, Neon32, dst.code(), src.code()));
}

void Assembler::vneg(NeonSize size, QwNeonRegister dst, QwNeonRegister src) {
  // Qd = vabs.s<size>(Qn, Qm) SIMD integer negate.
  // Instruction details available in ARM DDI 0406C.b, A8.8.968.
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonUnaryOp(VNEG, NEON_Q, size, dst.code(), src.code()));
}

enum BinaryBitwiseOp { VAND, VBIC, VBIF, VBIT, VBSL, VEOR, VORR, VORN };

static Instr EncodeNeonBinaryBitwiseOp(BinaryBitwiseOp op, NeonRegType reg_type,
                                       int dst_code, int src_code1,
                                       int src_code2) {
  int op_encoding = 0;
  switch (op) {
    case VBIC:
      op_encoding = 0x1 * B20;
      break;
    case VBIF:
      op_encoding = B24 | 0x3 * B20;
      break;
    case VBIT:
      op_encoding = B24 | 0x2 * B20;
      break;
    case VBSL:
      op_encoding = B24 | 0x1 * B20;
      break;
    case VEOR:
      op_encoding = B24;
      break;
    case VORR:
      op_encoding = 0x2 * B20;
      break;
    case VORN:
      op_encoding = 0x3 * B20;
      break;
    case VAND:
      // op_encoding is 0.
      break;
    default:
      UNREACHABLE();
  }
  int vd, d;
  NeonSplitCode(reg_type, dst_code, &vd, &d, &op_encoding);
  int vn, n;
  NeonSplitCode(reg_type, src_code1, &vn, &n, &op_encoding);
  int vm, m;
  NeonSplitCode(reg_type, src_code2, &vm, &m, &op_encoding);

  return 0x1E4U * B23 | op_encoding | d * B22 | vn * B16 | vd * B12 | B8 |
         n * B7 | m * B5 | B4 | vm;
}

void Assembler::vand(QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  // Qd = vand(Qn, Qm) SIMD AND.
  // Instruction details available in ARM DDI 0406C.b, A8.8.836.
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonBinaryBitwiseOp(VAND, NEON_Q, dst.code(), src1.code(),
                                 src2.code()));
}

void Assembler::vbic(QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  // Qd = vbic(Qn, Qm) SIMD AND.
  // Instruction details available in ARM DDI 0406C.b, A8-840.
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonBinaryBitwiseOp(VBIC, NEON_Q, dst.code(), src1.code(),
                                 src2.code()));
}

void Assembler::vbsl(QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  // Qd = vbsl(Qn, Qm) SIMD bitwise select.
  // Instruction details available in ARM DDI 0406C.b, A8-844.
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonBinaryBitwiseOp(VBSL, NEON_Q, dst.code(), src1.code(),
                                 src2.code()));
}

void Assembler::veor(DwVfpRegister dst, DwVfpRegister src1,
                     DwVfpRegister src2) {
  // Dd = veor(Dn, Dm) SIMD exclusive OR.
  // Instruction details available in ARM DDI 0406C.b, A8.8.888.
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonBinaryBitwiseOp(VEOR, NEON_D, dst.code(), src1.code(),
                                 src2.code()));
}

void Assembler::veor(QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  // Qd = veor(Qn, Qm) SIMD exclusive OR.
  // Instruction details available in ARM DDI 0406C.b, A8.8.888.
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonBinaryBitwiseOp(VEOR, NEON_Q, dst.code(), src1.code(),
                                 src2.code()));
}

void Assembler::vorr(QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  // Qd = vorr(Qn, Qm) SIMD OR.
  // Instruction details available in ARM DDI 0406C.b, A8.8.976.
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonBinaryBitwiseOp(VORR, NEON_Q, dst.code(), src1.code(),
                                 src2.code()));
}

void Assembler::vorn(QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  // Qd = vorn(Qn, Qm) SIMD OR NOT.
  // Instruction details available in ARM DDI 0406C.d, A8.8.359.
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonBinaryBitwiseOp(VORN, NEON_Q, dst.code(), src1.code(),
                                 src2.code()));
}

enum FPBinOp {
  VADDF,
  VSUBF,
  VMULF,
  VMINF,
  VMAXF,
  VRECPS,
  VRSQRTS,
  VCEQF,
  VCGEF,
  VCGTF
};

static Instr EncodeNeonBinOp(FPBinOp op, QwNeonRegister dst,
                             QwNeonRegister src1, QwNeonRegister src2) {
  int op_encoding = 0;
  switch (op) {
    case VADDF:
      op_encoding = 0xD * B8;
      break;
    case VSUBF:
      op_encoding = B21 | 0xD * B8;
      break;
    case VMULF:
      op_encoding = B24 | 0xD * B8 | B4;
      break;
    case VMINF:
      op_encoding = B21 | 0xF * B8;
      break;
    case VMAXF:
      op_encoding = 0xF * B8;
      break;
    case VRECPS:
      op_encoding = 0xF * B8 | B4;
      break;
    case VRSQRTS:
      op_encoding = B21 | 0xF * B8 | B4;
      break;
    case VCEQF:
      op_encoding = 0xE * B8;
      break;
    case VCGEF:
      op_encoding = B24 | 0xE * B8;
      break;
    case VCGTF:
      op_encoding = B24 | B21 | 0xE * B8;
      break;
    default:
      UNREACHABLE();
  }
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  return 0x1E4U * B23 | d * B22 | vn * B16 | vd * B12 | n * B7 | B6 | m * B5 |
         vm | op_encoding;
}

enum IntegerBinOp {
  VADD,
  VQADD,
  VSUB,
  VQSUB,
  VMUL,
  VMIN,
  VMAX,
  VTST,
  VCEQ,
  VCGE,
  VCGT,
  VRHADD,
  VQRDMULH
};

static Instr EncodeNeonDataTypeBinOp(IntegerBinOp op, NeonDataType dt,
                                     QwNeonRegister dst, QwNeonRegister src1,
                                     QwNeonRegister src2) {
  int op_encoding = 0;
  switch (op) {
    case VADD:
      op_encoding = 0x8 * B8;
      break;
    case VQADD:
      op_encoding = B4;
      break;
    case VSUB:
      op_encoding = B24 | 0x8 * B8;
      break;
    case VQSUB:
      op_encoding = 0x2 * B8 | B4;
      break;
    case VMUL:
      op_encoding = 0x9 * B8 | B4;
      break;
    case VMIN:
      op_encoding = 0x6 * B8 | B4;
      break;
    case VMAX:
      op_encoding = 0x6 * B8;
      break;
    case VTST:
      op_encoding = 0x8 * B8 | B4;
      break;
    case VCEQ:
      op_encoding = B24 | 0x8 * B8 | B4;
      break;
    case VCGE:
      op_encoding = 0x3 * B8 | B4;
      break;
    case VCGT:
      op_encoding = 0x3 * B8;
      break;
    case VRHADD:
      op_encoding = B8;
      break;
    case VQRDMULH:
      op_encoding = B24 | 0xB * B8;
      break;
    default:
      UNREACHABLE();
  }
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  int size = NeonSz(dt);
  int u = NeonU(dt);
  return 0x1E4U * B23 | u * B24 | d * B22 | size * B20 | vn * B16 | vd * B12 |
         n * B7 | B6 | m * B5 | vm | op_encoding;
}

static Instr EncodeNeonSizeBinOp(IntegerBinOp op, NeonSize size,
                                 QwNeonRegister dst, QwNeonRegister src1,
                                 QwNeonRegister src2) {
  // Map NeonSize values to the signed values in NeonDataType, so the U bit
  // will be 0.
  return EncodeNeonDataTypeBinOp(op, static_cast<NeonDataType>(size), dst, src1,
                                 src2);
}

void Assembler::vadd(QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vadd(Qn, Qm) SIMD floating point addition.
  // Instruction details available in ARM DDI 0406C.b, A8-830.
  emit(EncodeNeonBinOp(VADDF, dst, src1, src2));
}

void Assembler::vadd(NeonSize size, QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vadd(Qn, Qm) SIMD integer addition.
  // Instruction details available in ARM DDI 0406C.b, A8-828.
  emit(EncodeNeonSizeBinOp(VADD, size, dst, src1, src2));
}

void Assembler::vqadd(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src1,
                      QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vqadd(Qn, Qm) SIMD integer saturating addition.
  // Instruction details available in ARM DDI 0406C.b, A8-996.
  emit(EncodeNeonDataTypeBinOp(VQADD, dt, dst, src1, src2));
}

void Assembler::vsub(QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vsub(Qn, Qm) SIMD floating point subtraction.
  // Instruction details available in ARM DDI 0406C.b, A8-1086.
  emit(EncodeNeonBinOp(VSUBF, dst, src1, src2));
}

void Assembler::vsub(NeonSize size, QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vsub(Qn, Qm) SIMD integer subtraction.
  // Instruction details available in ARM DDI 0406C.b, A8-1084.
  emit(EncodeNeonSizeBinOp(VSUB, size, dst, src1, src2));
}

void Assembler::vqsub(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src1,
                      QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vqsub(Qn, Qm) SIMD integer saturating subtraction.
  // Instruction details available in ARM DDI 0406C.b, A8-1020.
  emit(EncodeNeonDataTypeBinOp(VQSUB, dt, dst, src1, src2));
}

void Assembler::vmlal(NeonDataType dt, QwNeonRegister dst, DwVfpRegister src1,
                      DwVfpRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vmlal(Dn, Dm) Vector Multiply Accumulate Long (integer)
  // Instruction details available in ARM DDI 0406C.b, A8-931.
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  int size = NeonSz(dt);
  int u = NeonU(dt);
  if (!u) UNIMPLEMENTED();
  DCHECK_NE(size, 3);  // SEE "Related encodings"
  emit(0xFU * B28 | B25 | u * B24 | B23 | d * B22 | size * B20 | vn * B16 |
       vd * B12 | 0x8 * B8 | n * B7 | m * B5 | vm);
}

void Assembler::vmul(QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vadd(Qn, Qm) SIMD floating point multiply.
  // Instruction details available in ARM DDI 0406C.b, A8-958.
  emit(EncodeNeonBinOp(VMULF, dst, src1, src2));
}

void Assembler::vmul(NeonSize size, QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vadd(Qn, Qm) SIMD integer multiply.
  // Instruction details available in ARM DDI 0406C.b, A8-960.
  emit(EncodeNeonSizeBinOp(VMUL, size, dst, src1, src2));
}

void Assembler::vmull(NeonDataType dt, QwNeonRegister dst, DwVfpRegister src1,
                      DwVfpRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vmull(Dn, Dm) Vector Multiply Long (integer).
  // Instruction details available in ARM DDI 0406C.b, A8-960.
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  int size = NeonSz(dt);
  int u = NeonU(dt);
  emit(0xFU * B28 | B25 | u * B24 | B23 | d * B22 | size * B20 | vn * B16 |
       vd * B12 | 0xC * B8 | n * B7 | m * B5 | vm);
}

void Assembler::vmin(QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vmin(Qn, Qm) SIMD floating point MIN.
  // Instruction details available in ARM DDI 0406C.b, A8-928.
  emit(EncodeNeonBinOp(VMINF, dst, src1, src2));
}

void Assembler::vmin(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vmin(Qn, Qm) SIMD integer MIN.
  // Instruction details available in ARM DDI 0406C.b, A8-926.
  emit(EncodeNeonDataTypeBinOp(VMIN, dt, dst, src1, src2));
}

void Assembler::vmax(QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vmax(Qn, Qm) SIMD floating point MAX.
  // Instruction details available in ARM DDI 0406C.b, A8-928.
  emit(EncodeNeonBinOp(VMAXF, dst, src1, src2));
}

void Assembler::vmax(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vmax(Qn, Qm) SIMD integer MAX.
  // Instruction details available in ARM DDI 0406C.b, A8-926.
  emit(EncodeNeonDataTypeBinOp(VMAX, dt, dst, src1, src2));
}

enum NeonShiftOp { VSHL, VSHR, VSLI, VSRI, VSRA };

static Instr EncodeNeonShiftRegisterOp(NeonShiftOp op, NeonDataType dt,
                                       NeonRegType reg_type, int dst_code,
                                       int src_code, int shift_code) {
  DCHECK_EQ(op, VSHL);
  int op_encoding = 0;
  int vd, d;
  NeonSplitCode(reg_type, dst_code, &vd, &d, &op_encoding);
  int vm, m;
  NeonSplitCode(reg_type, src_code, &vm, &m, &op_encoding);
  int vn, n;
  NeonSplitCode(reg_type, shift_code, &vn, &n, &op_encoding);
  int size = NeonSz(dt);
  int u = NeonU(dt);

  return 0x1E4U * B23 | u * B24 | d * B22 | size * B20 | vn * B16 | vd * B12 |
         0x4 * B8 | n * B7 | m * B5 | vm | op_encoding;
}

static Instr EncodeNeonShiftOp(NeonShiftOp op, NeonSize size, bool is_unsigned,
                               NeonRegType reg_type, int dst_code, int src_code,
                               int shift) {
  int size_in_bits = kBitsPerByte << static_cast<int>(size);
  int op_encoding = 0, imm6 = 0, L = 0;
  switch (op) {
    case VSHL: {
      DCHECK(shift >= 0 && size_in_bits > shift);
      imm6 = size_in_bits + shift;
      op_encoding = 0x5 * B8;
      break;
    }
    case VSHR: {
      DCHECK(shift > 0 && size_in_bits >= shift);
      imm6 = 2 * size_in_bits - shift;
      if (is_unsigned) op_encoding |= B24;
      break;
    }
    case VSLI: {
      DCHECK(shift >= 0 && size_in_bits > shift);
      imm6 = size_in_bits + shift;
      op_encoding = B24 | 0x5 * B8;
      break;
    }
    case VSRI: {
      DCHECK(shift > 0 && size_in_bits >= shift);
      imm6 = 2 * size_in_bits - shift;
      op_encoding = B24 | 0x4 * B8;
      break;
    }
    case VSRA: {
      DCHECK(shift > 0 && size_in_bits >= shift);
      imm6 = 2 * size_in_bits - shift;
      op_encoding = B8;
      if (is_unsigned) op_encoding |= B24;
      break;
    }
    default:
      UNREACHABLE();
  }

  L = imm6 >> 6;
  imm6 &= 0x3F;

  int vd, d;
  NeonSplitCode(reg_type, dst_code, &vd, &d, &op_encoding);
  int vm, m;
  NeonSplitCode(reg_type, src_code, &vm, &m, &op_encoding);

  return 0x1E5U * B23 | d * B22 | imm6 * B16 | vd * B12 | L * B7 | m * B5 | B4 |
         vm | op_encoding;
}

void Assembler::vshl(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src,
                     int shift) {
  DCHECK(IsEnabled(NEON));
  // Qd = vshl(Qm, bits) SIMD shift left immediate.
  // Instruction details available in ARM DDI 0406C.b, A8-1046.
  emit(EncodeNeonShiftOp(VSHL, NeonDataTypeToSize(dt), false, NEON_Q,
                         dst.code(), src.code(), shift));
}

void Assembler::vshl(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src,
                     QwNeonRegister shift) {
  DCHECK(IsEnabled(NEON));
  // Qd = vshl(Qm, Qn) SIMD shift left Register.
  // Instruction details available in ARM DDI 0487A.a, F8-3340..
  emit(EncodeNeonShiftRegisterOp(VSHL, dt, NEON_Q, dst.code(), src.code(),
                                 shift.code()));
}

void Assembler::vshr(NeonDataType dt, DwVfpRegister dst, DwVfpRegister src,
                     int shift) {
  DCHECK(IsEnabled(NEON));
  // Dd = vshr(Dm, bits) SIMD shift right immediate.
  // Instruction details available in ARM DDI 0406C.b, A8-1052.
  emit(EncodeNeonShiftOp(VSHR, NeonDataTypeToSize(dt), NeonU(dt), NEON_D,
                         dst.code(), src.code(), shift));
}

void Assembler::vshr(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src,
                     int shift) {
  DCHECK(IsEnabled(NEON));
  // Qd = vshr(Qm, bits) SIMD shift right immediate.
  // Instruction details available in ARM DDI 0406C.b, A8-1052.
  emit(EncodeNeonShiftOp(VSHR, NeonDataTypeToSize(dt), NeonU(dt), NEON_Q,
                         dst.code(), src.code(), shift));
}

void Assembler::vsli(NeonSize size, DwVfpRegister dst, DwVfpRegister src,
                     int shift) {
  DCHECK(IsEnabled(NEON));
  // Dd = vsli(Dm, bits) SIMD shift left and insert.
  // Instruction details available in ARM DDI 0406C.b, A8-1056.
  emit(EncodeNeonShiftOp(VSLI, size, false, NEON_D, dst.code(), src.code(),
                         shift));
}

void Assembler::vsri(NeonSize size, DwVfpRegister dst, DwVfpRegister src,
                     int shift) {
  DCHECK(IsEnabled(NEON));
  // Dd = vsri(Dm, bits) SIMD shift right and insert.
  // Instruction details available in ARM DDI 0406C.b, A8-1062.
  emit(EncodeNeonShiftOp(VSRI, size, false, NEON_D, dst.code(), src.code(),
                         shift));
}

void Assembler::vsra(NeonDataType dt, DwVfpRegister dst, DwVfpRegister src,
                     int imm) {
  DCHECK(IsEnabled(NEON));
  // Dd = vsra(Dm, imm) SIMD shift right and accumulate.
  // Instruction details available in ARM DDI 0487F.b, F6-5569.
  emit(EncodeNeonShiftOp(VSRA, NeonDataTypeToSize(dt), NeonU(dt), NEON_D,
                         dst.code(), src.code(), imm));
}

void Assembler::vrecpe(QwNeonRegister dst, QwNeonRegister src) {
  DCHECK(IsEnabled(NEON));
  // Qd = vrecpe(Qm) SIMD reciprocal estimate.
  // Instruction details available in ARM DDI 0406C.b, A8-1024.
  emit(EncodeNeonUnaryOp(VRECPE, NEON_Q, Neon32, dst.code(), src.code()));
}

void Assembler::vrsqrte(QwNeonRegister dst, QwNeonRegister src) {
  DCHECK(IsEnabled(NEON));
  // Qd = vrsqrte(Qm) SIMD reciprocal square root estimate.
  // Instruction details available in ARM DDI 0406C.b, A8-1038.
  emit(EncodeNeonUnaryOp(VRSQRTE, NEON_Q, Neon32, dst.code(), src.code()));
}

void Assembler::vrecps(QwNeonRegister dst, QwNeonRegister src1,
                       QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vrecps(Qn, Qm) SIMD reciprocal refinement step.
  // Instruction details available in ARM DDI 0406C.b, A8-1026.
  emit(EncodeNeonBinOp(VRECPS, dst, src1, src2));
}

void Assembler::vrsqrts(QwNeonRegister dst, QwNeonRegister src1,
                        QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vrsqrts(Qn, Qm) SIMD reciprocal square root refinement step.
  // Instruction details available in ARM DDI 0406C.b, A8-1040.
  emit(EncodeNeonBinOp(VRSQRTS, dst, src1, src2));
}

enum NeonPairwiseOp { VPADD, VPMIN, VPMAX };

static Instr EncodeNeonPairwiseOp(NeonPairwiseOp op, NeonDataType dt,
                                  DwVfpRegister dst, DwVfpRegister src1,
                                  DwVfpRegister src2) {
  int op_encoding = 0;
  switch (op) {
    case VPADD:
      op_encoding = 0xB * B8 | B4;
      break;
    case VPMIN:
      op_encoding = 0xA * B8 | B4;
      break;
    case VPMAX:
      op_encoding = 0xA * B8;
      break;
    default:
      UNREACHABLE();
  }
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  int size = NeonSz(dt);
  int u = NeonU(dt);
  return 0x1E4U * B23 | u * B24 | d * B22 | size * B20 | vn * B16 | vd * B12 |
         n * B7 | m * B5 | vm | op_encoding;
}

void Assembler::vpadd(DwVfpRegister dst, DwVfpRegister src1,
                      DwVfpRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Dd = vpadd(Dn, Dm) SIMD floating point pairwise ADD.
  // Instruction details available in ARM DDI 0406C.b, A8-982.
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);

  emit(0x1E6U * B23 | d * B22 | vn * B16 | vd * B12 | 0xD * B8 | n * B7 |
       m * B5 | vm);
}

void Assembler::vpadd(NeonSize size, DwVfpRegister dst, DwVfpRegister src1,
                      DwVfpRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Dd = vpadd(Dn, Dm) SIMD integer pairwise ADD.
  // Instruction details available in ARM DDI 0406C.b, A8-980.
  emit(EncodeNeonPairwiseOp(VPADD, NeonSizeToDataType(size), dst, src1, src2));
}

void Assembler::vpmin(NeonDataType dt, DwVfpRegister dst, DwVfpRegister src1,
                      DwVfpRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Dd = vpmin(Dn, Dm) SIMD integer pairwise MIN.
  // Instruction details available in ARM DDI 0406C.b, A8-986.
  emit(EncodeNeonPairwiseOp(VPMIN, dt, dst, src1, src2));
}

void Assembler::vpmax(NeonDataType dt, DwVfpRegister dst, DwVfpRegister src1,
                      DwVfpRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Dd = vpmax(Dn, Dm) SIMD integer pairwise MAX.
  // Instruction details available in ARM DDI 0406C.b, A8-986.
  emit(EncodeNeonPairwiseOp(VPMAX, dt, dst, src1, src2));
}

void Assembler::vrintm(NeonDataType dt, const QwNeonRegister dst,
                       const QwNeonRegister src) {
  // SIMD vector round floating-point to integer towards -Infinity.
  // See ARM DDI 0487F.b, F6-5493.
  DCHECK(IsEnabled(ARMv8));
  emit(EncodeNeonUnaryOp(VRINTM, NEON_Q, NeonSize(dt), dst.code(), src.code()));
}

void Assembler::vrintn(NeonDataType dt, const QwNeonRegister dst,
                       const QwNeonRegister src) {
  // SIMD vector round floating-point to integer to Nearest.
  // See ARM DDI 0487F.b, F6-5497.
  DCHECK(IsEnabled(ARMv8));
  emit(EncodeNeonUnaryOp(VRINTN, NEON_Q, NeonSize(dt), dst.code(), src.code()));
}

void Assembler::vrintp(NeonDataType dt, const QwNeonRegister dst,
                       const QwNeonRegister src) {
  // SIMD vector round floating-point to integer towards +Infinity.
  // See ARM DDI 0487F.b, F6-5501.
  DCHECK(IsEnabled(ARMv8));
  emit(EncodeNeonUnaryOp(VRINTP, NEON_Q, NeonSize(dt), dst.code(), src.code()));
}

void Assembler::vrintz(NeonDataType dt, const QwNeonRegister dst,
                       const QwNeonRegister src) {
  // SIMD vector round floating-point to integer towards Zero.
  // See ARM DDI 0487F.b, F6-5511.
  DCHECK(IsEnabled(ARMv8));
  emit(EncodeNeonUnaryOp(VRINTZ, NEON_Q, NeonSize(dt), dst.code(), src.code()));
}

void Assembler::vtst(NeonSize size, QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vtst(Qn, Qm) SIMD test integer operands.
  // Instruction details available in ARM DDI 0406C.b, A8-1098.
  emit(EncodeNeonSizeBinOp(VTST, size, dst, src1, src2));
}

void Assembler::vceq(QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vceq(Qn, Qm) SIMD floating point compare equal.
  // Instruction details available in ARM DDI 0406C.b, A8-844.
  emit(EncodeNeonBinOp(VCEQF, dst, src1, src2));
}

void Assembler::vceq(NeonSize size, QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vceq(Qn, Qm) SIMD integer compare equal.
  // Instruction details available in ARM DDI 0406C.b, A8-844.
  emit(EncodeNeonSizeBinOp(VCEQ, size, dst, src1, src2));
}

void Assembler::vceq(NeonSize size, QwNeonRegister dst, QwNeonRegister src1,
                     int value) {
  DCHECK(IsEnabled(NEON));
  DCHECK_EQ(0, value);
  // Qd = vceq(Qn, Qm, #0) Vector Compare Equal to Zero.
  // Instruction details available in ARM DDI 0406C.d, A8-847.
  emit(EncodeNeonUnaryOp(VCEQ0, NEON_Q, size, dst.code(), src1.code()));
}

void Assembler::vcge(QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vcge(Qn, Qm) SIMD floating point compare greater or equal.
  // Instruction details available in ARM DDI 0406C.b, A8-848.
  emit(EncodeNeonBinOp(VCGEF, dst, src1, src2));
}

void Assembler::vcge(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vcge(Qn, Qm) SIMD integer compare greater or equal.
  // Instruction details available in ARM DDI 0406C.b, A8-848.
  emit(EncodeNeonDataTypeBinOp(VCGE, dt, dst, src1, src2));
}

void Assembler::vcgt(QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vcgt(Qn, Qm) SIMD floating point compare greater than.
  // Instruction details available in ARM DDI 0406C.b, A8-852.
  emit(EncodeNeonBinOp(VCGTF, dst, src1, src2));
}

void Assembler::vcgt(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vcgt(Qn, Qm) SIMD integer compare greater than.
  // Instruction details available in ARM DDI 0406C.b, A8-852.
  emit(EncodeNeonDataTypeBinOp(VCGT, dt, dst, src1, src2));
}

void Assembler::vclt(NeonSize size, QwNeonRegister dst, QwNeonRegister src,
                     int value) {
  DCHECK(IsEnabled(NEON));
  DCHECK_EQ(0, value);
  // vclt.<size>(Qn, Qm, #0) SIMD Vector Compare Less Than Zero.
  // Instruction details available in ARM DDI 0487F.b, F6-5072.
  emit(EncodeNeonUnaryOp(VCLT0, NEON_Q, size, dst.code(), src.code()));
}

void Assembler::vrhadd(NeonDataType dt, QwNeonRegister dst, QwNeonRegister src1,
                       QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // Qd = vrhadd(Qn, Qm) SIMD integer rounding halving add.
  // Instruction details available in ARM DDI 0406C.b, A8-1030.
  emit(EncodeNeonDataTypeBinOp(VRHADD, dt, dst, src1, src2));
}

void Assembler::vext(QwNeonRegister dst, QwNeonRegister src1,
                     QwNeonRegister src2, int bytes) {
  DCHECK(IsEnabled(NEON));
  // Qd = vext(Qn, Qm) SIMD byte extract.
  // Instruction details available in ARM DDI 0406C.b, A8-890.
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  src1.split_code(&vn, &n);
  int vm, m;
  src2.split_code(&vm, &m);
  DCHECK_GT(16, bytes);
  emit(0x1E5U * B23 | d * B22 | 0x3 * B20 | vn * B16 | vd * B12 | bytes * B8 |
       n * B7 | B6 | m * B5 | vm);
}

void Assembler::vzip(NeonSize size, DwVfpRegister src1, DwVfpRegister src2) {
  if (size == Neon32) {  // vzip.32 Dd, Dm is a pseudo-op for vtrn.32 Dd, Dm.
    vtrn(size, src1, src2);
  } else {
    DCHECK(IsEnabled(NEON));
    // vzip.<size>(Dn, Dm) SIMD zip (interleave).
    // Instruction details available in ARM DDI 0406C.b, A8-1102.
    emit(EncodeNeonUnaryOp(VZIP, NEON_D, size, src1.code(), src2.code()));
  }
}

void Assembler::vzip(NeonSize size, QwNeonRegister src1, QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // vzip.<size>(Qn, Qm) SIMD zip (interleave).
  // Instruction details available in ARM DDI 0406C.b, A8-1102.
  emit(EncodeNeonUnaryOp(VZIP, NEON_Q, size, src1.code(), src2.code()));
}

void Assembler::vuzp(NeonSize size, DwVfpRegister src1, DwVfpRegister src2) {
  if (size == Neon32) {  // vuzp.32 Dd, Dm is a pseudo-op for vtrn.32 Dd, Dm.
    vtrn(size, src1, src2);
  } else {
    DCHECK(IsEnabled(NEON));
    // vuzp.<size>(Dn, Dm) SIMD un-zip (de-interleave).
    // Instruction details available in ARM DDI 0406C.b, A8-1100.
    emit(EncodeNeonUnaryOp(VUZP, NEON_D, size, src1.code(), src2.code()));
  }
}

void Assembler::vuzp(NeonSize size, QwNeonRegister src1, QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // vuzp.<size>(Qn, Qm) SIMD un-zip (de-interleave).
  // Instruction details available in ARM DDI 0406C.b, A8-1100.
  emit(EncodeNeonUnaryOp(VUZP, NEON_Q, size, src1.code(), src2.code()));
}

void Assembler::vrev16(NeonSize size, QwNeonRegister dst, QwNeonRegister src) {
  DCHECK(IsEnabled(NEON));
  // Qd = vrev16.<size>(Qm) SIMD element reverse.
  // Instruction details available in ARM DDI 0406C.b, A8-1028.
  emit(EncodeNeonUnaryOp(VREV16, NEON_Q, size, dst.code(), src.code()));
}

void Assembler::vrev32(NeonSize size, QwNeonRegister dst, QwNeonRegister src) {
  DCHECK(IsEnabled(NEON));
  // Qd = vrev32.<size>(Qm) SIMD element reverse.
  // Instruction details available in ARM DDI 0406C.b, A8-1028.
  emit(EncodeNeonUnaryOp(VREV32, NEON_Q, size, dst.code(), src.code()));
}

void Assembler::vrev64(NeonSize size, QwNeonRegister dst, QwNeonRegister src) {
  DCHECK(IsEnabled(NEON));
  // Qd = vrev64.<size>(Qm) SIMD element reverse.
  // Instruction details available in ARM DDI 0406C.b, A8-1028.
  emit(EncodeNeonUnaryOp(VREV64, NEON_Q, size, dst.code(), src.code()));
}

void Assembler::vtrn(NeonSize size, DwVfpRegister src1, DwVfpRegister src2) {
  DCHECK(IsEnabled(NEON));
  // vtrn.<size>(Dn, Dm) SIMD element transpose.
  // Instruction details available in ARM DDI 0406C.b, A8-1096.
  emit(EncodeNeonUnaryOp(VTRN, NEON_D, size, src1.code(), src2.code()));
}

void Assembler::vtrn(NeonSize size, QwNeonRegister src1, QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  // vtrn.<size>(Qn, Qm) SIMD element transpose.
  // Instruction details available in ARM DDI 0406C.b, A8-1096.
  emit(EncodeNeonUnaryOp(VTRN, NEON_Q, size, src1.code(), src2.code()));
}

void Assembler::vpadal(NeonDataType dt, QwNeonRegister dst,
                       QwNeonRegister src) {
  DCHECK(IsEnabled(NEON));
  // vpadal.<dt>(Qd, Qm) SIMD Vector Pairwise Add and Accumulate Long
  emit(EncodeNeonUnaryOp(NeonU(dt) ? VPADAL_U : VPADAL_S, NEON_Q,
                         NeonDataTypeToSize(dt), dst.code(), src.code()));
}

void Assembler::vpaddl(NeonDataType dt, QwNeonRegister dst,
                       QwNeonRegister src) {
  DCHECK(IsEnabled(NEON));
  // vpaddl.<dt>(Qd, Qm) SIMD Vector Pairwise Add Long.
  emit(EncodeNeonUnaryOp(NeonU(dt) ? VPADDL_U : VPADDL_S, NEON_Q,
                         NeonDataTypeToSize(dt), dst.code(), src.code()));
}

void Assembler::vqrdmulh(NeonDataType dt, QwNeonRegister dst,
                         QwNeonRegister src1, QwNeonRegister src2) {
  DCHECK(IsEnabled(NEON));
  DCHECK(dt == NeonS16 || dt == NeonS32);
  emit(EncodeNeonDataTypeBinOp(VQRDMULH, dt, dst, src1, src2));
}

void Assembler::vcnt(QwNeonRegister dst, QwNeonRegister src) {
  // Qd = vcnt(Qm) SIMD Vector Count Set Bits.
  // Instruction details available at ARM DDI 0487F.b, F6-5094.
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonUnaryOp(VCNT, NEON_Q, Neon8, dst.code(), src.code()));
}

// Encode NEON vtbl / vtbx instruction.
static Instr EncodeNeonVTB(DwVfpRegister dst, const NeonListOperand& list,
                           DwVfpRegister index, bool vtbx) {
  // Dd = vtbl(table, Dm) SIMD vector permute, zero at out of range indices.
  // Instruction details available in ARM DDI 0406C.b, A8-1094.
  // Dd = vtbx(table, Dm) SIMD vector permute, skip out of range indices.
  // Instruction details available in ARM DDI 0406C.b, A8-1094.
  int vd, d;
  dst.split_code(&vd, &d);
  int vn, n;
  list.base().split_code(&vn, &n);
  int vm, m;
  index.split_code(&vm, &m);
  int op = vtbx ? 1 : 0;  // vtbl = 0, vtbx = 1.
  return 0x1E7U * B23 | d * B22 | 0x3 * B20 | vn * B16 | vd * B12 | 0x2 * B10 |
         list.length() * B8 | n * B7 | op * B6 | m * B5 | vm;
}

void Assembler::vtbl(DwVfpRegister dst, const NeonListOperand& list,
                     DwVfpRegister index) {
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonVTB(dst, list, index, false));
}

void Assembler::vtbx(DwVfpRegister dst, const NeonListOperand& list,
                     DwVfpRegister index) {
  DCHECK(IsEnabled(NEON));
  emit(EncodeNeonVTB(dst, list, index, true));
}

// Pseudo instructions.
void Assembler::nop(int type) {
  // ARMv6{K/T2} and v7 have an actual NOP instruction but it serializes
  // some of the CPU's pipeline and has to issue. Older ARM chips simply used
  // MOV Rx, Rx as NOP and it performs better even in newer CPUs.
  // We therefore use MOV Rx, Rx, even on newer CPUs, and use Rx to encode
  // a type.
  DCHECK(0 <= type && type <= 14);  // mov pc, pc isn't a nop.
  emit(al | 13 * B21 | type * B12 | type);
}

void Assembler::pop() { add(sp, sp, Operand(kPointerSize)); }

bool Assembler::IsMovT(Instr instr) {
  instr &= ~(((kNumberOfConditions - 1) << 28) |  // Mask off conditions
             ((kNumRegisters - 1) * B12) |        // mask out register
             EncodeMovwImmediate(0xFFFF));        // mask out immediate value
  return instr == kMovtPattern;
}

bool Assembler::IsMovW(Instr instr) {
  instr &= ~(((kNumberOfConditions - 1) << 28) |  // Mask off conditions
             ((kNumRegisters - 1) * B12) |        // mask out destination
             EncodeMovwImmediate(0xFFFF));        // mask out immediate value
  return instr == kMovwPattern;
}

Instr Assembler::GetMovTPattern() { return kMovtPattern; }

Instr Assembler::GetMovWPattern() { return kMovwPattern; }

Instr Assembler::EncodeMovwImmediate(uint32_t immediate) {
  DCHECK_LT(immediate, 0x10000);
  return ((immediate & 0xF000) << 4) | (immediate & 0xFFF);
}

Instr Assembler::PatchMovwImmediate(Instr instruction, uint32_t immediate) {
  instruction &= ~EncodeMovwImmediate(0xFFFF);
  return instruction | EncodeMovwImmediate(immediate);
}

int Assembler::DecodeShiftImm(Instr instr) {
  int rotate = Instruction::RotateValue(instr) * 2;
  int immed8 = Instruction::Immed8Value(instr);
  return base::bits::RotateRight32(immed8, rotate);
}

Instr Assembler::PatchShiftImm(Instr instr, int immed) {
  uint32_t rotate_imm = 0;
  uint32_t immed_8 = 0;
  bool immed_fits = FitsShifter(immed, &rotate_imm, &immed_8, nullptr);
  DCHECK(immed_fits);
  USE(immed_fits);
  return (instr & ~kOff12Mask) | (rotate_imm << 8) | immed_8;
}

bool Assembler::IsNop(Instr instr, int type) {
  DCHECK(0 <= type && type <= 14);  // mov pc, pc isn't a nop.
  // Check for mov rx, rx where x = type.
  return instr == (al | 13 * B21 | type * B12 | type);
}

bool Assembler::IsMovImmed(Instr instr) {
  return (instr & kMovImmedMask) == kMovImmedPattern;
}

bool Assembler::IsOrrImmed(Instr instr) {
  return (instr & kOrrImmedMask) == kOrrImmedPattern;
}

// static
bool Assembler::ImmediateFitsAddrMode1Instruction(int32_t imm32) {
  uint32_t dummy1;
  uint32_t dummy2;
  return FitsShifter(imm32, &dummy1, &dummy2, nullptr);
}

bool Assembler::ImmediateFitsAddrMode2Instruction(int32_t imm32) {
  return is_uint12(abs(imm32));
}

// Debugging.
void Assembler::RecordConstPool(int size) {
  // We only need this for debugger support, to correctly compute offsets in the
  // code.
  RecordRelocInfo(RelocInfo::CONST_POOL, static_cast<intptr_t>(size));
}

void Assembler::GrowBuffer() {
  DCHECK_EQ(buffer_start_, buffer_->start());

  // Compute new buffer size.
  int old_size = buffer_->size();
  int new_size = std::min(2 * old_size, old_size + 1 * MB);

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
  int pc_delta = new_start - buffer_start_;
  int rc_delta = (new_start + new_size) - (buffer_start_ + old_size);
  size_t reloc_size = (buffer_start_ + old_size) - reloc_info_writer.pos();
  MemMove(new_start, buffer_start_, pc_offset());
  uint8_t* new_reloc_start = reinterpret_cast<uint8_t*>(
      reinterpret_cast<Address>(reloc_info_writer.pos()) + rc_delta);
  MemMove(new_reloc_start, reloc_info_writer.pos(), reloc_size);

  // Switch buffers.
  buffer_ = std::move(new_buffer);
  buffer_start_ = new_start;
  pc_ = reinterpret_cast<uint8_t*>(reinterpret_cast<Address>(pc_) + pc_delta);
  uint8_t* new_last_pc = reinterpret_cast<uint8_t*>(
      reinterpret_cast<Address>(reloc_info_writer.last_pc()) + pc_delta);
  reloc_info_writer.Reposition(new_reloc_start, new_last_pc);

  // None of our relocation types are pc relative pointing outside the code
  // buffer nor pc absolute pointing inside the code buffer, so there is no need
  // to relocate any emitted relocation entries.
}

void Assembler::db(uint8_t data) {
  // db is used to write raw data. The constant pool should be emitted or
  // blocked before using db.
  DCHECK(is_const_pool_blocked() || pending_32_bit_constants_.empty());
  CheckBuffer();
  *reinterpret_cast<uint8_t*>(pc_) = data;
  pc_ += sizeof(uint8_t);
}

void Assembler::dd(uint32_t data) {
  // dd is used to write raw data. The constant pool should be emitted or
  // blocked before using dd.
  DCHECK(is_const_pool_blocked() || pending_32_bit_constants_.empty());
  CheckBuffer();
  base::WriteUnalignedValue(reinterpret_cast<Address>(pc_), data);
  pc_ += sizeof(uint32_t);
}

void Assembler::dq(uint64_t value) {
  // dq is used to write raw data. The constant pool should be emitted or
  // blocked before using dq.
  DCHECK(is_const_pool_blocked() || pending_32_bit_constants_.empty());
  CheckBuffer();
  base::WriteUnalignedValue(reinterpret_cast<Address>(pc_), value);
  pc_ += sizeof(uint64_t);
}

void Assembler::RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data) {
  if (!ShouldRecordRelocInfo(rmode)) return;
  DCHECK_GE(buffer_space(), kMaxRelocSize);  // too late to grow buffer here
  RelocInfo rinfo(reinterpret_cast<Address>(pc_), rmode, data);
  reloc_info_writer.Write(&rinfo);
}

void Assembler::ConstantPoolAddEntry(int position, RelocInfo::Mode rmode,
                                     intptr_t value) {
  DCHECK(rmode != RelocInfo::CONST_POOL);
  // We can share CODE_TARGETs and embedded objects, but we must make sure we
  // only emit one reloc info for them (thus delta patching will apply the delta
  // only once). At the moment, we do not deduplicate heap object request which
  // are indicated by value == 0.
  bool sharing_ok = RelocInfo::IsShareableRelocMode(rmode) ||
                    (rmode == RelocInfo::CODE_TARGET && value != 0) ||
                    (RelocInfo::IsEmbeddedObjectMode(rmode) && value != 0);
  DCHECK_LT(pending_32_bit_constants_.size(), kMaxNumPending32Constants);
  if (first_const_pool_32_use_ < 0) {
    DCHECK(pending_32_bit_constants_.empty());
    DCHECK_EQ(constant_pool_deadline_, kMaxInt);
    first_const_pool_32_use_ = position;
    constant_pool_deadline_ = position + kCheckPoolDeadline;
  } else {
    DCHECK(!pending_32_bit_constants_.empty());
  }
  ConstantPoolEntry entry(position, value, sharing_ok, rmode);

  bool shared = false;
  if (sharing_ok) {
    // Merge the constant, if possible.
    for (size_t i = 0; i < pending_32_bit_constants_.size(); i++) {
      ConstantPoolEntry& current_entry = pending_32_bit_constants_[i];
      if (!current_entry.sharing_ok()) continue;
      if (entry.value() == current_entry.value() &&
          entry.rmode() == current_entry.rmode()) {
        entry.set_merged_index(i);
        shared = true;
        break;
      }
    }
  }

  pending_32_bit_constants_.emplace_back(entry);

  // Make sure the constant pool is not emitted in place of the next
  // instruction for which we just recorded relocation info.
  BlockConstPoolFor(1);

  // Emit relocation info.
  if (MustOutputRelocInfo(rmode, this) && !shared) {
    RecordRelocInfo(rmode);
  }
}

void Assembler::BlockConstPoolFor(int instructions) {
  int pc_limit = pc_offset() + instructions * kInstrSize;
  if (no_const_pool_before_ < pc_limit) {
    no_const_pool_before_ = pc_limit;
  }

  // If we're due a const pool check before the block finishes, move it to just
  // after the block.
  if (constant_pool_deadline_ < no_const_pool_before_) {
    // Make sure that the new deadline isn't too late (including a jump and the
    // constant pool marker).
    DCHECK_LE(no_const_pool_before_,
              first_const_pool_32_use_ + kMaxDistToIntPool);
    constant_pool_deadline_ = no_const_pool_before_;
  }
}

void Assembler::CheckConstPool(bool force_emit, bool require_jump) {
  // Some short sequence of instruction mustn't be broken up by constant pool
  // emission, such sequences are protected by calls to BlockConstPoolFor and
  // BlockConstPoolScope.
  if (is_const_pool_blocked()) {
    // Something is wrong if emission is forced and blocked at the same time.
    DCHECK(!force_emit);
    return;
  }

  // There is nothing to do if there are no pending constant pool entries.
  if (pending_32_bit_constants_.empty()) {
    // We should only fall into this case if we're either trying to forcing
    // emission or opportunistically checking after a jump.
    DCHECK(force_emit || !require_jump);
    return;
  }

  // We emit a constant pool when:
  //  * requested to do so by parameter force_emit (e.g. after each function).
  //  * the distance from the first instruction accessing the constant pool to
  //    the first constant pool entry will exceed its limit the next time the
  //    pool is checked.
  //  * the instruction doesn't require a jump after itself to jump over the
  //    constant pool, and we're getting close to running out of range.
  if (!force_emit) {
    DCHECK_NE(first_const_pool_32_use_, -1);
    int dist32 = pc_offset() - first_const_pool_32_use_;
    if (require_jump) {
      // We should only be on this path if we've exceeded our deadline.
      DCHECK_GE(dist32, kCheckPoolDeadline);
    } else if (dist32 < kCheckPoolDeadline / 2) {
      return;
    }
  }

  int size_after_marker = pending_32_bit_constants_.size() * kPointerSize;

  // Deduplicate constants.
  for (size_t i = 0; i < pending_32_bit_constants_.size(); i++) {
    ConstantPoolEntry& entry = pending_32_bit_constants_[i];
    if (entry.is_merged()) size_after_marker -= kPointerSize;
  }

  // Check that the code buffer is large enough before emitting the constant
  // pool (include the jump over the pool and the constant pool marker and
  // the gap to the relocation information).
  int jump_instr = require_jump ? kInstrSize : 0;
  int size_up_to_marker = jump_instr + kInstrSize;
  int size = size_up_to_marker + size_after_marker;
  int needed_space = size + kGap;
  while (buffer_space() <= needed_space) GrowBuffer();

  {
    ASM_CODE_COMMENT_STRING(this, "Constant Pool");
    // Block recursive calls to CheckConstPool.
    BlockConstPoolScope block_const_pool(this);
    RecordConstPool(size);

    Label size_check;
    bind(&size_check);

    // Emit jump over constant pool if necessary.
    Label after_pool;
    if (require_jump) {
      b(&after_pool);
    }

    // Put down constant pool marker "Undefined instruction".
    // The data size helps disassembly know what to print.
    emit(kConstantPoolMarker |
         EncodeConstantPoolLength(size_after_marker / kPointerSize));

    // The first entry in the constant pool should also be the first
    CHECK_EQ(first_const_pool_32_use_, pending_32_bit_constants_[0].position());
    CHECK(!pending_32_bit_constants_[0].is_merged());

    // Make sure we're not emitting the constant too late.
    CHECK_LE(pc_offset(),
             first_const_pool_32_use_ + kMaxDistToPcRelativeConstant);

    // Check that the code buffer is large enough before emitting the constant
    // pool (this includes the gap to the relocation information).
    int needed_space = pending_32_bit_constants_.size() * kPointerSize + kGap;
    while (buffer_space() <= needed_space) {
      GrowBuffer();
    }

    // Emit 32-bit constant pool entries.
    for (size_t i = 0; i < pending_32_bit_constants_.size(); i++) {
      ConstantPoolEntry& entry = pending_32_bit_constants_[i];
      Instr instr = instr_at(entry.position());

      // 64-bit loads shouldn't get here.
      DCHECK(!IsVldrDPcImmediateOffset(instr));
      DCHECK(!IsMovW(instr));
      DCHECK(IsLdrPcImmediateOffset(instr) &&
             GetLdrRegisterImmediateOffset(instr) == 0);

      int delta = pc_offset() - entry.position() - Instruction::kPcLoadDelta;
      DCHECK(is_uint12(delta));
      // 0 is the smallest delta:
      //   ldr rd, [pc, #0]
      //   constant pool marker
      //   data

      if (entry.is_merged()) {
        DCHECK(entry.sharing_ok());
        ConstantPoolEntry& merged =
            pending_32_bit_constants_[entry.merged_index()];
        DCHECK(entry.value() == merged.value());
        DCHECK_LT(merged.position(), entry.position());
        Instr merged_instr = instr_at(merged.position());
        DCHECK(IsLdrPcImmediateOffset(merged_instr));
        delta = GetLdrRegisterImmediateOffset(merged_instr);
        delta += merged.position() - entry.position();
      }
      instr_at_put(entry.position(),
                   SetLdrRegisterImmediateOffset(instr, delta));
      if (!entry.is_merged()) {
        emit(entry.value());
      }
    }

    pending_32_bit_constants_.clear();

    first_const_pool_32_use_ = -1;

    DCHECK_EQ(size, SizeOfCodeGeneratedSince(&size_check));

    if (after_pool.is_linked()) {
      bind(&after_pool);
    }
  }

  // Since a constant pool was just emitted, we don't need another check until
  // the next constant pool entry is added.
  constant_pool_deadline_ = kMaxInt;
}

PatchingAssembler::PatchingAssembler(const AssemblerOptions& options,
                                     uint8_t* address, int instructions)
    : Assembler(options, ExternalAssemblerBuffer(
                             address, instructions * kInstrSize + kGap)) {
  DCHECK_EQ(reloc_info_writer.pos(), buffer_start_ + buffer_->size());
}

PatchingAssembler::~PatchingAssembler() {
  // Check that we don't have any pending constant pools.
  DCHECK(pending_32_bit_constants_.empty());

  // Check that the code was patched as expected.
  DCHECK_EQ(pc_, buffer_start_ + buffer_->size() - kGap);
  DCHECK_EQ(reloc_info_writer.pos(), buffer_start_ + buffer_->size());
}

void PatchingAssembler::Emit(Address addr) { emit(static_cast<Instr>(addr)); }

void PatchingAssembler::PadWithNops() {
  DCHECK_LE(pc_, buffer_start_ + buffer_->size() - kGap);
  while (pc_ < buffer_start_ + buffer_->size() - kGap) {
    nop();
  }
}

LoadStoreLaneParams::LoadStoreLaneParams(MachineRepresentation rep,
                                         uint8_t laneidx) {
  if (rep == MachineRepresentation::kWord8) {
    *this = LoadStoreLaneParams(laneidx, Neon8, 8);
  } else if (rep == MachineRepresentation::kWord16) {
    *this = LoadStoreLaneParams(laneidx, Neon16, 4);
  } else if (rep == MachineRepresentation::kWord32) {
    *this = LoadStoreLaneParams(laneidx, Neon32, 2);
  } else if (rep == MachineRepresentation::kWord64) {
    *this = LoadStoreLaneParams(laneidx, Neon64, 1);
  } else {
    UNREACHABLE();
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM
