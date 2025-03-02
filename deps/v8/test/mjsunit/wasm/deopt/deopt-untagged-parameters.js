// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --allow-natives-syntax --wasm-deopt
// Flags: --liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestDeoptWithUntaggedStackSlotsOnly() {
  const builder = new WasmModuleBuilder();
  let calleeSig = builder.addType(kSig_v_v);

  let callee0 = builder.addFunction("c0", kSig_v_v).addBody([]);
  let callee1 = builder.addFunction("c1", kSig_v_v).addBody([]);

  let $table0 = builder.addTable(wasmRefNullType(calleeSig), 2, 2);
  builder.addActiveElementSegment($table0.index, wasmI32Const(0),
    [[kExprRefFunc, callee0.index], [kExprRefFunc, callee1.index]],
    wasmRefNullType(calleeSig));

  // The function uses the first argument as the call target in the table and
  // then adds up all the parameters. The call_ref with the callee is only used
  // to trigger the deopt.
  let mainSig = builder.addType(makeSig([
    kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32],
    [kWasmI32]));
  builder.addFunction("main", mainSig).addBody([
    kExprLocalGet, 0,
    kExprTableGet, $table0.index,
    kExprCallRef, calleeSig,
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprLocalGet, 4,
    kExprLocalGet, 5,
    kExprLocalGet, 6,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
    kExprI32Add,
  ]).exportFunc();

  const instance = builder.instantiate({});
  const wasm = instance.exports;
  assertEquals(0+1+2+3+4+5+6, wasm.main(0, 1, 2, 3, 4, 5, 6));
  %WasmTierUpFunction(wasm.main);
  assertEquals(1+1+2+3+4+5+6, wasm.main(1, 1, 2, 3, 4, 5, 6));
})();
