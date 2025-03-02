// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_FLOAT16_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_FLOAT16_LOWERING_REDUCER_H_

#include "src/base/vector.h"
#include "src/common/globals.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <class Next>
class Float16LoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(Reducer)
  V<Float64OrWord32> REDUCE(Float16Change)(V<Float64OrWord32> input,
                                           Float16ChangeOp::Kind operation) {
    if (operation == Float16ChangeOp::Kind::kToFloat16) {
      if (SupportedOperations::float64_to_float16_raw_bits()) {
        return __ TruncateFloat64ToFloat16RawBits(V<Float64>::Cast(input));
      } else {
#ifdef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
        constexpr bool supports_fp_params_in_c_linkage = Is64();
#else
        constexpr bool supports_fp_params_in_c_linkage = false;
#endif
        if constexpr (supports_fp_params_in_c_linkage) {
          MachineSignature::Builder builder(__ graph_zone(), 1, 1);
          builder.AddReturn(MachineType::Uint32());
          builder.AddParam(MachineType::Float64());
          auto desc =
              Linkage::GetSimplifiedCDescriptor(__ graph_zone(), builder.Get());
          auto ts_desc = TSCallDescriptor::Create(
              desc, CanThrow::kNo, LazyDeoptOnThrow::kNo, __ graph_zone());
          OpIndex ieee754_fp64_to_fp16_raw_bits = __ ExternalConstant(
              ExternalReference::ieee754_fp64_to_fp16_raw_bits());

          V<Word32> result =
              V<Word32>::Cast(__ Call(ieee754_fp64_to_fp16_raw_bits,
                                      {V<Float64>::Cast(input)}, ts_desc));
          return result;
        } else {
          MachineSignature::Builder builder(__ graph_zone(), 1, 2);
          builder.AddReturn(MachineType::Uint32());
          builder.AddParam(MachineType::Uint32());
          builder.AddParam(MachineType::Uint32());
          auto desc =
              Linkage::GetSimplifiedCDescriptor(__ graph_zone(), builder.Get());
          auto ts_desc = TSCallDescriptor::Create(
              desc, CanThrow::kNo, LazyDeoptOnThrow::kNo, __ graph_zone());
          OpIndex ieee754_fp64_raw_bits_to_fp16_raw_bits_for_32bit_arch =
              __ ExternalConstant(
                  ExternalReference::
                      ieee754_fp64_raw_bits_to_fp16_raw_bits_for_32bit_arch());

          V<Float64> float64_input = V<Float64>::Cast(input);

          V<Word32> hi = __ Float64ExtractHighWord32(float64_input);
          V<Word32> lo = __ Float64ExtractLowWord32(float64_input);

          V<Word32> result = V<Word32>::Cast(
              __ Call(ieee754_fp64_raw_bits_to_fp16_raw_bits_for_32bit_arch,
                      {hi, lo}, ts_desc));
          return result;
        }
      }
    }

    if (operation == Float16ChangeOp::Kind::kToFloat64) {
      if (SupportedOperations::float16_raw_bits_to_float64()) {
        return __ ChangeFloat16RawBitsToFloat64(V<Word32>::Cast(input));

      } else {
        MachineSignature::Builder builder(__ graph_zone(), 1, 1);
        builder.AddReturn(MachineType::Uint32());
        builder.AddParam(MachineType::Uint32());
        auto desc =
            Linkage::GetSimplifiedCDescriptor(__ graph_zone(), builder.Get());
        auto ts_desc = TSCallDescriptor::Create(
            desc, CanThrow::kNo, LazyDeoptOnThrow::kNo, __ graph_zone());

        OpIndex ieee754_fp16_raw_bits_to_fp32_raw_bits = __ ExternalConstant(
            ExternalReference::ieee754_fp16_raw_bits_to_fp32_raw_bits());

        V<Float64> result = __ ChangeFloat32ToFloat64(
            __ BitcastWord32ToFloat32(V<Word32>::Cast(__ Call(
                ieee754_fp16_raw_bits_to_fp32_raw_bits, {input}, ts_desc))));

        return result;
      }
    }

    UNREACHABLE();
  }
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"
}  // namespace v8::internal::compiler::turboshaft
#endif  // V8_COMPILER_TURBOSHAFT_FLOAT16_LOWERING_REDUCER_H_
