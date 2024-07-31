// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-generic-wrapper --expose-gc --allow-natives-syntax
// Flags: --experimental-wasm-jspi

// This is a port of the generic-wrapper.js tests for the JS Promise Integration
// variant of the wrapper. We don't suspend the stacks in this test, we only
// test the wrapper's argument conversion logic.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function GC() {
  %CheckIsOnCentralStack();
  gc();
}

(function testGenericWrapper0Param() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_r_r);
  let func_index = builder.addImport("mod", "func", kSig_r_v);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12;
  function import_func() {
    GC();
    x = 20;
  }

  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(main(), v => assertEquals(undefined, v));
  assertEquals(20, x);
})();

(function testGenericWrapper0ParamTraps() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_r_r);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprUnreachable
    ])
    .exportFunc();

  let instance = builder.instantiate();
  let main = ToPromising(instance.exports.main);
  assertThrowsAsync(main(), WebAssembly.RuntimeError);
})();

(function testGenericWrapper1ParamTrap() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef, kWasmI32], [kWasmExternRef]);
  let sig_index = builder.addType(sig);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 1, kExprUnreachable
    ])
    .exportFunc();

  let instance = builder.instantiate();
  let main = ToPromising(instance.exports.main);
  assertThrowsAsync(main(), WebAssembly.RuntimeError);
})();

(function testGenericWrapper1ParamGeneral() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef, kWasmI32], [kWasmExternRef]);
  let sig_index = builder.addType(sig);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12;
  function import_func(suspender, param) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += param;
  }

  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(main(5), v => { assertEquals(undefined, v); });
  assertEquals(17, x);
})();

(function testGenericWrapper1ParamNotSmi() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef, kWasmI32], [kWasmExternRef]);
  let sig_index = builder.addType(sig);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12;
  function import_func(suspender, param) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += param;
  }

  let y = { valueOf: () => { print("Hello!"); GC(); return 24; } };
  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(main(y), v => { assertEquals(undefined, v); });
  assertEquals(36, x);
})();

(function testGenericWrapper4Param() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI32, kWasmI32],
      [kWasmExternRef]);
  let sig_index = builder.addType(sig);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprLocalGet, 3,
      kExprLocalGet, 4,
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12;
  function import_func(suspender, param1, param2, param3, param4) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += 2 * param1 + 3 * param2 + 4 * param3 + 5 * param4;
  }

  let param2 = { valueOf: () => { GC(); return 6; } };
  let param3 = { valueOf: () => { GC(); return 3; } };
  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(
      main(9, param2, param3, 0),
      v => { assertEquals(undefined, v); });
  assertEquals(60, x);
  // Now we test if the evaluation order of the parameters is correct.
  x = 12;
  param3 = {
    valueOf: () => {
      Object.defineProperty(param2, 'valueOf', {
        value: () => 30
      })
      return 3;
    }
  };
  assertPromiseResult(main(9, param2, param3, 0), undefined);
  assertEquals(60, x);
})();

let kSig_r_riiiiiiii = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI32,
    kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32], [kWasmExternRef]);

(function testGenericWrapper8Param() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_r_riiiiiiii);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprLocalGet, 3,
      kExprLocalGet, 4,
      kExprLocalGet, 5,
      kExprLocalGet, 6,
      kExprLocalGet, 7,
      kExprLocalGet, 8,
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12;
  function import_func(suspender, param1, param2, param3, param4, param5,
      param6, param7, param8) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += param1 + 2 * param2 + 3 * param3 + 4 * param4 + 5 * param5
      + 6 * param6 + 7 * param7 + 8 * param8;
  }

  let param1 = { valueOf: () => { GC(); return 5; } };
  let param4 = { valueOf: () => { GC(); return 8; } };
  let param6 = { valueOf: () => { GC(); return 10; } };
  let param8 = { valueOf: () => { GC(); return 12; } };
  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(
      main(param1, 6, 7, param4, 9, param6, 11, param8),
      v => assertEquals(undefined, v));
  assertEquals(360, x);
})();

