// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
builder.addMemory(1, 2, true);

builder.addFunction('store', kSig_v_v).exportFunc().addBody([
  kExprI32Const, 1,                             // address
  ...wasmI32Const(0x1234),                      // value
  kAtomicPrefix, kExprI32AtomicStore16U, 1, 0,  // size_log_2, offset
]);

builder.addFunction('load', kSig_i_v).exportFunc().addBody([
  kExprI32Const, 1,          // address
  kAtomicPrefix, kExprI32AtomicLoad16U, 1, 0,  // size_log_2, offset
]);

builder.addFunction('add', kSig_i_v).exportFunc().addBody([
  kExprI32Const, 1,                           // address
  kExprI32Const, 0,                           // value,
  kAtomicPrefix, kExprI32AtomicAdd16U, 1, 0,  // size_log_2, offset
]);

builder.addFunction('cmpxchg', kSig_i_v).exportFunc().addBody([
  kExprI32Const, 1,                                       // address
  kExprI32Const, 0,                                       // expected value
  kExprI32Const, 0,                                       // new value
  kAtomicPrefix, kExprI32AtomicCompareExchange16U, 1, 0,  // size_log_2, offset
]);

let instance = builder.instantiate();
assertThrows(
    () => instance.exports.store(), WebAssembly.RuntimeError,
    'operation does not support unaligned accesses');

assertThrows(
    () => instance.exports.load(), WebAssembly.RuntimeError,
    'operation does not support unaligned accesses');

assertThrows(
    () => instance.exports.add(), WebAssembly.RuntimeError,
    'operation does not support unaligned accesses');

assertThrows(
    () => instance.exports.cmpxchg(), WebAssembly.RuntimeError,
    'operation does not support unaligned accesses');
