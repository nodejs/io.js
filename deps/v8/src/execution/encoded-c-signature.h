// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_ENCODED_C_SIGNATURE_H_
#define V8_EXECUTION_ENCODED_C_SIGNATURE_H_

#include <stdint.h>

namespace v8 {
class CFunctionInfo;

namespace internal {

namespace compiler {
class CallDescriptor;
}  // namespace compiler

// This structure represents whether the parameters for a given function
// should be read from general purpose or FP registers. parameter_count =
// kInvalidParamCount represents "invalid" signature, a placeholder for
// non-existing elements in the mapping.
struct EncodedCSignature {
 public:
  EncodedCSignature() = default;
  EncodedCSignature(uint32_t bitfield, int parameter_count)
      : bitfield_(bitfield), parameter_count_(parameter_count) {}
  explicit EncodedCSignature(int parameter_count)
      : parameter_count_(parameter_count) {}
  explicit EncodedCSignature(const CFunctionInfo* signature);

  bool IsFloat(int index) const {
    return (bitfield_ & (static_cast<uint32_t>(1) << index)) != 0;
  }
  bool IsReturnFloat() const { return IsFloat(kReturnIndex); }
#ifdef V8_TARGET_ARCH_RISCV64
  bool IsReturnFloat64() const {
    return IsFloat(kReturnIndex) && return_type_is_float64_;
  }
#endif
  void SetFloat(int index) { bitfield_ |= (static_cast<uint32_t>(1) << index); }

  void SetReturnFloat64() {
    SetFloat(kReturnIndex);
#ifdef V8_TARGET_ARCH_RISCV64
    return_type_is_float64_ = true;
#endif
  }
  void SetReturnFloat32() {
    SetFloat(kReturnIndex);
#ifdef V8_TARGET_ARCH_RISCV64
    return_type_is_float64_ = false;
#endif
  }

  bool IsValid() const { return parameter_count_ < kInvalidParamCount; }

  int ParameterCount() const { return parameter_count_; }
  int FPParameterCount() const;

  static const EncodedCSignature& Invalid() {
    static EncodedCSignature kInvalid = {0, kInvalidParamCount};
    return kInvalid;
  }

  static const int kReturnIndex = 31;
  static const int kInvalidParamCount = kReturnIndex + 1;

 private:
  // Bit i is set if floating point, unset if not.
  uint32_t bitfield_ = 0;
#ifdef V8_TARGET_ARCH_RISCV64
  // Indicates whether the return type for functions is float64,
  // RISC-V need NaNboxing float32 return value in simulator.
  bool return_type_is_float64_ = false;
#endif  // V8_TARGET_ARCH_RISCV64
  int parameter_count_ = kInvalidParamCount;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_ENCODED_C_SIGNATURE_H_