// Passing less parameters than expected.
(function testGenericWrapper4ParamWithLessParams() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI32, kWasmI32],
      [kWasmExternRef]);
  let sig_index = builder.addType(sig);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprLocalGet, 3,
      kExprLocalGet, 4,
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12;
  function import_func(suspender, param1, param2, param3, param4) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += param1 + param2 + param3 + param4;
  }

  let param2 = { valueOf: () => { GC(); return 3; } };
  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(main(5, param2), v => assertEquals(undefined, v));
  assertEquals(20, x);
})();

// Passing more parameters than expected.
(function testGenericWrapper4ParamWithMoreParams() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI32, kWasmI32],
      [kWasmExternRef]);
  let sig_index = builder.addType(sig);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprLocalGet, 3,
      kExprLocalGet, 4,
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12;
  function import_func(suspender, param1, param2, param3, param4) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += param1 + param2 + param3 + param4;
  }

  let param2 = { valueOf: () => { GC(); return 3; } };
  let param3 = { valueOf: () => { GC(); return 6; } };
  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(
      main(5, param2, param3, 7, 200, 300, 400),
      v => assertEquals(undefined, v));
  assertEquals(33, x);
})();

(function testGenericWrapper1I32ReturnSmi() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef, kWasmI32], [kWasmI32]);
  let sig_index = builder.addType(sig);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12;
  function import_func(suspender, param) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    return x + param;
  }

  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(main(5), v => assertEquals(17, v));
})();

(function testGenericWrapper1I32ReturnHeapNumber() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef, kWasmI32], [kWasmI32]);
  let sig_index = builder.addType(sig);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 2147483640;
  function import_func(suspender, param) {
    assertInstanceof(suspender, WebAssembly.Suspender);
    let result = x + param;
    %SimulateNewspaceFull();
    return result;
  }

  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(main(5), v => assertEquals(2147483645, v));
})();

let kSig_i_rlili = makeSig([kWasmExternRef, kWasmI64, kWasmI32, kWasmI64, kWasmI32],
    [kWasmI32]);

(function testGenericWrapper4IParam1I32Ret() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_i_rlili);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprLocalGet, 3,
      kExprLocalGet, 4,
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12n;
  function import_func(suspender, param1, param2, param3, param4) {
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += 2n * param1 + BigInt(3 * param2) + 4n * param3 + BigInt(5 * param4);
    return Number(x);
  }

  let param2 = { valueOf: () => { GC(); return 6; } };
  let param3 = { valueOf: () => { GC(); return 3n; } };
  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(main(9n, param2, param3, 0), v => assertEquals(60, v));
})();

let kSig_r_riiili = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI32, kWasmI64,
  kWasmI32], [kWasmExternRef]);

(function testGenericWrapper5IParam() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_r_riiili);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprLocalGet, 3,
      kExprLocalGet, 4,
      kExprLocalGet, 5,
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12;
  function import_func(suspender, param1, param2, param3, param4, param5) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += 2 * param1 + 3 * param2 + 4 * param3 + 5 * Number(param4) + 6 * param5;
  }

  let param2 = { valueOf: () => { GC(); return 6; } };
  let param3 = { valueOf: () => { GC(); return 3; } };
  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(
      main(9, param2, param3, 0n, 2),
      v => assertEquals(undefined, v));
  assertEquals(72, x);
})();

let kSig_r_riiilii = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI32,
    kWasmI64, kWasmI32, kWasmI32], [kWasmExternRef]);

(function testGenericWrapper6IParam() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_r_riiilii);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprLocalGet, 3,
      kExprLocalGet, 4,
      kExprLocalGet, 5,
      kExprLocalGet, 6,
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12;
  function import_func(suspender, param1, param2, param3, param4, param5,
      param6) {
    gc();
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += 2 * param1 + 3 * param2 + 4 * param3 + 5 * Number(param4) + 6 * param5 + 7 * param6;
  }

  let param2 = { valueOf: () => { GC(); return 6; } };
  let param3 = { valueOf: () => { GC(); return 3; } };
  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(
      main(9, param2, param3, 0n, 2, 3),
      v => assertEquals(undefined, v));
  assertEquals(93, x);
})();

let kSig_r_rliilliiil = makeSig([kWasmExternRef, kWasmI64, kWasmI32, kWasmI32,
    kWasmI64, kWasmI64, kWasmI32, kWasmI32, kWasmI32, kWasmI64], [kWasmI32]);

