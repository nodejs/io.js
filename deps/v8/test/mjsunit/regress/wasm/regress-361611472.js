// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --allow-natives-syntax
// Flags: --turboshaft-wasm --turboshaft-wasm-instruction-selection-staged

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let struct = builder.addStruct([makeField(kWasmF32, false)]);

builder.addFunction("main", kSig_i_v)
  .addBody([
    kExprRefNull, struct,
    kGCPrefix, kExprStructGet, struct, 0,
    ...wasmF32Const(1.5),
    kExprF32Add,
    kExprI32SConvertF32,
  ]).exportFunc();

const instance = builder.instantiate({});

TestNullDereferenceStackTrace();
%WasmTierUpFunction(instance.exports.main);
TestNullDereferenceStackTrace();

function TestNullDereferenceStackTrace() {
  try {
    instance.exports.main();
    assertUnreachable();
  } catch(e) {
    testStackTrace(e, [
      /RuntimeError: dereferencing a null pointer/,
      /at main \(wasm:\/\/wasm\/[0-9a-f]+:wasm-function\[0\]:0x2a/,
      /at TestNullDereferenceStackTrace .*/,
    ]);
  }
}

function testStackTrace(error, expected) {
  try {
    let stack = error.stack.split("\n");
    assertTrue(stack.length >= expected.length);
    for (let i = 0; i < expected.length; ++i) {
      assertMatches(expected[i], stack[i]);
    }
  } catch(failure) {
    print("Actual stack trace: ", error.stack);
    throw failure;
  }
}
