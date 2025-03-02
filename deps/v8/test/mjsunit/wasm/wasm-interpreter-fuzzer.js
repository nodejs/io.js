// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --drumbrake-fuzzer-timeout --drumbrake-fuzzer-timeout-limit-ms=10

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Test that the interpreter can handle infinite loops on fuzzer test.
(function testInfiniteLoop() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  let sig_v_v = builder.addType(kSig_v_v);
  builder.addFunction("main", kSig_v_v)
    .addBody([
      kExprBlock, kWasmVoid,
        kExprLoop, kWasmVoid,
        ...wasmI32Const(1),
        kExprBrIf, 0,
        kExprEnd,
      kExprEnd
    ])
    .exportAs("main");
  const instance = builder.instantiate();
  // This should trap with the trap reason kTrapUnreachable.
  assertTraps(kTrapUnreachable, () => instance.exports.main());
})();