(function testGenericWrapper9IParam132Ret() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_r_rliilliiil);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprLocalGet, 3,
      kExprLocalGet, 4,
      kExprLocalGet, 5,
      kExprLocalGet, 6,
      kExprLocalGet, 7,
      kExprLocalGet, 8,
      kExprLocalGet, 9,
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12;
  function import_func(suspender, param1, param2, param3, param4, param5,
      param6, param7, param8, param9) {
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += Number(param1) + 2 * param2 + 3 * param3 + Number(4n * param4) + Number(5n * param5)
      + 6 * param6 + 7 * param7 + 8 * param8 + Number(9n * param9);
    return x;
  }

  let param1 = { valueOf: () => { GC(); return 5n; } };
  let param4 = { valueOf: () => { GC(); return 8n; } };
  let param6 = { valueOf: () => { GC(); return 10; } };
  let param8 = { valueOf: () => { GC(); return 12; } };
  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(
      main(param1, 6, 7, param4, 9n, param6, 11, param8, 0n),
      v => assertEquals(360, v));
})();

// The function expects BigInt, but gets Number.
(function testGenericWrapperTypeError() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef, kWasmI64], [kWasmExternRef]);
  let sig_index = builder.addType(sig);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12n;
  function import_func(suspender, param1) {
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += param1;
  }

  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertThrows(() => main(17), TypeError);
})();

(function testGenericWrapper1I64Return() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef], [kWasmI64]);
  let sig_index = builder.addType(sig);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
        kExprLocalGet, 0,
        kExprCallFunction, func_index
    ])
    .exportFunc();

  function import_func(suspender) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    return 10000000000n;
  }

  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(main(), v => assertEquals(10000000000n, v));
})();

(function testGenericWrapper1F32Return() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef], [kWasmF32]);
  let sig_index = builder.addType(sig);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
        kExprLocalGet, 0,
        kExprCallFunction, func_index
    ])
    .exportFunc();

  function import_func(suspender) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    return 0.5;
  }

  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(main(), v => assertEquals(0.5, v));
})();

(function testGenericWrapper1F64Return() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef], [kWasmF64]);
  let sig_index = builder.addType(sig);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
        kExprLocalGet, 0,
        kExprCallFunction, func_index
    ])
    .exportFunc();

  function import_func(suspender) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    return 0.25;
  }

  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(main(), v => assertEquals(0.25, v));
})();

(function testGenericWrapper1Float32() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef, kWasmF32], [kWasmExternRef]);
  let sig_index = builder.addType(sig);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12.5;
  function import_func(suspender, param) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += param;
  }

  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(main(12.5), v => assertEquals(undefined, v));
  assertEquals(25, x);
})();

(function testGenericWrapper1Float64() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef, kWasmF64], [kWasmExternRef]);
  let sig_index = builder.addType(sig);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12.5;
  function import_func(suspender, param) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += param;
  }

  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(main(12.5), v => assertEquals(undefined, v));
  assertEquals(25, x);
})();

let kSig_r_rffddddff = makeSig([kWasmExternRef, kWasmF32, kWasmF32, kWasmF64,
    kWasmF64, kWasmF64, kWasmF64, kWasmF32, kWasmF32], [kWasmExternRef]);

(function testGenericWrapper8Floats() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_r_rffddddff);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprLocalGet, 3,
      kExprLocalGet, 4,
      kExprLocalGet, 5,
      kExprLocalGet, 6,
      kExprLocalGet, 7,
      kExprLocalGet, 8,
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12;
  function import_func(suspender, param1, param2, param3, param4, param5,
      param6, param7, param8) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += param1 + 2 * param2 + 3 * param3 + 4 * param4 + 5 * param5
      + 6 * param6 + 7 * param7 + 8 * param8;
  }

  let param1 = { valueOf: () => { GC(); return 1.5; } };
  let param4 = { valueOf: () => { GC(); return 4.5; } };
  let param6 = { valueOf: () => { GC(); return 6.5; } };
  let param8 = { valueOf: () => { GC(); return 8.5; } };
  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(
      main(param1, 2.5, 3.5, param4, 5.5, param6, 7.5, param8),
      v => assertEquals(undefined, v));
  assertEquals(234, x);
})();

