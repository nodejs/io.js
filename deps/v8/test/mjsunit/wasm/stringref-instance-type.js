// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-stringref --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// This test case tests the loading an instance type for a string before a GC
// is triggered which (in the case of thin strings) changes the instance type.
// Afterwards, the instance type needs to be reloaded.
//
// In Turbofan map and instance type loads in wasm are marked as immutable.
// This is slightly imprecise. Still, due to the limited possibility of changing
// maps resulting in different behavior, this works fine.
// (StringPrepareForGetCodeunit is the only operation that could be affected by
// changing type info and it emits a loop which the instance type floats into
// with another value on the backedge preventing the load elimination.)
(function TestStringInstanceTypeLoad() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let gc_func = builder.addImport("a", "gc", makeSig([], []));

  builder.addFunction("test", makeSig([wasmRefType(kWasmAnyRef)], [kWasmI32]))
    .addLocals(wasmRefType(kWasmStringViewWtf16), 1,)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      // Cast to string. This will load the map and the instance type immutably.
      kGCPrefix, kExprRefCast, kStringRefCode,
      ...GCInstr(kExprStringAsWtf16),
      // Trigger gc.
      kExprCallFunction, gc_func,
      // Access some char on the string.
      kExprI32Const, 4,
      ...GCInstr(kExprStringViewWtf16GetCodeunit),
    ]);
  let instance = builder.instantiate({"a": {gc}});
  assertEquals(52, instance.exports.test(ThinString("a", "1234567")));
  assertEquals(52, instance.exports.test(ConsString("a", "1234567")));

  function ThinString(a, b) {
    var str = a + b;
    var o = {};
    o[str];
    return str;
  }
  function ConsString(a, b) {
    return a + b;
  }
})();
