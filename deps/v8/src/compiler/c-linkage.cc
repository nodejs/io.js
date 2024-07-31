// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/compiler/globals.h"
#include "src/compiler/linkage.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

// Platform-specific configuration for C calling convention.
#if V8_TARGET_ARCH_IA32
// ===========================================================================
// == ia32 ===================================================================
// ===========================================================================
#define CALLEE_SAVE_REGISTERS esi, edi, ebx
#define CALLEE_SAVE_FP_REGISTERS

#elif V8_TARGET_ARCH_X64
// ===========================================================================
// == x64 ====================================================================
// ===========================================================================

#ifdef V8_TARGET_OS_WIN
// == x64 windows ============================================================
#define STACK_SHADOW_WORDS 4
#define PARAM_REGISTERS rcx, rdx, r8, r9
#define FP_PARAM_REGISTERS xmm0, xmm1, xmm2, xmm3
#define FP_RETURN_REGISTER xmm0
#define CALLEE_SAVE_REGISTERS rbx, rdi, rsi, r12, r13, r14, r15
#define CALLEE_SAVE_FP_REGISTERS \
  xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15

#else  // V8_TARGET_OS_WIN
// == x64 other ==============================================================
#define PARAM_REGISTERS rdi, rsi, rdx, rcx, r8, r9
#define FP_PARAM_REGISTERS xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7
#define FP_RETURN_REGISTER xmm0
#define CALLEE_SAVE_REGISTERS rbx, r12, r13, r14, r15
#define CALLEE_SAVE_FP_REGISTERS
#endif  // V8_TARGET_OS_WIN

#elif V8_TARGET_ARCH_ARM
// ===========================================================================
// == arm ====================================================================
// ===========================================================================
#define PARAM_REGISTERS r0, r1, r2, r3
#define CALLEE_SAVE_REGISTERS r4, r5, r6, r7, r8, r9, r10
#define CALLEE_SAVE_FP_REGISTERS d8, d9, d10, d11, d12, d13, d14, d15

#elif V8_TARGET_ARCH_ARM64
// ===========================================================================
// == arm64 ====================================================================
// ===========================================================================
#define PARAM_REGISTERS x0, x1, x2, x3, x4, x5, x6, x7
#define FP_PARAM_REGISTERS d0, d1, d2, d3, d4, d5, d6, d7
#define FP_RETURN_REGISTER d0
#define CALLEE_SAVE_REGISTERS x19, x20, x21, x22, x23, x24, x25, x26, x27, x28

#define CALLEE_SAVE_FP_REGISTERS d8, d9, d10, d11, d12, d13, d14, d15

#elif V8_TARGET_ARCH_MIPS64
// ===========================================================================
// == mips64 =================================================================
// ===========================================================================
#define PARAM_REGISTERS a0, a1, a2, a3, a4, a5, a6, a7
#define FP_PARAM_REGISTERS f12, f13, f14, f15, f16, f17, f18, f19
#define FP_RETURN_REGISTER f0
#define CALLEE_SAVE_REGISTERS s0, s1, s2, s3, s4, s5, s6, s7
#define CALLEE_SAVE_FP_REGISTERS f20, f22, f24, f26, f28, f30

#elif V8_TARGET_ARCH_LOONG64
// ===========================================================================
// == loong64 ================================================================
// ===========================================================================
#define PARAM_REGISTERS a0, a1, a2, a3, a4, a5, a6, a7
#define FP_PARAM_REGISTERS f0, f1, f2, f3, f4, f5, f6, f7
#define FP_RETURN_REGISTER f0
#define CALLEE_SAVE_REGISTERS s0, s1, s2, s3, s4, s5, s6, s7, s8, fp
#define CALLEE_SAVE_FP_REGISTERS f24, f25, f26, f27, f28, f29, f30, f31