let kSig_r_riiliffddlfdff = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI64,
  kWasmI32, kWasmF32, kWasmF32, kWasmF64, kWasmF64, kWasmI64, kWasmF32,
  kWasmF64, kWasmF32, kWasmF32], [kWasmExternRef]);
// Floats don't fit into param registers.
(function testGenericWrapper13ParamMix() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_r_riiliffddlfdff);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprLocalGet, 3,
      kExprLocalGet, 4,
      kExprLocalGet, 5,
      kExprLocalGet, 6,
      kExprLocalGet, 7,
      kExprLocalGet, 8,
      kExprLocalGet, 9,
      kExprLocalGet, 10,
      kExprLocalGet, 11,
      kExprLocalGet, 12,
      kExprLocalGet, 13,
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12;
  let y = 1.0;
  function import_func(suspender, parami1, parami2, paraml1, parami3, paramf1,
      paramf2, paramd1, paramd2, paraml2, paramf3, paramd3, paramf4, paramf5) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += parami1 + 2 * parami2 + 3 * Number(paraml1) + 4 * parami3
      + 5 * Number(paraml2);
    y += paramf1 + 2 * paramf2 + 3 * paramd1 + 4 * paramd2 + 5 * paramf3
      + 6 * paramd3 + 7 * paramf4 + 8 * paramf5;
  }

  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(
      main(5, 6, 7n, 8, 1.5, 2.5, 3.5, 4.5, 11n, 5.5, 6.5, 7.5, 8.5),
      v => assertEquals(undefined, v));
  assertEquals(137, x);
  assertEquals(223, y);
})();

let kSig_r_riiliiiffddli = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI64,
    kWasmI32, kWasmI32, kWasmI32, kWasmF32, kWasmF32, kWasmF64, kWasmF64,
    kWasmI64, kWasmI32], [kWasmExternRef]);
// Integers don't fit into param registers.
(function testGenericWrapper12ParamMix() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_r_riiliiiffddli);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprLocalGet, 3,
      kExprLocalGet, 4,
      kExprLocalGet, 5,
      kExprLocalGet, 6,
      kExprLocalGet, 7,
      kExprLocalGet, 8,
      kExprLocalGet, 9,
      kExprLocalGet, 10,
      kExprLocalGet, 11,
      kExprLocalGet, 12,
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12;
  let y = 1.0;
  function import_func(suspender, param1, param2, param3, param4, param5,
      param6, paramf1, paramf2, paramd1, paramd2, param7, param8) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += param1 + 2 * param2 + 3 * Number(param3) + 4 * param4 + 5 * param5
      + 6 * param6 + 7 * Number(param7) + 8 * param8;
    y += paramf1 + paramf2 + paramd1 + paramd2;
  }

  let param1 = { valueOf: () => { GC(); return 5; } };
  let param4 = { valueOf: () => { GC(); return 8; } };
  let param6 = { valueOf: () => { GC(); return 10; } };
  let param8 = { valueOf: () => { GC(); return 12; } };
  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(
      main(param1, 6, 7n, param4, 9, param6, 1.5, 2.5, 3.6, 4.4, 11n, param8),
      v => assertEquals(undefined, v));
  assertEquals(360, x);
  assertEquals(13, y);
})();

let kSig_f_riiliiiffddlifffdi = makeSig([kWasmExternRef, kWasmI32, kWasmI32,
    kWasmI64, kWasmI32, kWasmI32, kWasmI32, kWasmF32, kWasmF32, kWasmF64,
    kWasmF64, kWasmI64, kWasmI32, kWasmF32, kWasmF32, kWasmF32, kWasmF64,
    kWasmI32], [kWasmF32]);
