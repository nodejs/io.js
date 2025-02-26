// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/register-configuration.h"

#include "src/base/lazy-instance.h"
#include "src/codegen/cpu-features.h"
#include "src/codegen/register.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

namespace {

#define REGISTER_COUNT(R) 1 +
static const int kMaxAllocatableGeneralRegisterCount =
    ALLOCATABLE_GENERAL_REGISTERS(REGISTER_COUNT) 0;
static const int kMaxAllocatableDoubleRegisterCount =
    ALLOCATABLE_DOUBLE_REGISTERS(REGISTER_COUNT) 0;
#if V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64 || V8_TARGET_ARCH_PPC64
static const int kMaxAllocatableSIMD128RegisterCount =
    ALLOCATABLE_SIMD128_REGISTERS(REGISTER_COUNT) 0;
#endif

static const int kAllocatableGeneralCodes[] = {
#define REGISTER_CODE(R) kRegCode_##R,
    ALLOCATABLE_GENERAL_REGISTERS(REGISTER_CODE)};
#undef REGISTER_CODE

#define REGISTER_CODE(R) kDoubleCode_##R,
static const int kAllocatableDoubleCodes[] = {
    ALLOCATABLE_DOUBLE_REGISTERS(REGISTER_CODE)};
#if V8_TARGET_ARCH_ARM
static const int kAllocatableNoVFP32DoubleCodes[] = {
    ALLOCATABLE_NO_VFP32_DOUBLE_REGISTERS(REGISTER_CODE)};
#endif  // V8_TARGET_ARCH_ARM
#undef REGISTER_CODE

#if V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64 || V8_TARGET_ARCH_PPC64
static const int kAllocatableSIMD128Codes[] = {
#if V8_TARGET_ARCH_RISCV64 || V8_TARGET_ARCH_RISCV32
#define REGISTER_CODE(R) kVRCode_##R,
#else
#define REGISTER_CODE(R) kSimd128Code_##R,
#endif
    ALLOCATABLE_SIMD128_REGISTERS(REGISTER_CODE)};
#undef REGISTER_CODE
#endif  // V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64 ||
        // V8_TARGET_ARCH_PPC64

static_assert(RegisterConfiguration::kMaxGeneralRegisters >=
              Register::kNumRegisters);
static_assert(RegisterConfiguration::kMaxFPRegisters >=
              FloatRegister::kNumRegisters);
static_assert(RegisterConfiguration::kMaxFPRegisters >=
              DoubleRegister::kNumRegisters);
static_assert(RegisterConfiguration::kMaxFPRegisters >=
              Simd128Register::kNumRegisters);
#if V8_TARGET_ARCH_X64
static_assert(RegisterConfiguration::kMaxFPRegisters >=
              Simd256Register::kNumRegisters);
#endif

static int get_num_simd128_registers() {
  return
#if V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64 || V8_TARGET_ARCH_PPC64
      Simd128Register::kNumRegisters;
#else
      0;
#endif  // V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64 ||
        // V8_TARGET_ARCH_PPC64
}

static int get_num_simd256_registers() { return 0; }

// Callers on architectures other than Arm expect this to be be constant
// between build and runtime. Avoid adding variability on other platforms.
static int get_num_allocatable_double_registers() {
  return
#if V8_TARGET_ARCH_IA32
      kMaxAllocatableDoubleRegisterCount;
#elif V8_TARGET_ARCH_X64
      kMaxAllocatableDoubleRegisterCount;
#elif V8_TARGET_ARCH_ARM
      CpuFeatures::IsSupported(VFP32DREGS)
          ? kMaxAllocatableDoubleRegisterCount
          : (ALLOCATABLE_NO_VFP32_DOUBLE_REGISTERS(REGISTER_COUNT) 0);
#elif V8_TARGET_ARCH_ARM64
      kMaxAllocatableDoubleRegisterCount;
#elif V8_TARGET_ARCH_MIPS
      kMaxAllocatableDoubleRegisterCount;
#elif V8_TARGET_ARCH_MIPS64
      kMaxAllocatableDoubleRegisterCount;
#elif V8_TARGET_ARCH_LOONG64
      kMaxAllocatableDoubleRegisterCount;
#elif V8_TARGET_ARCH_PPC64
      kMaxAllocatableDoubleRegisterCount;
#elif V8_TARGET_ARCH_S390X
      kMaxAllocatableDoubleRegisterCount;
#elif V8_TARGET_ARCH_RISCV64
      kMaxAllocatableDoubleRegisterCount;
#elif V8_TARGET_ARCH_RISCV32
      kMaxAllocatableDoubleRegisterCount;
#else
#error Unsupported target architecture.
#endif
}

#undef REGISTER_COUNT

static int get_num_allocatable_simd128_registers() {
  return
#if V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64 || V8_TARGET_ARCH_PPC64
      kMaxAllocatableSIMD128RegisterCount;
#else
      0;
#endif
}

static int get_num_allocatable_simd256_registers() { return 0; }

// Callers on architectures other than Arm expect this to be be constant
// between build and runtime. Avoid adding variability on other platforms.
static const int* get_allocatable_double_codes() {
  return
#if V8_TARGET_ARCH_ARM
      CpuFeatures::IsSupported(VFP32DREGS) ? kAllocatableDoubleCodes
                                           : kAllocatableNoVFP32DoubleCodes;
#else
      kAllocatableDoubleCodes;
#endif
}

static const int* get_allocatable_simd128_codes() {
  return
#if V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64 || V8_TARGET_ARCH_PPC64
      kAllocatableSIMD128Codes;
#else
      kAllocatableDoubleCodes;
#endif
}

class ArchDefaultRegisterConfiguration : public RegisterConfiguration {
 public:
  ArchDefaultRegisterConfiguration()
      : RegisterConfiguration(
            kFPAliasing, Register::kNumRegisters, DoubleRegister::kNumRegisters,
            get_num_simd128_registers(), get_num_simd256_registers(),
            kMaxAllocatableGeneralRegisterCount,
            get_num_allocatable_double_registers(),
            get_num_allocatable_simd128_registers(),
            get_num_allocatable_simd256_registers(), kAllocatableGeneralCodes,
            get_allocatable_double_codes(), get_allocatable_simd128_codes()) {}
};

DEFINE_LAZY_LEAKY_OBJECT_GETTER(ArchDefaultRegisterConfiguration,
                                GetDefaultRegisterConfiguration)

// RestrictedRegisterConfiguration uses the subset of allocatable general
// registers the architecture support, which results into generating assembly
// to use less registers. Currently, it's only used by RecordWrite code stub.
class RestrictedRegisterConfiguration : public RegisterConfiguration {
 public:
  RestrictedRegisterConfiguration(
      int num_allocatable_general_registers,
      std::unique_ptr<int[]> allocatable_general_register_codes,
      std::unique_ptr<char const*[]> allocatable_general_register_names)
      : RegisterConfiguration(
            kFPAliasing, Register::kNumRegisters, DoubleRegister::kNumRegisters,
            get_num_simd128_registers(), get_num_simd256_registers(),
            num_allocatable_general_registers,
            get_num_allocatable_double_registers(),
            get_num_allocatable_simd128_registers(),
            get_num_allocatable_simd256_registers(),
            allocatable_general_register_codes.get(),
            get_allocatable_double_codes(), get_allocatable_simd128_codes()),
        allocatable_general_register_codes_(
            std::move(allocatable_general_register_codes)),
        allocatable_general_register_names_(
            std::move(allocatable_general_register_names)) {
    for (int i = 0; i < num_allocatable_general_registers; ++i) {
      DCHECK(
          IsAllocatableGeneralRegister(allocatable_general_register_codes_[i]));
    }
  }