#elif V8_TARGET_ARCH_PPC64
// ===========================================================================
// == ppc & ppc64 ============================================================
// ===========================================================================
#ifdef V8_TARGET_LITTLE_ENDIAN  // ppc64le linux
#define STACK_SHADOW_WORDS 12
#else  // AIX
#define STACK_SHADOW_WORDS 14
#endif
#define PARAM_REGISTERS r3, r4, r5, r6, r7, r8, r9, r10
#define CALLEE_SAVE_REGISTERS                                                \
  r14, r15, r16, r17, r18, r19, r20, r21, r22, r23, r24, r25, r26, r27, r28, \
      r29, r30

#define CALLEE_SAVE_FP_REGISTERS                                             \
  d14, d15, d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, \
      d29, d30, d31

#elif V8_TARGET_ARCH_S390X
// ===========================================================================
// == s390x ==================================================================
// ===========================================================================
#define STACK_SHADOW_WORDS 20
#define PARAM_REGISTERS r2, r3, r4, r5, r6
#define CALLEE_SAVE_REGISTERS r6, r7, r8, r9, r10, ip, r13
#define CALLEE_SAVE_FP_REGISTERS d8, d9, d10, d11, d12, d13, d14, d15

#elif V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64
// ===========================================================================
// == riscv64 =================================================================
// ===========================================================================
#define PARAM_REGISTERS a0, a1, a2, a3, a4, a5, a6, a7
#define FP_PARAM_REGISTERS fa0, fa1, fa2, fa3, fa4, fa5, fa6, fa7
// fp is not part of CALLEE_SAVE_REGISTERS (similar to how MIPS64 or PPC defines
// it)
#define CALLEE_SAVE_REGISTERS s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11
#define CALLEE_SAVE_FP_REGISTERS \
  fs0, fs1, fs2, fs3, fs4, fs5, fs6, fs7, fs8, fs9, fs10, fs11
#else
// ===========================================================================
// == unknown ================================================================
// ===========================================================================
#define UNSUPPORTED_C_LINKAGE 1
#endif
}  // namespace

#if (defined(V8_TARGET_OS_WIN) && defined(V8_TARGET_ARCH_X64)) || \
    defined(V8_TARGET_ARCH_MIPS64)
