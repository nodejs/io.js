// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TEST_SIGNATURES_H
#define TEST_SIGNATURES_H

#include "src/codegen/signature.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-opcodes.h"

namespace v8 {
namespace internal {
namespace wasm {

// A helper class with many useful signatures in order to simplify tests.
class TestSignatures {
 public:
  TestSignatures()
      : sig_i_v(1, 0, kIntTypes4),
        sig_i_i(1, 1, kIntTypes4),
        sig_i_ii(1, 2, kIntTypes4),
        sig_i_iii(1, 3, kIntTypes4),
        sig_i_f(1, 1, kIntFloatTypes4),
        sig_i_ff(1, 2, kIntFloatTypes4),
        sig_i_d(1, 1, kIntDoubleTypes4),
        sig_i_dd(1, 2, kIntDoubleTypes4),
        sig_i_a(1, 1, kIntExternRefTypes4),
        sig_i_aa(1, 2, kIntExternRefTypes4),
        sig_i_c(1, 1, kIntFuncRefTypes4),
        sig_i_s(1, 1, kIntSimd128Types4),
        sig_l_v(1, 0, kLongTypes4),
        sig_l_a(1, 1, kLongExternRefTypes4),
        sig_l_c(1, 1, kLongFuncRefTypes4),
        sig_l_l(1, 1, kLongTypes4),
        sig_l_ll(1, 2, kLongTypes4),
        sig_i_ll(1, 2, kIntLongTypes4),
        sig_f_f(1, 1, kFloatTypes4),
        sig_f_ff(1, 2, kFloatTypes4),
        sig_d_d(1, 1, kDoubleTypes4),
        sig_d_dd(1, 2, kDoubleTypes4),
        sig_a_v(1, 0, kExternRefTypes4),
        sig_c_v(1, 0, kFuncTypes4),
        sig_a_a(1, 1, kExternRefTypes4),
        sig_c_c(1, 1, kFuncTypes4),
        sig_v_v(0, 0, kIntTypes4),
        sig_v_i(0, 1, kIntTypes4),
        sig_v_ii(0, 2, kIntTypes4),
        sig_v_iii(0, 3, kIntTypes4),
        sig_v_a(0, 1, kExternRefTypes4),
        sig_v_c(0, 1, kFuncTypes4),
        sig_v_d(0, 1, kDoubleTypes4),
        sig_s_i(1, 1, kSimd128IntTypes4),
        sig_s_s(1, 1, kSimd128Types4),
        sig_s_ss(1, 2, kSimd128Types4),
        sig_ii_v(2, 0, kIntTypes4),
        sig_iii_v(3, 0, kIntTypes4) {
    // I used C++ and you won't believe what happened next....
    for (int i = 0; i < 4; i++) kIntTypes4[i] = kWasmI32;
    for (int i = 0; i < 4; i++) kLongTypes4[i] = kWasmI64;
    for (int i = 0; i < 4; i++) kFloatTypes4[i] = kWasmF32;
    for (int i = 0; i < 4; i++) kDoubleTypes4[i] = kWasmF64;
    for (int i = 0; i < 4; i++) kExternRefTypes4[i] = kWasmExternRef;
    for (int i = 0; i < 4; i++) kFuncTypes4[i] = kWasmFuncRef;
    for (int i = 1; i < 4; i++) kIntLongTypes4[i] = kWasmI64;
    for (int i = 1; i < 4; i++) kIntFloatTypes4[i] = kWasmF32;
    for (int i = 1; i < 4; i++) kIntDoubleTypes4[i] = kWasmF64;
    for (int i = 1; i < 4; i++) kIntExternRefTypes4[i] = kWasmExternRef;
    for (int i = 1; i < 4; i++) kIntFuncRefTypes4[i] = kWasmFuncRef;
    for (int i = 1; i < 4; i++) kLongExternRefTypes4[i] = kWasmExternRef;
    for (int i = 1; i < 4; i++) kLongFuncRefTypes4[i] = kWasmFuncRef;
    for (int i = 0; i < 4; i++) kSimd128Types4[i] = kWasmS128;
    for (int i = 1; i < 4; i++) kIntSimd128Types4[i] = kWasmS128;
    for (int i = 0; i < 4; i++) kSimd128IntTypes4[i] = kWasmS128;
    kIntLongTypes4[0] = kWasmI32;
    kIntFloatTypes4[0] = kWasmI32;
    kIntDoubleTypes4[0] = kWasmI32;
    kIntExternRefTypes4[0] = kWasmI32;
    kIntFuncRefTypes4[0] = kWasmI32;
    kLongExternRefTypes4[0] = kWasmI64;
    kLongFuncRefTypes4[0] = kWasmI64;
    kIntSimd128Types4[0] = kWasmI32;
    kSimd128IntTypes4[1] = kWasmI32;
  }