  bool IsAllocatableGeneralRegister(int code) {
    for (int i = 0; i < kMaxAllocatableGeneralRegisterCount; ++i) {
      if (code == kAllocatableGeneralCodes[i]) {
        return true;
      }
    }
    return false;
  }

 private:
  std::unique_ptr<int[]> allocatable_general_register_codes_;
  std::unique_ptr<char const*[]> allocatable_general_register_names_;
};

}  // namespace

const RegisterConfiguration* RegisterConfiguration::Default() {
  return GetDefaultRegisterConfiguration();
}

const RegisterConfiguration* RegisterConfiguration::RestrictGeneralRegisters(
    RegList registers) {
  int num = registers.Count();
  std::unique_ptr<int[]> codes{new int[num]};
  std::unique_ptr<char const* []> names { new char const*[num] };
  int counter = 0;
  for (int i = 0; i < Default()->num_allocatable_general_registers(); ++i) {
    auto reg = Register::from_code(Default()->GetAllocatableGeneralCode(i));
    if (registers.has(reg)) {
      DCHECK(counter < num);
      codes[counter] = reg.code();
      names[counter] = RegisterName(Register::from_code(i));
      counter++;
    }
  }

  return new RestrictedRegisterConfiguration(num, std::move(codes),
                                             std::move(names));
}

