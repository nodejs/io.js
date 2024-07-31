// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_C_SIGNATURE_H_
#define V8_COMMON_C_SIGNATURE_H_

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
#include "include/v8-fast-api-calls.h"
#endif  // V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS

#include "src/codegen/machine-type.h"

namespace v8 {
namespace internal {
namespace compiler {

#define FOREACH_CTYPE_MACHINE_TYPE_MAPPING(V) \
  V(void, MachineType::None())                \
  V(bool, MachineType::Uint8())               \
  V(int8_t, MachineType::Int8())              \
  V(uint8_t, MachineType::Uint8())            \
  V(int16_t, MachineType::Int16())            \
  V(uint16_t, MachineType::Uint16())          \
  V(int32_t, MachineType::Int32())            \
  V(uint32_t, MachineType::Uint32())          \
  V(int64_t, MachineType::Int64())            \
  V(uint64_t, MachineType::Uint64())          \
  V(float, MachineType::Float32())            \
  V(double, MachineType::Float64())           \
  V(void*, MachineType::Pointer())            \
  V(int*, MachineType::Pointer())

template <typename T>
inline constexpr MachineType MachineTypeForC() {
  static_assert(
      std::is_convertible<T, Tagged<Object>>::value,
      "all non-specialized types must be convertible to Tagged<Object>");
  return MachineType::AnyTagged();
}

#define DECLARE_TEMPLATE_SPECIALIZATION(ctype, mtype)     \
  template <>                                             \
  inline MachineType constexpr MachineTypeForC<ctype>() { \
    return mtype;                                         \
  }
FOREACH_CTYPE_MACHINE_TYPE_MAPPING(DECLARE_TEMPLATE_SPECIALIZATION)
#undef DECLARE_TEMPLATE_SPECIALIZATION

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
template <>
inline MachineType constexpr MachineTypeForC<v8::AnyCType>() {
  return MachineType::Int64();
}
#endif  // V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
// Helper for building machine signatures from C types.
class CSignature : public MachineSignature {
 protected:
  CSignature(size_t return_count, size_t parameter_count, MachineType* reps)
      : MachineSignature(return_count, parameter_count, reps) {}

 public:
  friend Zone;

  template <typename... Params>
  static void VerifyParams(MachineSignature* sig) {
    // Verifies the C signature against the machine types.
    std::array<MachineType, sizeof...(Params)> params{
        {MachineTypeForC<Params>()...}};
    for (size_t p = 0; p < params.size(); ++p) {
      CHECK_EQ(sig->GetParam(p), params[p]);
    }
  }

  static CSignature* FromMachine(Zone* zone, MachineSignature* msig) {
    return reinterpret_cast<CSignature*>(msig);
  }

  template <typename... ParamMachineTypes>
  static CSignature* New(Zone* zone, MachineType ret,
                         ParamMachineTypes... params) {
    constexpr size_t param_count = sizeof...(params);
    std::array<MachineType, param_count> param_arr{{params...}};
    const size_t buffer_size =
        param_count + (ret == MachineType::None() ? 0 : 1);
    MachineType* buffer = zone->AllocateArray<MachineType>(buffer_size);
    size_t pos = 0;
    size_t return_count = 0;
    if (ret != MachineType::None()) {
      buffer[pos++] = ret;
      return_count++;
    }
    for (MachineType p : param_arr) {
      // Check that there are no MachineType::None()'s in the parameters.
      CHECK_NE(MachineType::None(), p);
      buffer[pos++] = p;
    }
    DCHECK_EQ(buffer_size, pos);
    return zone->New<CSignature>(return_count, param_count, buffer);
  }
};

// Helper classes for instantiating Signature objects to be callable from C.
template <typename Ret, typename... Params>
class CSignatureOf : public CSignature {
 public:
  CSignatureOf() : CSignature(kReturnCount, kParamCount, storage_) {
    constexpr std::array<MachineType, kParamCount> param_types{
        MachineTypeForC<Params>()...};
    if (kReturnCount == 1) storage_[0] = MachineTypeForC<Ret>();
    static_assert(
        std::is_same<decltype(*reps_), decltype(*param_types.data())>::value,
        "type mismatch, cannot memcpy");
    if (kParamCount > 0) {
#if V8_CC_GNU
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
#endif
      memcpy(storage_ + kReturnCount, param_types.data(),
             sizeof(*storage_) * kParamCount);
#if V8_CC_GNU
#pragma GCC diagnostic pop
#endif
    }
  }

 private:
  static constexpr size_t kReturnCount =
      MachineTypeForC<Ret>() == MachineType::None() ? 0 : 1;
  static constexpr size_t kParamCount = sizeof...(Params);

  MachineType storage_[kReturnCount + kParamCount];
};

using CSignature_i_ii = CSignatureOf<int32_t, int32_t, int32_t>;
using CSignature_u_uu = CSignatureOf<uint32_t, uint32_t, uint32_t>;
using CSignature_f_ff = CSignatureOf<float, float, float>;
using CSignature_d_dd = CSignatureOf<double, double, double>;
using CSignature_o_oo = CSignatureOf<Object, Object, Object>;

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_C_SIGNATURE_H_
