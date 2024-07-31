// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --stress-compaction
// Flags: --no-always-turbofan --no-always-sparkplug --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestStressCompactionWasmStubCallRelocation() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction('getElementNull', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  const trap = kTrapIllegalCast;
  // getElementNull calls the TrapIllegalCast built-in.
  const getNull = () => wasm.getElementNull(null);

  // Build feedback vector.
  %PrepareFunctionForOptimization(getNull);
  for (let i = 0; i < 10; ++i) {
    assertTraps(trap, getNull)
  }
  // Optimize function causing the wasm function with the built-in to be inlined
  // into the JavaScript TurboFan code. The built-in is encoded as a jump with
  // a 32 bit offset relative to the pc (on some platforms).
  %OptimizeFunctionOnNextCall(getNull);
  assertTraps(trap, getNull)
  assertOptimized(getNull);
  // Force gc(). It is necessary to call gc() multiple times and the flag
  // --stress-compaction has to be set as well to cause a relocation of the JS
  // function.
  gc();
  gc();
  gc();
  // The optimized function is not GCed.
  assertOptimized(getNull);
  // Calling getNull() again calls the relative built-in. If it still succeeds,
  // the jump to the built-in was relocated successfully.
  assertTraps(trap, getNull)
  assertOptimized(getNull);
})();
