// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_CPU_FEATURES_H_
#define V8_CODEGEN_CPU_FEATURES_H_

#include "src/common/globals.h"

namespace v8 {

namespace internal {

// CPU feature flags.
enum CpuFeature {
#if V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_X64
  SSE4_2,
  SSE4_1,
  SSSE3,
  SSE3,
  SAHF,
  AVX,
  AVX2,
  FMA3,
  BMI1,
  BMI2,
  LZCNT,
  POPCNT,
  INTEL_ATOM,
  INTEL_JCC_ERRATUM_MITIGATION,
  CETSS,

#elif V8_TARGET_ARCH_ARM
  // - Standard configurations. The baseline is ARMv6+VFPv2.
  ARMv7,        // ARMv7-A + VFPv3-D32 + NEON
  ARMv7_SUDIV,  // ARMv7-A + VFPv4-D32 + NEON + SUDIV
  ARMv8,        // ARMv8-A (+ all of the above)

  // ARM feature aliases (based on the standard configurations above).
  VFPv3 = ARMv7,
  NEON = ARMv7,
  VFP32DREGS = ARMv7,
  SUDIV = ARMv7_SUDIV,

#elif V8_TARGET_ARCH_ARM64
  JSCVT,
  DOTPROD,
  // Large System Extension, include atomic operations on memory: CAS, LDADD,
  // STADD, SWP, etc.
  LSE,

#elif V8_TARGET_ARCH_MIPS64
  FPU,
  FP64FPU,
  MIPSr1,
  MIPSr2,
  MIPSr6,
  MIPS_SIMD,  // MSA instructions

#elif V8_TARGET_ARCH_LOONG64
  FPU,

#elif V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64
  PPC_6_PLUS,
  PPC_7_PLUS,
  PPC_8_PLUS,
  PPC_9_PLUS,
  PPC_10_PLUS,

#elif V8_TARGET_ARCH_S390X
  FPU,
  DISTINCT_OPS,
  GENERAL_INSTR_EXT,
  FLOATING_POINT_EXT,
  VECTOR_FACILITY,
  VECTOR_ENHANCE_FACILITY_1,
  VECTOR_ENHANCE_FACILITY_2,
  MISC_INSTR_EXT2,

#elif V8_TARGET_ARCH_RISCV64 || V8_TARGET_ARCH_RISCV32
  FPU,
  FP64FPU,
  RISCV_SIMD,
  ZBA,
  ZBB,
  ZBS,
#endif

  NUMBER_OF_CPU_FEATURES
};

// CpuFeatures keeps track of which features are supported by the target CPU.
// Supported features must be enabled by a CpuFeatureScope before use.
// Example:
//   if (assembler->IsSupported(SSE3)) {
//     CpuFeatureScope fscope(assembler, SSE3);
//     // Generate code containing SSE3 instructions.
//   } else {
//     // Generate alternative code.
//   }
class V8_EXPORT_PRIVATE CpuFeatures : public AllStatic {
 public:
  CpuFeatures(const CpuFeatures&) = delete;
  CpuFeatures& operator=(const CpuFeatures&) = delete;

  static void Probe(bool cross_compile) {
    static_assert(NUMBER_OF_CPU_FEATURES <= kBitsPerInt);
    if (initialized_) return;
    initialized_ = true;
    ProbeImpl(cross_compile);
  }

  static unsigned SupportedFeatures() {
    Probe(false);
    return supported_;
  }

  static bool IsSupported(CpuFeature f) {
    return (supported_ & (1u << f)) != 0;
  }

  static void SetSupported(CpuFeature f) { supported_ |= 1u << f; }
  static void SetUnsupported(CpuFeature f) { supported_ &= ~(1u << f); }

  static bool SupportsWasmSimd128();

  static inline bool SupportsOptimizer();

  static inline unsigned icache_line_size() {
    DCHECK_NE(icache_line_size_, 0);
    return icache_line_size_;
  }

  static inline unsigned dcache_line_size() {
    DCHECK_NE(dcache_line_size_, 0);
    return dcache_line_size_;
  }

  static void PrintTarget();
  static void PrintFeatures();

 private:
  friend void V8_EXPORT_PRIVATE FlushInstructionCache(void*, size_t);
  friend class ExternalReference;
  // Flush instruction cache.
  static void FlushICache(void* start, size_t size);

  // Platform-dependent implementation.
  static void ProbeImpl(bool cross_compile);

  static unsigned supported_;
  static unsigned icache_line_size_;
  static unsigned dcache_line_size_;
  static bool initialized_;
  // This variable is only used for certain archs to query SupportWasmSimd128()
  // at runtime in builtins using an extern ref. Other callers should use
  // CpuFeatures::SupportWasmSimd128().
  static bool supports_wasm_simd_128_;
  static bool supports_cetss_;
};

}  // namespace internal
}  // namespace v8
#endif  // V8_CODEGEN_CPU_FEATURES_H_
