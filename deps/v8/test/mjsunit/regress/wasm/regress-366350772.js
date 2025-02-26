// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --wasm-debug-mask-for-testing=255
// Flags: --liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestDebugMaskForTesting() {
  const builder = new WasmModuleBuilder();
  let calleeSig = builder.addType(makeSig([], [kWasmI32]));
  let mainSig = builder.addType(makeSig([wasmRefType(calleeSig)], [kWasmI32]));
  builder.addFunction("callee_0", calleeSig).exportFunc()
    .addBody([kExprI32Const, 42]);
  builder.addFunction("main", mainSig).exportFunc()
    .addBody([kExprLocalGet, 0, kExprCallRef, calleeSig]);
  const instance = builder.instantiate();
  instance.exports.main(instance.exports.callee_0);
  %WasmTierUpFunction(instance.exports.main);
  const instance2 = builder.instantiate();
  instance2.exports.main(instance.exports.callee_0);
})();
