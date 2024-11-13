// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_REGISTER_CONFIGURATION_H_
#define V8_CODEGEN_REGISTER_CONFIGURATION_H_

#include "src/base/macros.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/reglist.h"
#include "src/common/globals.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

class V8_EXPORT_PRIVATE RegisterConfiguration {
 public:
  // Architecture independent maxes.
  static constexpr int kMaxGeneralRegisters = 32;
  static constexpr int kMaxFPRegisters = 32;
  static constexpr int kMaxRegisters =
      std::max(kMaxFPRegisters, kMaxGeneralRegisters);

  // Default RegisterConfigurations for the target architecture.
  static const RegisterConfiguration* Default();

  // Register configuration with reserved masking register.
  static const RegisterConfiguration* Poisoning();

  static const RegisterConfiguration* RestrictGeneralRegisters(
      RegList registers);

  RegisterConfiguration(
      AliasingKind fp_aliasing_kind, int num_general_registers,
      int num_double_registers, int num_simd128_registers,
      int num_simd256_registers, int num_allocatable_general_registers,
      int num_allocatable_double_registers,
      int num_allocatable_simd128_registers,
      int num_allocatable_simd256_registers,
      const int* allocatable_general_codes, const int* allocatable_double_codes,
      const int* independent_allocatable_simd128_codes = nullptr);

  int num_general_registers() const { return num_general_registers_; }
  int num_float_registers() const { return num_float_registers_; }
  int num_double_registers() const { return num_double_registers_; }
  int num_simd128_registers() const { return num_simd128_registers_; }
  int num_simd256_registers() const { return num_simd256_registers_; }
  int num_allocatable_general_registers() const {
    return num_allocatable_general_registers_;
  }
  int num_allocatable_float_registers() const {
    return num_allocatable_float_registers_;
  }
  // Caution: this value depends on the current cpu and may change between
  // build and runtime. At the time of writing, the only architecture with a
  // variable allocatable double register set is Arm.
  int num_allocatable_double_registers() const {
    return num_allocatable_double_registers_;
  }
  int num_allocatable_simd128_registers() const {
    return num_allocatable_simd128_registers_;
  }
  int num_allocatable_simd256_registers() const {
    return num_allocatable_simd256_registers_;
  }

  AliasingKind fp_aliasing_kind() const { return fp_aliasing_kind_; }
  int32_t allocatable_general_codes_mask() const {
    return allocatable_general_codes_mask_;
  }
  int32_t allocatable_double_codes_mask() const {
    return allocatable_double_codes_mask_;
  }
  int32_t allocatable_float_codes_mask() const {
    return allocatable_float_codes_mask_;
  }
  int32_t allocatable_simd128_codes_mask() const {
    return allocatable_simd128_codes_mask_;
  }
  int GetAllocatableGeneralCode(int index) const {
    DCHECK(index >= 0 && index < num_allocatable_general_registers());
    return allocatable_general_codes_[index];
  }
  bool IsAllocatableGeneralCode(int index) const {
    return ((1 << index) & allocatable_general_codes_mask_) != 0;
  }
  int GetAllocatableFloatCode(int index) const {
    DCHECK(index >= 0 && index < num_allocatable_float_registers());
    return allocatable_float_codes_[index];
  }
  bool IsAllocatableFloatCode(int index) const {
    return ((1 << index) & allocatable_float_codes_mask_) != 0;
  }
  int GetAllocatableDoubleCode(int index) const {
    DCHECK(index >= 0 && index < num_allocatable_double_registers());
    return allocatable_double_codes_[index];
  }
  bool IsAllocatableDoubleCode(int index) const {
    return ((1 << index) & allocatable_double_codes_mask_) != 0;
  }
  int GetAllocatableSimd128Code(int index) const {
    DCHECK(index >= 0 && index < num_allocatable_simd128_registers());
    return allocatable_simd128_codes_[index];
  }
  bool IsAllocatableSimd128Code(int index) const {
    return ((1 << index) & allocatable_simd128_codes_mask_) != 0;
  }
  int GetAllocatableSimd256Code(int index) const {
    DCHECK(index >= 0 && index < num_allocatable_simd256_registers());
    return allocatable_simd256_codes_[index];
  }
  bool IsAllocatableSimd256Code(int index) const {
    return ((1 << index) & allocatable_simd256_codes_mask_) != 0;
  }

  const int* allocatable_general_codes() const {
    return allocatable_general_codes_;
  }
  const int* allocatable_float_codes() const {
    return allocatable_float_codes_;
  }
  const int* allocatable_double_codes() const {
    return allocatable_double_codes_;
  }
  const int* allocatable_simd128_codes() const {
    return allocatable_simd128_codes_;
  }
  const int* allocatable_simd256_codes() const {
    return allocatable_simd256_codes_;
  }

  // Aliasing calculations for floating point registers, when fp_aliasing_kind()
  // is COMBINE. Currently only implemented for kFloat32, kFloat64, or kSimd128
  // reps. Returns the number of aliases, and if > 0, alias_base_index is set to
  // the index of the first alias.
  int GetAliases(MachineRepresentation rep, int index,
                 MachineRepresentation other_rep, int* alias_base_index) const;
  // Returns a value indicating whether two registers alias each other, when
  // fp_aliasing_kind() is COMBINE. Currently implemented for kFloat32,
  // kFloat64, or kSimd128 reps.
  bool AreAliases(MachineRepresentation rep, int index,
                  MachineRepresentation other_rep, int other_index) const;

  virtual ~RegisterConfiguration() = default;

 private:
  const int num_general_registers_;
  int num_float_registers_;
  const int num_double_registers_;
  int num_simd128_registers_;
  int num_simd256_registers_;
  int num_allocatable_general_registers_;
  int num_allocatable_float_registers_;
  int num_allocatable_double_registers_;
  int num_allocatable_simd128_registers_;
  int num_allocatable_simd256_registers_;
  int32_t allocatable_general_codes_mask_;
  int32_t allocatable_float_codes_mask_;
  int32_t allocatable_double_codes_mask_;
  int32_t allocatable_simd128_codes_mask_;
  int32_t allocatable_simd256_codes_mask_;
  const int* allocatable_general_codes_;
  int allocatable_float_codes_[kMaxFPRegisters];
  const int* allocatable_double_codes_;
  int allocatable_simd128_codes_[kMaxFPRegisters];
  int allocatable_simd256_codes_[kMaxFPRegisters];
  AliasingKind fp_aliasing_kind_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_REGISTER_CONFIGURATION_H_