RegisterConfiguration::RegisterConfiguration(
    AliasingKind fp_aliasing_kind, int num_general_registers,
    int num_double_registers, int num_simd128_registers,
    int num_simd256_registers, int num_allocatable_general_registers,
    int num_allocatable_double_registers, int num_allocatable_simd128_registers,
    int num_allocatable_simd256_registers, const int* allocatable_general_codes,
    const int* allocatable_double_codes,
    const int* independent_allocatable_simd128_codes)
    : num_general_registers_(num_general_registers),
      num_float_registers_(0),
      num_double_registers_(num_double_registers),
      num_simd128_registers_(num_simd128_registers),
      num_simd256_registers_(num_simd256_registers),
      num_allocatable_general_registers_(num_allocatable_general_registers),
      num_allocatable_float_registers_(0),
      num_allocatable_double_registers_(num_allocatable_double_registers),
      num_allocatable_simd128_registers_(num_allocatable_simd128_registers),
      num_allocatable_simd256_registers_(num_allocatable_simd256_registers),
      allocatable_general_codes_mask_(0),
      allocatable_float_codes_mask_(0),
      allocatable_double_codes_mask_(0),
      allocatable_simd128_codes_mask_(0),
      allocatable_simd256_codes_mask_(0),
      allocatable_general_codes_(allocatable_general_codes),
      allocatable_double_codes_(allocatable_double_codes),
      fp_aliasing_kind_(fp_aliasing_kind) {
  DCHECK_LE(num_general_registers_,
            RegisterConfiguration::kMaxGeneralRegisters);
  DCHECK_LE(num_double_registers_, RegisterConfiguration::kMaxFPRegisters);
  for (int i = 0; i < num_allocatable_general_registers_; ++i) {
    allocatable_general_codes_mask_ |= (1 << allocatable_general_codes_[i]);
  }
  for (int i = 0; i < num_allocatable_double_registers_; ++i) {
    allocatable_double_codes_mask_ |= (1 << allocatable_double_codes_[i]);
  }

  if (fp_aliasing_kind_ == AliasingKind::kCombine) {
    num_float_registers_ = num_double_registers_ * 2 <= kMaxFPRegisters
                               ? num_double_registers_ * 2
                               : kMaxFPRegisters;
    num_allocatable_float_registers_ = 0;
    for (int i = 0; i < num_allocatable_double_registers_; i++) {
      int base_code = allocatable_double_codes_[i] * 2;
      if (base_code >= kMaxFPRegisters) continue;
      allocatable_float_codes_[num_allocatable_float_registers_++] = base_code;
      allocatable_float_codes_[num_allocatable_float_registers_++] =
          base_code + 1;
      allocatable_float_codes_mask_ |= (0x3 << base_code);
    }
    num_simd128_registers_ = num_double_registers_ / 2;
    num_allocatable_simd128_registers_ = 0;
    int last_simd128_code = allocatable_double_codes_[0] / 2;
    for (int i = 1; i < num_allocatable_double_registers_; i++) {
      int next_simd128_code = allocatable_double_codes_[i] / 2;
      // This scheme assumes allocatable_double_codes_ are strictly increasing.
      DCHECK_GE(next_simd128_code, last_simd128_code);
      if (last_simd128_code == next_simd128_code) {
        allocatable_simd128_codes_[num_allocatable_simd128_registers_++] =
            next_simd128_code;
        allocatable_simd128_codes_mask_ |= (0x1 << next_simd128_code);
      }
      last_simd128_code = next_simd128_code;
    }
  } else if (fp_aliasing_kind_ == AliasingKind::kOverlap) {
    num_float_registers_ = num_simd128_registers_ = num_double_registers_;
    num_allocatable_float_registers_ = num_allocatable_simd128_registers_ =
        num_allocatable_double_registers_;
    for (int i = 0; i < num_allocatable_float_registers_; ++i) {
      allocatable_float_codes_[i] = allocatable_simd128_codes_[i] =
          allocatable_double_codes_[i];
#if V8_TARGET_ARCH_X64
      allocatable_simd256_codes_[i] = allocatable_double_codes_[i];
#endif
    }
    allocatable_float_codes_mask_ = allocatable_simd128_codes_mask_ =
        allocatable_double_codes_mask_;
#if V8_TARGET_ARCH_X64
    num_simd256_registers_ = num_double_registers_;
    num_allocatable_simd256_registers_ = num_allocatable_double_registers_;
    allocatable_simd256_codes_mask_ = allocatable_double_codes_mask_;
#endif
  } else {
    DCHECK_EQ(fp_aliasing_kind_, AliasingKind::kIndependent);
    DCHECK_NE(independent_allocatable_simd128_codes, nullptr);
    num_float_registers_ = num_double_registers_;
    num_allocatable_float_registers_ = num_allocatable_double_registers_;
    for (int i = 0; i < num_allocatable_float_registers_; ++i) {
      allocatable_float_codes_[i] = allocatable_double_codes_[i];
    }
    allocatable_float_codes_mask_ = allocatable_double_codes_mask_;
    for (int i = 0; i < num_allocatable_simd128_registers; i++) {
      allocatable_simd128_codes_[i] = independent_allocatable_simd128_codes[i];
    }
    for (int i = 0; i < num_allocatable_simd128_registers_; ++i) {
      allocatable_simd128_codes_mask_ |= (1 << allocatable_simd128_codes_[i]);
    }
  }
}