  FunctionSig* i_v() { return &sig_i_v; }
  FunctionSig* i_i() { return &sig_i_i; }
  FunctionSig* i_ii() { return &sig_i_ii; }
  FunctionSig* i_iii() { return &sig_i_iii; }

  FunctionSig* i_f() { return &sig_i_f; }
  FunctionSig* i_ff() { return &sig_i_ff; }
  FunctionSig* i_d() { return &sig_i_d; }
  FunctionSig* i_dd() { return &sig_i_dd; }

  FunctionSig* l_v() { return &sig_l_v; }
  FunctionSig* l_a() { return &sig_l_a; }
  FunctionSig* l_c() { return &sig_l_c; }
  FunctionSig* l_l() { return &sig_l_l; }
  FunctionSig* l_ll() { return &sig_l_ll; }
  FunctionSig* i_ll() { return &sig_i_ll; }
  FunctionSig* i_a() { return &sig_i_a; }
  FunctionSig* i_aa() { return &sig_i_aa; }
  FunctionSig* i_c() { return &sig_i_c; }
  FunctionSig* i_s() { return &sig_i_s; }

  FunctionSig* f_f() { return &sig_f_f; }
  FunctionSig* f_ff() { return &sig_f_ff; }
  FunctionSig* d_d() { return &sig_d_d; }
  FunctionSig* d_dd() { return &sig_d_dd; }

  FunctionSig* a_v() { return &sig_a_v; }
  FunctionSig* c_v() { return &sig_c_v; }
  FunctionSig* a_a() { return &sig_a_a; }
  FunctionSig* c_c() { return &sig_c_c; }

  FunctionSig* v_v() { return &sig_v_v; }
  FunctionSig* v_i() { return &sig_v_i; }
  FunctionSig* v_ii() { return &sig_v_ii; }
  FunctionSig* v_iii() { return &sig_v_iii; }
  FunctionSig* v_a() { return &sig_v_a; }
  FunctionSig* v_c() { return &sig_v_c; }
  FunctionSig* v_d() { return &sig_v_d; }
  FunctionSig* s_i() { return &sig_s_i; }
  FunctionSig* s_s() { return &sig_s_s; }
  FunctionSig* s_ss() { return &sig_s_ss; }

  FunctionSig* ii_v() { return &sig_ii_v; }
  FunctionSig* iii_v() { return &sig_iii_v; }

  FunctionSig* many(Zone* zone, ValueType ret, ValueType param, int count) {
    FunctionSig::Builder builder(zone, ret == kWasmVoid ? 0 : 1, count);
    if (ret != kWasmVoid) builder.AddReturn(ret);
    for (int i = 0; i < count; i++) {
      builder.AddParam(param);
    }
    return builder.Build();
  }

 private:
  ValueType kIntTypes4[4];
  ValueType kLongTypes4[4];
  ValueType kFloatTypes4[4];
  ValueType kDoubleTypes4[4];
  ValueType kExternRefTypes4[4];
  ValueType kFuncTypes4[4];
  ValueType kIntLongTypes4[4];
  ValueType kIntFloatTypes4[4];
  ValueType kIntDoubleTypes4[4];
  ValueType kIntExternRefTypes4[4];
  ValueType kIntFuncRefTypes4[4];
  ValueType kLongExternRefTypes4[4];
  ValueType kLongFuncRefTypes4[4];
  ValueType kSimd128Types4[4];
  ValueType kIntSimd128Types4[4];
  ValueType kSimd128IntTypes4[4];

  FunctionSig sig_i_v;
  FunctionSig sig_i_i;
  FunctionSig sig_i_ii;
  FunctionSig sig_i_iii;

  FunctionSig sig_i_f;
  FunctionSig sig_i_ff;
  FunctionSig sig_i_d;
  FunctionSig sig_i_dd;
  FunctionSig sig_i_a;
  FunctionSig sig_i_aa;
  FunctionSig sig_i_c;
  FunctionSig sig_i_s;

  FunctionSig sig_l_v;
  FunctionSig sig_l_a;
  FunctionSig sig_l_c;
  FunctionSig sig_l_l;
  FunctionSig sig_l_ll;
  FunctionSig sig_i_ll;

  FunctionSig sig_f_f;
  FunctionSig sig_f_ff;
  FunctionSig sig_d_d;
  FunctionSig sig_d_dd;

  FunctionSig sig_a_v;
  FunctionSig sig_c_v;
  FunctionSig sig_a_a;
  FunctionSig sig_c_c;

  FunctionSig sig_v_v;
  FunctionSig sig_v_i;
  FunctionSig sig_v_ii;
  FunctionSig sig_v_iii;
  FunctionSig sig_v_a;
  FunctionSig sig_v_c;
  FunctionSig sig_v_d;
  FunctionSig sig_s_i;
  FunctionSig sig_s_s;
  FunctionSig sig_s_ss;

  FunctionSig sig_ii_v;
  FunctionSig sig_iii_v;
};
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // TEST_SIGNATURES_H