// Integers and floats don't fit into param registers.
(function testGenericWrapper17ParamMix() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_f_riiliiiffddlifffdi);
  let func_index = builder.addImport("mod", "func", sig_index);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprLocalGet, 3,
      kExprLocalGet, 4,
      kExprLocalGet, 5,
      kExprLocalGet, 6,
      kExprLocalGet, 7,
      kExprLocalGet, 8,
      kExprLocalGet, 9,
      kExprLocalGet, 10,
      kExprLocalGet, 11,
      kExprLocalGet, 12,
      kExprLocalGet, 13,
      kExprLocalGet, 14,
      kExprLocalGet, 15,
      kExprLocalGet, 16,
      kExprLocalGet, 17,
      kExprCallFunction, func_index
    ])
    .exportFunc();

  let x = 12;
  function import_func(suspender, param1, param2, param3, param4, param5,
      param6, paramf1, paramf2, paramd1, paramd2, param7, param8, paramf3,
      paramf4, paramf5, paramd3, param9) {
    GC();
    assertInstanceof(suspender, WebAssembly.Suspender);
    x += param1 + 2 * param2 + 3 * Number(param3) + 4 * param4 + 5 * param5
      + 6 * param6 + 7 * Number(param7) + 8 * param8 + 9 * param9;
    let y = 1.0;
    y += paramf1 + 2 * paramf2 + 3 * paramd1 + 4 * paramd2 + 5 * paramf3
      + 6 * paramf4 + 7 * paramf5 + 8 * paramd3;
    return y;
  }

  let param1 = { valueOf: () => { gc(); return 5; } };
  let param4 = { valueOf: () => { gc(); return 8; } };
  let param6 = { valueOf: () => { gc(); return 10; } };
  let param8 = { valueOf: () => { gc(); return 12; } };
  let paramd1 = { valueOf: () => { gc(); return 3.5; } };
  let paramf3 = { valueOf: () => { gc(); return 5.5; } };
  let param9 = { valueOf: () => { gc(); return 0; } };
  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = ToPromising(instance.exports.main);
  assertPromiseResult(
      main(param1, 6, 7n, param4, 9, param6, 1.5, 2.5, paramd1,
        4.5, 11n, param8, paramf3, 6.5, 7.5, 8.5, param9),
      v => assertEquals(223, v));
  assertEquals(360, x);
})();

(function testCallFromOptimizedFunction() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addFunction('wasm_fn', kSig_r_r).addBody([
      kExprLocalGet, 0
  ]).exportFunc();

  instance = builder.instantiate();
  function js_caller() {
    return ToPromising(instance.exports.wasm_fn);
  }
  %PrepareFunctionForOptimization(js_caller);
  js_caller();
  %OptimizeFunctionOnNextCall(js_caller);
  js_caller();
})();

(function Regression1130385() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_i_r);
  builder.addFunction("f0", sig_index)
    .addBody([kExprI32Const, 12])
    .exportFunc();

  builder.addFunction("f1", sig_index)
    .addBody([kExprI32Const, 15])
    .exportFunc();

  let instance = builder.instantiate();
  let f1 = ToPromising(instance.exports.f1);
  assertPromiseResult(f1(), v => assertEquals(15, v));
})();

(function testDeoptWithIncorrectNumberOfParams() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  let sig = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmExternRef]);
  let sig_index = builder.addType(sig);
  let imp = builder.addImport('q', 'func', sig_index);
  builder.addFunction('main', sig_index)
      .addBody([
          kExprLocalGet, 0,
          kExprLocalGet, 1,
          kExprLocalGet, 2,
          kExprCallFunction, imp])
      .exportAs('main');

  function deopt() {
    %DeoptimizeFunction(caller);
  }

  let instance = builder.instantiate({q: {func: deopt}});
  let main = ToPromising(instance.exports.main);
  function caller() {
    main(1, 2, 3, 4, 5);
    main(1, 2, 3, 4);
    main(1, 2, 3);
    main(1, 2);
    main(1);
    main();
  }
  caller();
})();

(function testGenericWrapper6Ref7F64Param() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_r_rddrrrrrrddddd = builder.addType(makeSig(
      [kWasmExternRef, kWasmF64, kWasmF64, kWasmExternRef, kWasmExternRef,
      kWasmExternRef, kWasmExternRef, kWasmExternRef, kWasmExternRef, kWasmF64,
      kWasmF64, kWasmF64, kWasmF64, kWasmF64],
       [kWasmExternRef]));


  builder.addFunction("func0", sig_r_rddrrrrrrddddd)
    .addBody([
      kExprLocalGet, 8,
      ])
    .exportAs("func0");

  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module);
  let func0 = ToPromising(instance.exports.func0);
  let res = func0(1, 2, "3", "4", "5", "6", "7", "8", 9, 10, 11, 12, 13);
  assertPromiseResult(res, v => assertEquals("8", v));
})();