// Assert that kFloat32, kFloat64, kSimd128 and kSimd256 are consecutive values.
static_assert(static_cast<int>(MachineRepresentation::kSimd256) ==
              static_cast<int>(MachineRepresentation::kSimd128) + 1);
static_assert(static_cast<int>(MachineRepresentation::kSimd128) ==
              static_cast<int>(MachineRepresentation::kFloat64) + 1);
static_assert(static_cast<int>(MachineRepresentation::kFloat64) ==
              static_cast<int>(MachineRepresentation::kFloat32) + 1);

int RegisterConfiguration::GetAliases(MachineRepresentation rep, int index,
                                      MachineRepresentation other_rep,
                                      int* alias_base_index) const {
  DCHECK(fp_aliasing_kind_ == AliasingKind::kCombine);
  DCHECK(IsFloatingPoint(rep) && IsFloatingPoint(other_rep));
  if (rep == other_rep) {
    *alias_base_index = index;
    return 1;
  }
  int rep_int = static_cast<int>(rep);
  int other_rep_int = static_cast<int>(other_rep);
  if (rep_int > other_rep_int) {
    int shift = rep_int - other_rep_int;
    int base_index = index << shift;
    if (base_index >= kMaxFPRegisters) {
      // Alias indices would be out of FP register range.
      return 0;
    }
    *alias_base_index = base_index;
    return 1 << shift;
  }
  int shift = other_rep_int - rep_int;
  *alias_base_index = index >> shift;
  return 1;
}

bool RegisterConfiguration::AreAliases(MachineRepresentation rep, int index,
                                       MachineRepresentation other_rep,
                                       int other_index) const {
  DCHECK(fp_aliasing_kind_ == AliasingKind::kCombine);
  DCHECK(IsFloatingPoint(rep) && IsFloatingPoint(other_rep));
  if (rep == other_rep) {
    return index == other_index;
  }
  int rep_int = static_cast<int>(rep);
  int other_rep_int = static_cast<int>(other_rep);
  if (rep_int > other_rep_int) {
    int shift = rep_int - other_rep_int;
    return index == other_index >> shift;
  }
  int shift = other_rep_int - rep_int;
  return index >> shift == other_index;
}

}  // namespace internal
}  // namespace v8
