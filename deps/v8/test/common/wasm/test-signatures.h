// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TEST_SIGNATURES_H
#define TEST_SIGNATURES_H

#include "src/codegen/signature.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-opcodes.h"

namespace v8::internal::wasm {

// A helper class with many useful signatures in order to simplify tests.
class TestSignatures {
 public:
#define SIG(name, Maker, ...)              \
  static FunctionSig* name() {             \
    static auto kSig = Maker(__VA_ARGS__); \
    return &kSig;                          \
  }

  // Empty signature.
  static FunctionSig* v_v() {
    static FixedSizeSignature<ValueType, 0, 0> kSig;
    return &kSig;
  }

  // Signatures with no return value.
  SIG(v_i, MakeSigNoReturn, kWasmI32)
  SIG(v_ii, MakeSigNoReturn, kWasmI32, kWasmI32)
  SIG(v_iii, MakeSigNoReturn, kWasmI32, kWasmI32, kWasmI32)
  SIG(v_a, MakeSigNoReturn, kWasmExternRef)
  SIG(v_c, MakeSigNoReturn, kWasmFuncRef)
  SIG(v_d, MakeSigNoReturn, kWasmF64)

  // Returning one i32 value.
  SIG(i_v, MakeSig1Return, kWasmI32)
  SIG(i_i, MakeSig1Return, kWasmI32, kWasmI32)
  SIG(i_ii, MakeSig1Return, kWasmI32, kWasmI32, kWasmI32)
  SIG(i_iii, MakeSig1Return, kWasmI32, kWasmI32, kWasmI32, kWasmI32)
  SIG(i_f, MakeSig1Return, kWasmI32, kWasmF32)
  SIG(i_ff, MakeSig1Return, kWasmI32, kWasmF32, kWasmF32)
  SIG(i_d, MakeSig1Return, kWasmI32, kWasmF64)
  SIG(i_dd, MakeSig1Return, kWasmI32, kWasmF64, kWasmF64)
  SIG(i_ll, MakeSig1Return, kWasmI32, kWasmI64, kWasmI64)
  SIG(i_a, MakeSig1Return, kWasmI32, kWasmExternRef)
  SIG(i_aa, MakeSig1Return, kWasmI32, kWasmExternRef, kWasmExternRef)
  SIG(i_c, MakeSig1Return, kWasmI32, kWasmFuncRef)
  SIG(i_s, MakeSig1Return, kWasmI32, kWasmS128)

  // Returning one i64 value.
  SIG(l_v, MakeSig1Return, kWasmI64)
  SIG(l_a, MakeSig1Return, kWasmI64, kWasmExternRef)
  SIG(l_c, MakeSig1Return, kWasmI64, kWasmFuncRef)
  SIG(l_l, MakeSig1Return, kWasmI64, kWasmI64)
  SIG(l_ll, MakeSig1Return, kWasmI64, kWasmI64, kWasmI64)

  // Returning one f32 value.
  SIG(f_f, MakeSig1Return, kWasmF32, kWasmF32)
  SIG(f_ff, MakeSig1Return, kWasmF32, kWasmF32, kWasmF32)

  // Returning one f64 value.
  SIG(d_d, MakeSig1Return, kWasmF64, kWasmF64)
  SIG(d_dd, MakeSig1Return, kWasmF64, kWasmF64, kWasmF64)

  // Returning other values.
  SIG(a_v, MakeSig1Return, kWasmExternRef)
  SIG(c_v, MakeSig1Return, kWasmFuncRef)
  SIG(a_a, MakeSig1Return, kWasmExternRef, kWasmExternRef)
  SIG(c_c, MakeSig1Return, kWasmFuncRef, kWasmFuncRef)
  SIG(s_i, MakeSig1Return, kWasmS128, kWasmI32)
  SIG(s_s, MakeSig1Return, kWasmS128, kWasmS128)
  SIG(s_ss, MakeSig1Return, kWasmS128, kWasmS128, kWasmS128)

  // Multi-return.
  static FunctionSig* ii_v() {
    static auto kSig =
        FixedSizeSignature<ValueType>::Returns(kWasmI32, kWasmI32);
    return &kSig;
  }
  static FunctionSig* iii_v() {
    static auto kSig =
        FixedSizeSignature<ValueType>::Returns(kWasmI32, kWasmI32, kWasmI32);
    return &kSig;
  }

  static FunctionSig* many(Zone* zone, ValueType ret, ValueType param,
                           int count) {
    FunctionSig::Builder builder(zone, ret == kWasmVoid ? 0 : 1, count);
    if (ret != kWasmVoid) builder.AddReturn(ret);
    for (int i = 0; i < count; i++) {
      builder.AddParam(param);
    }
    return builder.Get();
  }

 private:
  template <typename... ParamTypes>
  static FixedSizeSignature<ValueType, 0, sizeof...(ParamTypes)>
  MakeSigNoReturn(ParamTypes... param_types) {
    return FixedSizeSignature<ValueType>::Params(param_types...);
  }

  template <typename... ParamTypes>
  static FixedSizeSignature<ValueType, 1, sizeof...(ParamTypes)> MakeSig1Return(
      ValueType return_type, ParamTypes... param_types) {
    return FixedSizeSignature<ValueType>::Returns(return_type)
        .Params(param_types...);
  }
};

}  // namespace v8::internal::wasm

#endif  // TEST_SIGNATURES_H