// As defined in
// https://docs.microsoft.com/en-us/cpp/build/x64-calling-convention?view=vs-2019#parameter-passing,
// Windows calling convention doesn't differentiate between GP and FP params
// when counting how many of them should be placed in registers. That's why
// we use the same counter {i} for both types here.
// MIPS is the same, as defined in
// https://techpubs.jurassic.nl/manuals/0630/developer/Mpro_n32_ABI/sgi_html/ch02.html#id52620.
void BuildParameterLocations(const MachineSignature* msig,
                             size_t kFPParamRegisterCount,
                             size_t kParamRegisterCount,
                             const DoubleRegister* kFPParamRegisters,
                             const v8::internal::Register* kParamRegisters,
                             LocationSignature::Builder* out_locations) {
#ifdef STACK_SHADOW_WORDS
  int stack_offset = STACK_SHADOW_WORDS;
#else
  int stack_offset = 0;
#endif
  CHECK_EQ(kFPParamRegisterCount, kParamRegisterCount);

  for (size_t i = 0; i < msig->parameter_count(); i++) {
    MachineType type = msig->GetParam(i);
    bool spill = (i >= kParamRegisterCount);
    if (spill) {
      out_locations->AddParam(
          LinkageLocation::ForCallerFrameSlot(-1 - stack_offset, type));
      stack_offset++;
    } else {
      if (IsFloatingPoint(type.representation())) {
        out_locations->AddParam(
            LinkageLocation::ForRegister(kFPParamRegisters[i].code(), type));
      } else {
        out_locations->AddParam(
            LinkageLocation::ForRegister(kParamRegisters[i].code(), type));
      }
    }
  }
}
#elif defined(V8_TARGET_ARCH_LOONG64)
// As defined in
// https://loongson.github.io/LoongArch-Documentation/LoongArch-ELF-ABI-EN.html#_procedure_calling_convention
// Loongarch calling convention uses GP to pass floating-point arguments when no
// FP is available.
void BuildParameterLocations(const MachineSignature* msig,
                             size_t kFPParamRegisterCount,
                             size_t kParamRegisterCount,
                             const DoubleRegister* kFPParamRegisters,
                             const v8::internal::Register* kParamRegisters,
                             LocationSignature::Builder* out_locations) {
#ifdef STACK_SHADOW_WORDS
  int stack_offset = STACK_SHADOW_WORDS;
#else
  int stack_offset = 0;
#endif
  size_t num_params = 0;
  size_t num_fp_params = 0;
  for (size_t i = 0; i < msig->parameter_count(); i++) {
    MachineType type = msig->GetParam(i);
    if (IsFloatingPoint(type.representation())) {
      if (num_fp_params < kFPParamRegisterCount) {
        out_locations->AddParam(LinkageLocation::ForRegister(
            kFPParamRegisters[num_fp_params].code(), type));
        ++num_fp_params;
      } else if (num_params < kParamRegisterCount) {
        // ForNullRegister represents a floating-point param that should be put
        // into the GPR, and reg_code is the the negative of encoding of the
        // GPR, and the maximum is -4.
        out_locations->AddParam(LinkageLocation::ForNullRegister(
            -kParamRegisters[num_params].code(), type));
        ++num_params;
      } else {
        out_locations->AddParam(
            LinkageLocation::ForCallerFrameSlot(-1 - stack_offset, type));
        stack_offset++;
      }
    } else {
      if (num_params < kParamRegisterCount) {
        out_locations->AddParam(LinkageLocation::ForRegister(
            kParamRegisters[num_params].code(), type));
        ++num_params;
      } else {
        out_locations->AddParam(
            LinkageLocation::ForCallerFrameSlot(-1 - stack_offset, type));
        stack_offset++;
      }
    }
  }
}
#else
// As defined in https://www.agner.org/optimize/calling_conventions.pdf,
// Section 7, Linux and Mac place parameters in consecutive registers,
// differentiating between GP and FP params. That's why we maintain two
// separate counters here. This also applies to Arm systems following
// the AAPCS and Windows on Arm.
void BuildParameterLocations(const MachineSignature* msig,
                             size_t kFPParamRegisterCount,
                             size_t kParamRegisterCount,
                             const DoubleRegister* kFPParamRegisters,
                             const v8::internal::Register* kParamRegisters,
                             LocationSignature::Builder* out_locations) {
#ifdef STACK_SHADOW_WORDS
  int stack_offset = STACK_SHADOW_WORDS;
#else
  int stack_offset = 0;
#endif
  size_t num_params = 0;
  size_t num_fp_params = 0;
  for (size_t i = 0; i < msig->parameter_count(); i++) {
    MachineType type = msig->GetParam(i);
    bool spill = IsFloatingPoint(type.representation())
                     ? (num_fp_params >= kFPParamRegisterCount)
                     : (num_params >= kParamRegisterCount);
    if (spill) {
      out_locations->AddParam(
          LinkageLocation::ForCallerFrameSlot(-1 - stack_offset, type));
      stack_offset++;
    } else {
      if (IsFloatingPoint(type.representation())) {
        out_locations->AddParam(LinkageLocation::ForRegister(
            kFPParamRegisters[num_fp_params].code(), type));
        ++num_fp_params;
      } else {
        out_locations->AddParam(LinkageLocation::ForRegister(
            kParamRegisters[num_params].code(), type));
        ++num_params;
      }
    }
  }
}
#endif  // (defined(V8_TARGET_OS_WIN) && defined(V8_TARGET_ARCH_X64)) ||
        // defined(V8_TARGET_ARCH_MIPS64)

// General code uses the above configuration data.
CallDescriptor* Linkage::GetSimplifiedCDescriptor(Zone* zone,
                                                  const MachineSignature* msig,
                                                  CallDescriptor::Flags flags) {
#ifdef UNSUPPORTED_C_LINKAGE
  // This method should not be called on unknown architectures.
  FATAL("requested C call descriptor on unsupported architecture");
  return nullptr;
#endif

  DCHECK_LE(msig->parameter_count(), static_cast<size_t>(kMaxCParameters));

  LocationSignature::Builder locations(zone, msig->return_count(),
                                       msig->parameter_count());

#ifndef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
  // Check the types of the signature.
  for (size_t i = 0; i < msig->parameter_count(); i++) {
    MachineType type = msig->GetParam(i);
    CHECK(!IsFloatingPoint(type.representation()));
  }

  // Check the return types.
  for (size_t i = 0; i < locations.return_count_; i++) {
    MachineType type = msig->GetReturn(i);
    CHECK(!IsFloatingPoint(type.representation()));
  }
#endif

  CHECK_GE(2, locations.return_count_);
  if (locations.return_count_ > 0) {
#ifdef FP_RETURN_REGISTER
    const v8::internal::DoubleRegister kFPReturnRegister = FP_RETURN_REGISTER;
    auto reg = IsFloatingPoint(msig->GetReturn(0).representation())
                   ? kFPReturnRegister.code()
                   : kReturnRegister0.code();
#else
    auto reg = kReturnRegister0.code();
#endif
    // TODO(chromium:1052746): Use the correctly sized register here (e.g. "al"
    // if the return type is kBit), so we don't have to use a hacky bitwise AND
    // elsewhere.
    locations.AddReturn(LinkageLocation::ForRegister(reg, msig->GetReturn(0)));
  }

  if (locations.return_count_ > 1) {
    DCHECK(!IsFloatingPoint(msig->GetReturn(0).representation()));

    locations.AddReturn(LinkageLocation::ForRegister(kReturnRegister1.code(),
                                                     msig->GetReturn(1)));
  }

#ifdef PARAM_REGISTERS
  const v8::internal::Register kParamRegisters[] = {PARAM_REGISTERS};
  const int kParamRegisterCount = static_cast<int>(arraysize(kParamRegisters));
#else
  const v8::internal::Register* kParamRegisters = nullptr;
  const int kParamRegisterCount = 0;
#endif

#ifdef FP_PARAM_REGISTERS
  const DoubleRegister kFPParamRegisters[] = {FP_PARAM_REGISTERS};
  const size_t kFPParamRegisterCount = arraysize(kFPParamRegisters);
#else
  const DoubleRegister* kFPParamRegisters = nullptr;
  const size_t kFPParamRegisterCount = 0;
#endif

  // Add register and/or stack parameter(s).
  BuildParameterLocations(msig, kFPParamRegisterCount, kParamRegisterCount,
                          kFPParamRegisters, kParamRegisters, &locations);

  const RegList kCalleeSaveRegisters = {CALLEE_SAVE_REGISTERS};
  const DoubleRegList kCalleeSaveFPRegisters = {CALLEE_SAVE_FP_REGISTERS};

  // The target for C calls is always an address (i.e. machine pointer).
  MachineType target_type = MachineType::Pointer();
  LinkageLocation target_loc = LinkageLocation::ForAnyRegister(target_type);
  flags |= CallDescriptor::kNoAllocate;

  // TODO(saelo): here we probably want to use a c-call specific tag.
  return zone->New<CallDescriptor>(  // --
      CallDescriptor::kCallAddress,  // kind
      kDefaultCodeEntrypointTag,     // tag
      target_type,                   // target MachineType
      target_loc,                    // target location
      locations.Build(),             // location_sig
      0,                             // stack_parameter_count
      Operator::kNoThrow,            // properties
      kCalleeSaveRegisters,          // callee-saved registers
      kCalleeSaveFPRegisters,        // callee-saved fp regs
      flags, "c-call");
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
