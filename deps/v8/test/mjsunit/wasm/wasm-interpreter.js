// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-test-streaming --allow-natives-syntax --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Test a sequence of calls JS -> Wasm -> Wasm -> JS -> Wasm.
(function testMultipleActivations() {
  print(arguments.callee.name);
  var instance = null;

  function callSquare(a0, a1, a2, a3) {
    let r = a0 + a1 + a2 + a3;
    return instance.exports.square(r);
  }

  var builder = new WasmModuleBuilder();
  var kSig_i_iiii = makeSig([kWasmI32, kWasmI32, kWasmI32, kWasmI32], [kWasmI32]);

  var sig_index = builder.addType(kSig_i_iiii);

  // Function 0
  builder.addImport("o", "func", sig_index);

  // Function 1
  builder.addFunction("main", kSig_i_i)
    .addLocals(kWasmI32, 1)
    .addBody([
      ...wasmI32Const(42),
      kExprLocalSet, 1,      // $local1 = 42
      kExprLocalGet, 0,
      kExprLocalGet, 0,
      kExprLocalGet, 0,
      kExprLocalGet, 0,
      kExprCallFunction, 2,  // Call Wasm function 'foo'
      ...wasmI32Const(2),    // return foo() * 2
      kExprI32Mul,           //
      ])
    .exportAs("main");

  // Function 2
  builder.addFunction("foo", kSig_i_iiii)
    .addLocals(kWasmI32, 1)
    .addBody([
      ...wasmI32Const(21),   // $local4 = 21
      kExprLocalSet, 4,      //
      kExprLocalGet, 3,      // Swap args
      kExprLocalGet, 2,      //
      kExprLocalGet, 1,      //
      kExprLocalGet, 0,      //
      kExprCallFunction, 0,  // Call JS function 'func'
      kExprDrop,             // Discard the result
      kExprLocalGet, 1,      // return $arg1
    ])

  // Function 3
  builder.addFunction("square", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 0,
      kExprI32Mul])
    .exportAs("square");

  instance = builder.instantiate({ o: { func: callSquare } });
  const arg = 3;
  let result = instance.exports.main(arg);
  assertEquals(2 * arg, result);
})();

(function testSpuriousConstInstructions() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();

  // This function contains two I32Const instructions (opcode 0x41), but one of
  // the const values also contains byte 0x41 as the signed LEB representation
  // of the const value -63.
  // Therefore WasmBytecodeGenerator will allocate 3 (and not 2) const slots in
  // the stack frame for function 'main'.
  // This function tests that local values are managed correctly in this case.
  builder.addFunction("main", kSig_i_v)
    .addLocals(kWasmI32, 1)
    .addBody([
      0x41, 0x41,            // ...wasmI32Const(-63),
      ...wasmI32Const(1),
      kExprCallFunction, 1,  // Call add(-63, 1)
      kExprLocalGet, 0,
      kExprI32Add,           // -62 + 0
      ])
    .exportAs("main");

  // Function 'add'
  var kSig_i_ii = makeSig([kWasmI32, kWasmI32], [kWasmI32]);
  builder.addFunction("add", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI32Add,
    ])

  instance = builder.instantiate({});
  let result = instance.exports.main();
  assertEquals(-62, result);
})();

// Test that WebAssembly.instantiateStreaming works with the Wasm interpreter.
(function testAsyncInstantiation() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();

  function square(val) { return val * val; }
  function vvFunc() {}

  // Functions 0-63
  for (let i = 0; i < 32; i++) {
    builder.addImport("o", "foo", kSig_i_i);
    builder.addImport("o", "bar", kSig_v_v);
  }

  // Function 64
  builder.addFunction("main", kSig_i_i)
    .addLocals(kWasmI32, 1)
    .addBody([
      ...wasmI32Const(42),
      kExprLocalSet, 1,       // $local1 = 42
      kExprLocalGet, 0,
      kExprCallFunction, 65,  // Call Wasm function 'f2'
    ])
    .exportAs("main");

  // Function 65
  builder.addFunction("f2", kSig_i_i)
    .addLocals(kWasmI32, 1)
    .addBody([
      ...wasmI32Const(21),   // $local1 = 21
      kExprLocalSet, 1,      //
      kExprLocalGet, 0,      // Call JS function f3($arg0)
      kExprCallFunction, 66, //
      kExprDrop,             // Discard the result
      kExprLocalGet, 1,
      kExprCallFunction, 68, // r = f5($local1)
      kExprCallFunction, 0,  // return callSquare(r)
    ]);

  // Function 66
  builder.addFunction("f3", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 0,
      kExprI32Mul,
      kExprCallFunction, 67, // return f4($arg0 * $arg0)
    ]);

  // Function 67
  builder.addFunction("f4", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      ...wasmI32Const(1),
      kExprI32Add,           // return $arg0 + 1
    ]);

  // Function 68
  builder.addFunction("f5", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      ...wasmI32Const(2),
      kExprI32Mul,           // return $arg0 * 2
    ]);

  let bytes = builder.toBuffer();
  let promise = WebAssembly.instantiateStreaming(Promise.resolve(bytes),
    { o: { foo: square, bar: vvFunc } });
  assertPromiseResult(promise.then(({ module, instance }) => {
    assertEquals(42 * 42, instance.exports.main(0));
  }));
})();

(function testRegressAvoidOverflowInTableFill() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  builder.addTable(kWasmExternRef, 1, 1);
  builder.addFunction("main", kSig_v_v)
    .addLocals(kWasmExternRef, 1)
    .addBody([
      ...wasmI32Const(1),
      kExprLocalGet, 0,
      ...wasmI32Const(4294967295),
      kNumericPrefix, kExprTableFill, 0
    ])
    .exportAs("main");

  let instance = builder.instantiate({});

  // Test that the integer overflow of table's start + count is correctly
  // handled with a Wasm trap.
  assertTraps(kTrapTableOutOfBounds, () => instance.exports.main());
})();

(function testRegressIfBlockStackResizing() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();

  // Returning from an If block should resize the stack to the size expected at
  // the end of the function. In this case the function main starts with one
  // element on the stack ($arg0, of type F64) and has no return values, so it
  // should end with one element on the stack.
  //
  // But an Edge bug showed that the logic here was broken in the way
  // WasmBytecodeGenerator resized the stack at the {end} instruction of the
  // {if} block, which in this case resulted in a type mismatch for the
  // arguments of the final I32StoreMem instruction (which expects two I32
  // values at the top of the stack, but got the F64 value passed as arg).
  builder.addFunction("main", kSig_v_d)
    .addBody([
      ...wasmI32Const(0),
      kExprBlock, kWasmI32,
        ...wasmI32Const(1),
        kExprIf, kWasmVoid,
          kExprReturn,
        kExprEnd,
        ...wasmI32Const(3),
      kExprEnd,
      kExprI32StoreMem, 0, 0,
    ])
    .exportAs("main");
  builder.addMemory(1, 1, false);

  let instance = builder.instantiate({});
  instance.exports.main(.5);
})();

(function testRegressHandleInvalidAndUnreachableInstructions() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();

  // A Wasm module can have invalid instructions (like the first local.set here)
  // if they are unreachable. We need to make sure that these instructions are
  // handled correctly when they can be merged in a super-instruction, as in the
  // pair(f64.load + local.set) here.
  builder.addFunction("main", kSig_v_i)
    .addLocals(kWasmF64, 1)
    .addBody([
      kExprUnreachable,
      kExprLocalSet, 0,
      kExprLocalGet, 0,
      kExprF64LoadMem, 0, 0,
      kExprLocalSet, 1,
    ])
    .exportAs("main");
  builder.addMemory(1, 1, false);

  let instance = builder.instantiate({});
  assertTraps(kTrapUnreachable, () => instance.exports.main(0));
})();

(function testRegressExternrefInExternalJSFunctionReturnType() {
  print(arguments.callee.name);
  function fn() {
    return 42;
  }

  var builder = new WasmModuleBuilder();
  var kSig_r_i = makeSig([kWasmI32], [kWasmExternRef]);

  // Function 0
  builder.addImport("o", "func", kSig_r_i);

  // Function 1
  builder.addFunction("run", kSig_r_v)
    .addBody([
      ...wasmI32Const(42),
      kExprCallFunction, 0,
    ]);

  // Function 2
  builder.addFunction("main", kSig_r_v)
    .addBody([
      kExprCallFunction, 1,
    ])
    .exportAs("main");

  var instance = builder.instantiate({ o: { func: fn } });
  instance.exports.main();
})();

(function testRegressExternrefInExternalJSFunctionArguments() {
  print(arguments.callee.name);
  function fn(arg) {
    return 42;
  }

  var builder = new WasmModuleBuilder();
  var kSig_i_r = makeSig([kWasmExternRef], [kWasmI32]);

  // Function 0
  builder.addImport("o", "func", kSig_i_r);

  // Function 1
  builder.addFunction("main", kSig_i_r)
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, 0,
    ])
    .exportAs("main");

  var instance = builder.instantiate({ o: { func: fn } });
  instance.exports.main();
})();

// Test indirect calls to a function in a different Wasm instance that accepts
// ref arguments and returns ref results.
(function testRegressReferenceStackIsSharedOnIndirectExternalCalls() {
  print(arguments.callee.name);
  let table = new WebAssembly.Table({
    initial: 10, maximum: 10, element: 'anyfunc',
  });

  var builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_r_r);
  builder.addImportedTable('o', 'table');

  // Function 0
  builder.addImport("o", "fn", kSig_i_r);

  // Function 1
  builder.addFunction("func", kSig_r_r)
    .addBody([
      kExprLocalGet, 0,
    ])
    .exportAs("func");

  // Function 2
  builder.addFunction("main", kSig_r_r)
    .addBody([
      kExprLocalGet, 0,
      kExprI32Const, 0,
      kExprCallIndirect, sig_index, kTableZero,
    ])
    .exportAs("main");

  var instance2 = builder.instantiate({
    o: {
      table: table,
      fn: () => { return 42; },
    }
  });

  table.set(0, instance2.exports.func);

  var instance1 = builder.instantiate({
    o: {
      table: table,
      fn: () => { return 42; },
    }
  });

  instance1.exports.main(41);
})();

// Test indirect calling a JS function imported by a different Wasm instance.
(function WasmCallToFunctionImportedFromAnotherInstance() {
  print(arguments.callee.name);
  let table = new WebAssembly.Table({
    initial: 10, maximum: 10, element: 'anyfunc',
  });

  var builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_r_r);
  builder.addImportedTable('o', 'table');

  // Function 0
  builder.addImport("o", "fn", kSig_r_r);
  builder.addExport("fn", 0);

  // Function 1
  builder.addFunction("main", kSig_r_r)
    .addBody([
      kExprLocalGet, 0,
      kExprI32Const, 0,
      kExprCallIndirect, sig_index, kTableZero,
    ])
    .exportAs("main");

  var instance2 = builder.instantiate({
    o: {
      table: table,
      fn: (ref) => { return 42; },
    }
  });

  table.set(0, instance2.exports.fn);

  var instance1 = builder.instantiate({
    o: {
      table: table,
      fn: (ref) => { return 43; },
    }
  });

  let result = instance1.exports.main(41);
  assertEquals(42, result);
})();

// Test recursively calling a function that has ref arguments.
(function testRegressRefDrop() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();

  // Function 0
  builder.addFunction("main", kSig_a_v)
    .addLocals(kWasmI32, 8851)
    .addBody([
      kExprLocalGet, ...wasmSignedLeb(8850, 5),
      kExprDrop,
      kExprCallFunction, 0,
    ])
    .exportAs("main");

  var instance = builder.instantiate({ o: {} });
  assertThrows(
    () => instance.exports.main(0), RangeError);
})();

// In case of stack overflow was not handled correctly, the call stack was not
// fully unwound and the execution continued from the next instruction of the
// caller function.
(function testRegressCorrectlyUnwindStackOnStackOverflow() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();

  builder.addFunction("main", kSig_i_v)
    .addLocals(kWasmF32, 640)
    .addBody([
      kExprCallFunction, 0,  // Recursively call main.
      kExprUnreachable,
      ...wasmI32Const(0),
    ])
    .exportAs("main");

  let instance = builder.instantiate({});
  assertThrows(() => instance.exports.main(), RangeError);
})();

// Test that the frame address for a new Activation is correctly calculated
// for indirect calls to JS functions.
// (Broke https://silentspacemarine.com/)
(function testRegressFixIndirectJSFunctionCalls() {
  print(arguments.callee.name);
  let table = new WebAssembly.Table({
    initial: 10, maximum: 10, element: 'anyfunc',
  });

  var builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_v_v);
  builder.addImportedTable('o', 'table');

  // Function 0
  builder.addImport("o", "fn", kSig_v_v);
  builder.addExport("fn", 0);

  // Function 1
  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprI32Const, 1,
      kExprCallFunction, 2, // call_foo();
    ])
    .exportAs("main");

  // Function 2
  builder.addFunction("call_foo", kSig_v_v)
    .addBody([
      kExprI32Const, 0,
      kExprCallIndirect, sig_index, kTableZero,
    ]);

  // Function 3
  builder.addFunction("foo", kSig_v_v)
    .addLocals(kWasmI32, 8)
    .addBody([
    ])
    .exportAs("foo");

  var instance = null;
  instance = builder.instantiate({
    o: {
      table: table,
      fn: () => { instance.exports.foo(); },
    }
  });
  table.set(0, instance.exports.fn);

  let result = instance.exports.main();
  assertEquals(1, result);
})();

// Fix error decoding i64 atomic instructions.
(function testRegressI64AtomicInstructionDecoding() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();

  builder.addFunction("main", kSig_v_v)
    .addBody([
      ...wasmI32Const(0), // address
      ...wasmI64Const(0), // value to add
      kAtomicPrefix,
      kExprI64AtomicAdd,
      0x00, 0x00, 0xad, 0x04,
      kExprUnreachable,
    ])
    .exportAs("main");
  builder.addMemory(1, 1, false);

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /Compiling function #0:\"main\" failed: invalid alignment for atomic operation; expected alignment is 3, actual alignment is 0/);
})();

// Test capturing a stack trace when calling a JS function imported and exported
// by a Wasm module.
(function StackTraceInWasmCallToFunctionImportedFromAnotherInstance() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();

  // Function 0
  builder.addImport("o", "fn", kSig_r_r);
  builder.addExport("fn", 0);

  var instance = builder.instantiate({
    o: {
      fn: (ref) => {
        let e = new Error().stack;
        return e;
      },
    }
  });

  let result = instance.exports.fn(42);
  assertTrue(result.startsWith("Error"));
})();

(function testLoadStoreSuperInstruction() {
  print(arguments.callee.name);
  let addFunction = function (builder, name, value, makeConstInstr, loadInstr,
    storeInstr, notEqInstr) {
    builder.addFunction(name, kSig_v_v)
      .addBody([
        ...wasmI32Const(1),
        ...makeConstInstr(value),
        storeInstr, 0, 0,

        ...wasmI32Const(2),
        ...wasmI32Const(0),
        ...wasmI32Const(1),
        kExprI32Add,
        loadInstr, 0, 0,
        storeInstr, 0, 0,

        ...wasmI32Const(2),
        loadInstr, 0, 0,
        ...makeConstInstr(value),
        notEqInstr,
        kExprIf, kWasmVoid,
          kExprUnreachable,
        kExprEnd,
      ])
      .exportAs(name);
  }
  var builder = new WasmModuleBuilder();
  addFunction(builder, "test_i32_r2s_LoadStore", 42, wasmI32Const,
    kExprI32LoadMem, kExprI32StoreMem, kExprI32Ne);
  addFunction(builder, "test_f32_r2s_LoadStore", 42.0, wasmF32Const,
    kExprF32LoadMem, kExprF32StoreMem, kExprF32Ne);
  addFunction(builder, "test_i64_r2s_LoadStore", 42, wasmI64Const,
    kExprI64LoadMem, kExprI64StoreMem, kExprI64Ne);
  addFunction(builder, "test_f64_r2s_LoadStore", 42.0, wasmF64Const,
    kExprF64LoadMem, kExprF64StoreMem, kExprF64Ne);
  builder.addMemory(1, 1, false);

  let instance = builder.instantiate({});
  instance.exports.test_i32_r2s_LoadStore();
  instance.exports.test_f32_r2s_LoadStore();
  instance.exports.test_i64_r2s_LoadStore();
  instance.exports.test_f64_r2s_LoadStore();
})();

// Test handling exceptions while calling a JS function imported and exported
// by a Wasm module.
(function testExceptionFromImportedExportedJSFunction() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();

  // Function 0
  builder.addImport("o", "fn", kSig_v_v);
  builder.addExport("fn", 0);

  var instance = builder.instantiate({
    o: {
      fn: () => {
        throw "CRASH";
      },
    }
  });

  var error;
  try {
    instance.exports.fn();
  } catch (e) {
    error = e;
  }
  assertEquals("CRASH", error);
})();

// Test catching Wasm exceptions thrown after a call to an imported function.
(function testWasmExceptionCaughtAfterImportedFunctionCall() {
  print(arguments.callee.name);
  var instance = null;
  var builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_v);

  // Function 0
  builder.addImport("o", "js_throw", kSig_v_v);

  // Function 1
  builder.addImport("o", "js2", kSig_v_v);

  // Function 2
  builder.addFunction("wasm_throw", kSig_v_v)
    .addBody([
      kExprThrow, except,
    ])
    .exportAs("wasm_throw");

  // Function 3
  builder.addFunction("fn1", kSig_v_v)
    .addBody([
      // Call imported function 'js_throw'.
      kExprCallFunction, 0,
      kExprUnreachable,
    ])
    .exportAs("fn1");

  // Function 4
  builder.addFunction("fn2", kSig_v_v)
    .addBody([
        // Call imported function 'js2'.
      kExprCallFunction, 1,
      kExprUnreachable,
    ]);

  // Function 5
  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprTry, kWasmI32,
        // Call fn2.
        kExprCallFunction, 4,
        kExprI32Const, 0,
      kExprCatch, except,
        kExprI32Const, 1,
      kExprCatchAll,
        kExprI32Const, 2,
      kExprEnd,
    ])
    .exportAs("main");

  instance = builder.instantiate({
    o: {
      js_throw: function () {
        instance.exports.wasm_throw();
      },
      js2: function () {
        instance.exports.fn1();
      }
    }
  });

  // This call Wasm|main -> Wasm|fn2 -> JS|js2 -> Wasm|fn1 -> JS|js_throw ->
  //           -> Wasm|wasm_throw.
  // `wasm_throw` throws an exception that is caught by `main`.
  let result = instance.exports.main();
  assertEquals(1, result);
})();

// Test catching Wasm exceptions thrown after an indirect function call.
(function testWasmExceptionCaughtAfterIndirectFunctionCall() {
  print(arguments.callee.name);
  let table = new WebAssembly.Table({
    initial: 10, maximum: 10, element: 'anyfunc',
  });

  var builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_v);
  let sig_index = builder.addType(kSig_v_v);
  builder.addImportedTable('o', 'table');

  // Function 0
  builder.addFunction("wasm_throw", kSig_v_v)
    .addBody([
      kExprThrow, except,
      kExprUnreachable,
    ])
    .exportAs("wasm_throw");

  // Function 1
  builder.addFunction("fn1", kSig_v_v)
    .addBody([
      // Call 'wasm_throw'.
      kExprI32Const, 0,
      kExprCallIndirect, sig_index, kTableZero,
      kExprUnreachable,
    ])
    .exportAs("fn1");

  // Function 2
  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprTry, kWasmI32,

        // Call fn1.
        kExprI32Const, 1,
        kExprCallIndirect, sig_index, kTableZero,

        kExprI32Const, 0,
      kExprCatch, except,
        kExprI32Const, 1,
      kExprCatchAll,
        kExprI32Const, 2,
      kExprEnd,
    ])
    .exportAs("main");

  let instance = builder.instantiate({
    o: { table: table }
  });

  table.set(0, instance.exports.wasm_throw);
  table.set(1, instance.exports.fn1);

  // This call main -> fn1 -> wasm_throw.
  // `wasm_throw` throws an exception that is caught by `main`.
  let result = instance.exports.main();
  assertEquals(1, result);
})();

// A throw inside the body of a catch block is never caught by the corresponding
// try block of that catch block.
(function testWasmCatchHandlerCantCatchExceptionsThrownInThatCatchBlock() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();

  // Function 0
  builder.addImport("o", "js_throw_1", kSig_v_v);

  // Function 1
  builder.addImport("o", "js_throw_2", kSig_v_v);

  // Function 2
  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprTry, kWasmI32,
        kExprTry, kWasmI32,
          // Call js_throw_1.
          kExprCallFunction, 0,
          kExprI32Const, 0,
        kExprCatchAll,
          // Call js_throw_2.
          kExprCallFunction, 1,
          kExprI32Const, 1,
        kExprEnd,
      kExprCatchAll,
        kExprI32Const, 2,
      kExprEnd,
    ])
    .exportAs("main");

  let instance = builder.instantiate({
    o: {
      js_throw_1: function () {
        var e = new WebAssembly.Exception();
        throw e;
      },
      js_throw_2: function () {
        var e = new WebAssembly.Exception();
        throw e;
      }
    }
  });

  let result = instance.exports.main();
  assertEquals(2, result);
})();

// Test a sequence of calls JS -> Wasm-A -> Wasm-B -> Wasm-A.
(function testMultipleActivationsAcrossInstances() {
  print(arguments.callee.name);
  var instanceA = null;
  var instanceB = null;

  var builderA = new WasmModuleBuilder();
  let tableA = new WebAssembly.Table({
    element: "anyfunc",
    initial: 1,
    maximum: 1
  });
  builderA.addImportedTable("m", "table", 1, 1);
  var sig_index = builderA.addType(kSig_i_i);

  // Function 0
  builderA.addFunction("main", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,

      // Call Wasm function 'callSquare' in module B.
      kExprI32Const, 0,
      kExprCallIndirect, sig_index, kTableZero,

      // Call Wasm function 'square' in module A.
      kExprCallFunction, 1,
    ])
    .exportAs("main");

  // Function 1
  builderA.addFunction("square", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 0,
      kExprI32Mul])
    .exportAs("square");

  instanceA = builderA.instantiate({ m: { table: tableA } });

  var builderB = new WasmModuleBuilder();

  // Function 0
  builderB.addImport("o", "square", kSig_i_i);

  // Function 1
  builderB.addFunction("call_square", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, 0,  // Call Wasm function 'square' in module A.
    ])
    .exportAs("callSquare");

  instanceB = builderB.instantiate({ o: { square: instanceA.exports.square } });

  tableA.set(0, instanceB.exports.callSquare);

  const arg = 3;
  let result = instanceA.exports.main(arg);
  assertEquals(arg * arg * arg * arg, result);
})();

// Test Load+Set superinstructions that set the value of a shared slot.
(function testRegressSharedSlotsWithLoadSetSuperInstruction() {
  print(arguments.callee.name);
  const memory_offset = 1;
  const old_value = 11;
  const new_value = 42;

  let addLoadSetTestFunction = function (builder, name, copyFromReg) {
    builder.addFunction(name, kSig_v_v)
      .addLocals(kWasmI32, 1)
      .addBody([
        // Create a shared slot.
        ...wasmI32Const(old_value),
        kExprLocalSet, 0,
        kExprLocalGet, 0,

        // Write a new value to memory
        ...wasmI32Const(memory_offset),
        ...wasmI32Const(new_value),
        kExprI32StoreMem, 0, 0,

        // Super-instruction: Load+Set.
        ...wasmI32Const(memory_offset)]
        .concat(copyFromReg ? [
          ...wasmI32Const(0),
          kExprI32Add
        ] : [])
        .concat([
          kExprI32LoadMem, 0, 0,
          kExprLocalSet, 0,

          // Verify that the set slot contains the new value and that the shared
          // slot contains the old value.
          ...wasmI32Const(old_value),
          kExprI32Ne,
          kExprIf, kWasmVoid,
            kExprUnreachable,
          kExprEnd,

          kExprLocalGet, 0,
          ...wasmI32Const(new_value),
          kExprI32Ne,
          kExprIf, kWasmVoid,
            kExprUnreachable,
          kExprEnd
        ]))
      .exportAs(name);
  }

  var builder = new WasmModuleBuilder();
  builder.addMemory(1, 1, false);
  addLoadSetTestFunction(builder, "test_r2s_LoadMem_LocalSet", true);
  addLoadSetTestFunction(builder, "test_s2s_LoadMem_LocalSet", false);

  let instance = builder.instantiate({});
  instance.exports.test_r2s_LoadMem_LocalSet();
  instance.exports.test_s2s_LoadMem_LocalSet();
})();

// Test WorkerThread termination while running in the Wasm interpreter.
// (This broke https://rummikub-apps.com/?cb=37)
(function testRegressWorkerThreadTermination() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("add", kSig_i_ii)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();

  let module = builder.toModule();

  function workerCode() {
    onmessage = function (module) {
      try {
        let instance = new WebAssembly.Instance(module);
        let result;
        postMessage(123);
        for (let i = 0; i < 10000000; i++) {
          result = instance.exports.add(40, 2);
        }
      } catch (e) {
        postMessage('ERROR: ' + e);
      }
    }
  }

  let worker = new Worker(workerCode, { type: 'function' });
  worker.postMessage(module);
  worker.getMessage();
  worker.terminate();
})();

// Test cases where R2R/S2R mode support is not implemented for ref types.
(function testRegressNoRegisterOptimizationForRefTypes() {
  print(arguments.callee.name);

  // RefGlobalGet
  {
    const builder = new WasmModuleBuilder();
    let globalref = builder.addImportedGlobal('o', 'g', kWasmExternRef, true);

    builder.addFunction('get_ref_global', kSig_v_r)
      .exportFunc()
      .addBody([
        kExprLocalGet, 0,
        kExprGlobalSet, globalref,
        kExprGlobalGet, globalref,
        kExprGlobalSet, globalref,
      ]);

    let ref_global = new WebAssembly.Global({mutable: true, value: 'externref'});
    var instance = builder.instantiate({o: {g: ref_global}});
    let obj = {foo: 'bar'};
    instance.exports.get_ref_global(obj);
    assertEquals(obj, ref_global.value);
  }

  // RefSelect
  {
    const builder = new WasmModuleBuilder();
    let globalref = builder.addImportedGlobal('o', 'g', kWasmExternRef, true);

    builder.addFunction('select_ref', kSig_v_r)
      .exportFunc()
      .addBody([
        kExprLocalGet, 0,
        kExprLocalGet, 0,
        kExprI32Const, 0,
        kExprSelectWithType, 1, kExternRefCode,
        kExprGlobalSet, globalref,
      ]);

    let ref_global = new WebAssembly.Global({mutable: true, value: 'externref'});
    var instance = builder.instantiate({o: {g: ref_global}});
    let obj = {foo: 'bar'};
    instance.exports.select_ref(obj);
    assertEquals(obj, ref_global.value);
  }
})();

// Unreachable select instruction may not have enough stack values.
(function testRegressUnreachableSelect() {
  const builder = new WasmModuleBuilder();
  builder.addFunction('select', kSig_v_v)
    .exportFunc()
    .addBody([
      kExprReturn,
      kExprSelect,
      kExprDrop,
    ]);

  var instance = builder.instantiate({});
  instance.exports.select();
})();

// Test resizing the IndirectCallTable while in the middle of an indirect call.
(function testIndirectCallTableResizingInExecuteIndirectCall() {
  print(arguments.callee.name);

  var instance = null;
  function jsFunc0(val) {
    return val + val;
  }
  function jsFunc1(val) {
    return val * val;
  }
  function jsFunc2(val) {
    table.grow(61); // Resize table from 3 to 64 elements.
    table.set(63, instance.exports.func3);
    return instance.exports.foo(val);
  }
  function jsFunc3(val) {
    return val;
  }

  let table = new WebAssembly.Table({
    initial: 3, maximum: 64, element: 'anyfunc',
  });

  var builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_i_i);
  builder.addImportedTable('o', 'table');

  // Functions 0-3
  builder.addImport("o", "func0", sig_index);
  builder.addExport("func0", 0);
  builder.addImport("o", "func1", sig_index);
  builder.addExport("func1", 1);
  builder.addImport("o", "func2", sig_index);
  builder.addExport("func2", 2);
  builder.addImport("o", "func3", sig_index);
  builder.addExport("func3", 3);

  // Function 4
  builder.addFunction("foo", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      ...wasmI32Const(63),
      kExprCallIndirect, sig_index, kTableZero,
    ])
    .exportAs("foo");

  // Function 5
  builder.addFunction("main", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,

      kExprI32Const, 0,
      kExprCallIndirect, sig_index, kTableZero,

      kExprI32Const, 1,
      kExprCallIndirect, sig_index, kTableZero,

      kExprI32Const, 2,
      kExprCallIndirect, sig_index, kTableZero,
    ])
    .exportAs("main");

  instance = builder.instantiate(
    {
      o: {
        table: table,
        func0: jsFunc0, func1: jsFunc1, func2: jsFunc2, func3: jsFunc3
      }
    });
  table.set(0, instance.exports.func0);
  table.set(1, instance.exports.func1);
  table.set(2, instance.exports.func2);

  const arg = 42;
  let result = instance.exports.main(arg);
  assertEquals((arg*2)*(arg*2), result);
})();

(function TestReturnCallWithRefArgs() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();

  let append_dot = builder.addImport("q", "append_dot", kSig_r_r);

  // construct the code for the function
  // main(N,X) where N=<1 => X
  // main(N,X) => main(N-1, X + ".")
  let kSig_r_ir = makeSig([kWasmI32, kWasmExternRef], [kWasmExternRef]);
  let sig_r_v = builder.addType(kSig_r_v);
  let main = builder.addFunction("main", kSig_r_ir);
  main.addBody([
    kExprLocalGet, 0,
    kExprI32Const, 0,
    kExprI32LeS,
    kExprIf, sig_r_v,
    kExprLocalGet, 1,
    kExprElse,
    kExprLocalGet, 0,
    kExprI32Const, 1,
    kExprI32Sub,
    kExprLocalGet, 1,
    kExprCallFunction, append_dot,
    kExprReturnCall, main.index,
    kExprEnd
  ]).exportFunc();

  let module = builder.instantiate({
    q: {
      append_dot: function (s) { return s + '.'; }
    }
  });

  let result = module.exports.main(4, "");
  assertEquals(4, result.length);
})();

(function TestReturnCallImportedWithRefArgs() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();

  let append_dot = builder.addImport("q", "append_dot", kSig_r_r);

  let kSig_r_ir = makeSig([kWasmI32, kWasmExternRef], [kWasmExternRef]);
  let sig_r_ir = builder.addType(kSig_r_ir);
  let call_main = builder.addImport("q", "call_main", sig_r_ir);

  // construct the code for the function
  // main(N,X) where N=<1 => X
  // main(N,X) => call_main(N-1, X + ".")
  // where call_main(x, y) is an imported JS function that calls main(x, y).
  let sig_r_v = builder.addType(kSig_r_v);
  let main = builder.addFunction("main", kSig_r_ir);
  main.addBody([
    kExprLocalGet, 0,
    kExprI32Const, 0,
    kExprI32LeS,
    kExprIf, sig_r_v,
      kExprLocalGet, 1,
    kExprElse,
      kExprLocalGet, 0,
      kExprI32Const, 1,
      kExprI32Sub,
      kExprLocalGet, 1,
      kExprCallFunction, append_dot,
      kExprReturnCall, call_main,
    kExprEnd
  ]).exportFunc();

  let module = builder.instantiate({
    q: {
      append_dot: function (s) { return s + '.'; },
      call_main: function (arg1, arg2) {
        return module.exports.main(arg1, arg2);
      }

    }
  });

  let result = module.exports.main(4, "");
  assertEquals(4, result.length);
})();

(function TestReturnCallIndirectWithRefArgs() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();

  let append_dot = builder.addImport("q", "append_dot", kSig_r_r);

  // construct the code for the function
  // main(N,X) where N=<1 => X
  // main(N,X) => main(N-1, X + ".")
  // where main(x, y) is called with an indirect call to table[tableIndex].
  let kSig_r_ir = makeSig([kWasmI32, kWasmExternRef], [kWasmExternRef]);
  let sig_r_ir = builder.addType(kSig_r_ir);
  let sig_r_v = builder.addType(kSig_r_v);
  let main = builder.addFunction("main", kSig_r_ir);
  main.addBody([
    kExprLocalGet, 0,
    kExprI32Const, 0,
    kExprI32LeS,
    kExprIf, sig_r_v,
    kExprLocalGet, 1,
    kExprElse,
    kExprLocalGet, 0,
    kExprI32Const, 1,
    kExprI32Sub,
    kExprLocalGet, 1,
    kExprCallFunction, append_dot,
    kExprI32Const, 0,
    kExprReturnCallIndirect, sig_r_ir, kTableZero,
    kExprEnd
  ]).exportFunc();

  builder.appendToTable([main.index]);

  let module = builder.instantiate({
    q: {
      append_dot: function (s) { return s + '.'; }
    }
  });

  let result = module.exports.main(4, "");
  assertEquals(4, result.length);
})();

(function TestImportIndirectReturnCallWithRefArgs() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();

  let append_dot = builder.addImport("q", "append_dot", kSig_r_r);

  let kSig_r_ir = makeSig([kWasmI32, kWasmExternRef], [kWasmExternRef]);
  let sig_r_ir = builder.addType(kSig_r_ir);
  let call_main = builder.addImport("q", "call_main", sig_r_ir);
  builder.addTable(kWasmAnyFunc, 4);
  // Arbitrary location in the table.
  const tableIndex = 3;
  builder.addActiveElementSegment(0, wasmI32Const(tableIndex), [call_main]);

  // construct the code for the function
  // main(N,X) where N=<1 => X
  // main(N,X) => main(N-1, X + ".")
  // where main(x, y) is called through an indirect call to table[tableIndex] ->
  // (imported JS function) call_main(x, y).
  let sig_r_v = builder.addType(kSig_r_v);
  let mainFn = builder.addFunction("main", kSig_r_ir);
  mainFn.addBody([
    kExprLocalGet, 0,
    kExprI32Const, 0,
    kExprI32LeS,
    kExprIf, sig_r_v,
    kExprLocalGet, 1,
    kExprElse,
    kExprLocalGet, 0,
    kExprI32Const, 1,
    kExprI32Sub,
    kExprLocalGet, 1,
    kExprCallFunction, append_dot,
    kExprI32Const, tableIndex,
    kExprReturnCallIndirect, sig_r_ir, kTableZero,
    kExprEnd
  ]).exportAs("main");

  builder.appendToTable([mainFn.index]);

  let module = builder.instantiate({
    q: {
      append_dot: function (s) { return s + '.'; },
      call_main: function (arg1, arg2) {
        return module.exports.main(arg1, arg2);
      }
    }
  });

  let result = module.exports.main(4, "");
  assertEquals(4, result.length);
})();

(function TestImportIndirectReturnCallOnDifferentInstanceWithRefArgs() {
  print(arguments.callee.name);

  let table1 = new WebAssembly.Table({
    initial: 4, maximum: 4, element: 'anyfunc',
  });
  let table2 = new WebAssembly.Table({
    initial: 4, maximum: 4, element: 'anyfunc',
  });
  // Arbitrary location in the table.
  const tableIndex = 3;

  let builder = new WasmModuleBuilder();
  builder.addImportedTable("q", 'table');

  // Function 0
  let append_dot = builder.addImport("q", "append_dot", kSig_r_r);

  let kSig_r_ir = makeSig([kWasmI32, kWasmExternRef], [kWasmExternRef]);
  let sig_r_ir = builder.addType(kSig_r_ir);
  // Function 1
  let call_main = builder.addImport("q", "call_main", sig_r_ir);
  builder.addExport("call_main", 1);

  // construct the code for the function
  // main(N,X) where N=<1 => X
  // main(N,X) => main(N-1, X + ".")
  // where main(x, y) is called with an indirect call to table[tableIndex].
  // There are two instance of the same module, and in both instances
  // table[tableIndex] -> instance1.main().
  let sig_r_v = builder.addType(kSig_r_v);
  let mainFn = builder.addFunction("main", kSig_r_ir);
  // Function 2
  mainFn.addBody([
    kExprLocalGet, 0,
    kExprI32Const, 0,
    kExprI32LeS,
    kExprIf, sig_r_v,
    kExprLocalGet, 1,
    kExprElse,
    kExprLocalGet, 0,
    kExprI32Const, 1,
    kExprI32Sub,
    kExprLocalGet, 1,
    kExprCallFunction, append_dot,
    kExprI32Const, tableIndex,
    kExprReturnCallIndirect, sig_r_ir, kTableZero,
    kExprEnd
  ]).exportAs("main");

  let instance1 = builder.instantiate({
    q: {
      table: table1,
      append_dot: function (s) { return s + '.'; },
      call_main: function (arg1, arg2) {
        return instance1.exports.main(arg1, arg2);
      }
    }
  });
  table1.set(tableIndex, instance1.exports.call_main);

  let instance2 = builder.instantiate({
    q: {
      table: table2,
      append_dot: function (s) { return s + '.'; },
      call_main: function (arg1, arg2) {
        return instance1.exports.main(arg1, arg2);
      }
    }
  });
  table2.set(tableIndex, instance1.exports.main);

  let result = instance2.exports.main(4, "");
  assertEquals(4, result.length);
})();

(function TestRefTailCall() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();

  var sig_index = builder.addType(kSig_i_ii);

  builder.addFunction("main", makeSig(
    [wasmRefType(sig_index), kWasmI32, kWasmI32], [kWasmI32]))
    .addBody([kExprLocalGet, 1, kExprLocalGet, 2, kExprLocalGet, 0,
      kExprReturnCallRef, sig_index])
    .exportFunc();

  builder.addFunction("sub", sig_index)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Sub])
    .exportFunc();

  let instance = builder.instantiate({});

  let result = instance.exports.main(instance.exports.sub, 12, 7);
  assertEquals(5, result);
})();

(function TestTailCallsMadeFollowingInstructionsUnreachable() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  const table = builder.addTable(kWasmFuncRef, 2, 2);
  let sig_index = builder.addType(kSig_v_i);

  builder.addFunction("main", kSig_v_i)
    .addBody([
      kExprLocalGet, 0,
      ...wasmI32Const(0),
      kExprI32Eq,
      kExprIf, kWasmVoid,
        kExprReturn,
      kExprEnd,
      ...wasmI32Const(0),
      kExprReturnCall, 0,
      ...wasmI32Const(42),
      kExprCallIndirect, sig_index, kTableZero,
    ])
    .exportAs("main");

  instance = builder.instantiate({ imports: { table } });
  instance.exports.main(42);
})();

// Test Isolate::PrintStack() with interpreter frames.
(function StackTraceInWasmCallToFunctionImportedFromAnotherInstance() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();

  // Function 0
  builder.addImport("o", "foo", kSig_v_v);

  // Function 1
  builder.addFunction("main", kSig_v_v)
    .addBody([
      kExprCallFunction, 0,  // Call Wasm function 'foo'
    ])
    .exportAs("main");

  var instance = builder.instantiate({
    o: {
      foo: (ref) => {
        %DebugTrace();
      },
    }
  });

  instance.exports.main();
})();

(function TestReturnCallRefMadeFollowingInstructionsUnreachable() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_l_v);

  builder.addFunction("main", kSig_l_i)
    .addBody([
      kExprRefNull, sig_index,
      kExprReturnCallRef, sig_index,
      kExprLocalSet, 0
    ])
    .exportAs("main");

  var instance = builder.instantiate();
  assertThrows(() => instance.exports.main(42), WebAssembly.RuntimeError);
})();

(function TestPassingRefArgsAcrossJsCalls() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  var instance = null;

  // Function 0
  builder.addImport("o", "a", kSig_v_v);

  // Function 1
  builder.addImport("o", "b", kSig_v_r);

  // Function 2
  builder.addFunction("outer", kSig_v_r)
    .addBody([
      kExprCallFunction, 0,  // Call JS function 'a'
      kExprLocalGet, 0,
      kExprCallFunction, 1,  // Call JS function 'b'
    ])
    .exportAs("outer");

  // Function 3
  builder.addFunction("inner", kSig_v_r)
    .addBody([
    ])
    .exportAs("inner");

  instance = builder.instantiate({
    o: {
      a: function () {
        instance.exports.inner(null);
      },
      b: function (state) {
        state.push(42);
      },
    }
  });

  instance.exports.outer([]);
})();

(function TestCompactCopySlotOptimizationAcrossLoops() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  let sig_i_i = builder.addType(kSig_i_i);

  builder.addFunction("main", kSig_v_i)
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLocalGet, 0,
      kExprLoop, sig_i_i,
      kExprLocalSet, 1,
      kExprLocalGet, 0,
      kExprLocalGet, 0,
      ...wasmI32Const(0),
      kExprLocalSet, 0,
      kExprBrIf, 0,
      kExprEnd,
      kExprDrop,
    ])
    .exportAs("main");

  var instance = builder.instantiate();
  instance.exports.main(1);
})();

(function TestWasmGCSubytypeCheck() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let structSuper = builder.addStruct([makeField(kWasmI32, true)]);
  let structTypes = [];
  for (let i = 0; i < 50; i++) {
    structTypes[i] = structSuper;
    structSuper = builder.addStruct([makeField(kWasmI32, true)], structSuper);
  }

  builder.addFunction("main", kSig_v_v)
    .addBody([
      ...wasmI32Const(0),
      kGCPrefix, kExprStructNew, structTypes[0],
      kGCPrefix, kExprRefCast, structTypes[31],
      kExprDrop
    ])
    .exportAs("main");

  instance = builder.instantiate();
  assertTraps(kTrapIllegalCast, () => instance.exports.main());
})();

(function TestWasmGCBrOnNonNull() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  let array_type_index = builder.addArray(kWasmI32, true);
  let struct_type_index = builder.addStruct([makeField(kWasmI32, true)]);
  let sig_v_v = builder.addType(kSig_v_v);
  let sig_i_v = builder.addType(kSig_i_v);

  builder.addFunction("test_br_on_non_null_with_nullref_nonvoidsig", kSig_i_i)
    .exportAs("test_br_on_non_null_with_nullref_nonvoidsig").addBody([
      kExprLoop, sig_i_v,
        ...wasmI32Const(5),
        kGCPrefix, kExprArrayNewDefault, array_type_index,
        ...wasmI32Const(-8),
        kGCPrefix, kExprStructNew, struct_type_index,
        kExprRefNull, array_type_index,
        kExprBrOnNonNull, 0, // drop null ref and don't jump
        kExprDrop, // drop the struct
        kExprLocalGet, 0, // array index
        kGCPrefix, kExprArrayGet, array_type_index,
      kExprEnd,
  ]);

  builder.addFunction("test_br_on_non_null_with_nullref_voidsig", kSig_v_v)
    .exportAs("test_br_on_non_null_with_nullref_voidsig").addBody([
      kExprLoop, sig_v_v,
        ...wasmI32Const(5),
        kGCPrefix, kExprArrayNewDefault, array_type_index,
        ...wasmI32Const(-8),
        kGCPrefix, kExprStructNew, struct_type_index,
        kExprRefNull, array_type_index,
        kExprBrOnNonNull, 0, // drop null ref and don't jump
        kExprDrop, // drop the struct
       ...wasmI32Const(13), // value
       ...wasmI32Const(-2), // array index
       kGCPrefix, kExprArraySet, array_type_index,
      kExprEnd,
    ]);


  builder.addFunction("test_br_on_non_null_with_nonnullref_nonvoidsig", kSig_i_i)
    .exportAs("test_br_on_non_null_with_nonnullref_nonvoidsig")
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLoop, sig_i_v,
        ...wasmI32Const(42),
        kExprLocalGet, 1,
        ...wasmI32Const(1),
        kExprLocalSet, 1, // set local to 0 to end loop at next iteration.
        kExprBrIf, 1,
        ...wasmI32Const(5),
        kGCPrefix, kExprArrayNewDefault, array_type_index,
        ...wasmI32Const(-8),
        kGCPrefix, kExprStructNew, struct_type_index,
        kExprBrOnNonNull, 0, // non null ref => fallthru
        kExprLocalGet, 0, // array index
        kGCPrefix, kExprArrayGet, array_type_index,
        kExprDrop,
      kExprEnd,
    ]);

  builder.addFunction("test_br_on_non_null_with_nonnullref_voidsig", kSig_v_i)
    .exportAs("test_br_on_non_null_with_nonnullref_voidsig")
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLoop, sig_v_v,
        kExprLocalGet, 1,
        ...wasmI32Const(1),
        kExprLocalSet, 1, // set local to 0 to end loop at next iteration.
        kExprBrIf, 1,
        ...wasmI32Const(5),
        kGCPrefix, kExprArrayNewDefault, array_type_index,
        ...wasmI32Const(-8),
        kGCPrefix, kExprStructNew, struct_type_index,
        kExprBrOnNonNull, 0, // non null ref => fallthru
        kExprLocalGet, 0, // array index
        kGCPrefix, kExprArrayGet, array_type_index,
        kExprDrop,
      kExprEnd,
    ]);

  builder.addFunction("test_br_on_null_with_nullref_nonvoidsig", kSig_i_i)
    .exportAs("test_br_on_null_with_nullref_nonvoidsig")
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLoop, sig_i_v,
        ...wasmI32Const(42),
        kExprLocalGet, 1,
        ...wasmI32Const(1),
        kExprLocalSet, 1, // set local to 0 to end loop at next iteration.
        kExprBrIf, 1,
        ...wasmI32Const(5),
        ...wasmI32Const(5),
        kGCPrefix, kExprArrayNew, array_type_index,
        ...wasmI32Const(-8),
        kGCPrefix, kExprStructNew, struct_type_index,
        kExprRefNull, array_type_index,
        kExprBrOnNull, 0,
        kExprDrop, // drop the ref
        kExprDrop, // drop the struct
        kExprLocalGet, 0, // array index
        kGCPrefix, kExprArrayGet, array_type_index,
        kExprDrop,
      kExprEnd,
    ]);

  builder.addFunction("test_br_on_null_with_nullref_voidsig", kSig_v_v)
    .exportAs("test_br_on_null_with_nullref_voidsig")
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLoop, sig_v_v,
        kExprLocalGet, 0,
        ...wasmI32Const(1),
        kExprI32Add,
        kExprLocalSet, 0, // inc(local(0))
        ...wasmI32Const(3),
        kExprLocalGet, 0,
        kExprI32Eq,
        kExprBrIf, 1,
        kExprRefNull, array_type_index,
        kExprBrOnNull, 0,
        kExprUnreachable,
     kExprEnd,
    ]);

  builder.addFunction("test_br_on_null_with_nonnullref_nonvoidsig", kSig_i_i)
    .exportAs("test_br_on_null_with_nonnullref_nonvoidsig")
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLoop, sig_i_v,
        ...wasmI32Const(42),
        kExprLocalGet, 1,
        kExprBrIf, 1,
        ...wasmI32Const(5),
        ...wasmI32Const(5),
        kGCPrefix, kExprArrayNew, array_type_index,
        ...wasmI32Const(-8),
        kGCPrefix, kExprStructNew, struct_type_index,
        kExprBrOnNull, 0,
        kExprDrop, // drop the struct
        kExprLocalGet, 0, // array index
        kGCPrefix, kExprArrayGet, array_type_index,
        kExprLocalSet, 1, // set local to end loop
      kExprEnd,
    ]);

  builder.addFunction("test_br_on_null_with_nonnullref_voidsig", kSig_v_i)
    .exportAs("test_br_on_null_with_nonnullref_voidsig")
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLoop, sig_v_v,
        kExprLocalGet, 1,
        kExprBrIf, 1,
        ...wasmI32Const(5),
        ...wasmI32Const(5),
        kGCPrefix, kExprArrayNew, array_type_index,
        ...wasmI32Const(-8),
        kGCPrefix, kExprStructNew, struct_type_index,
        kExprBrOnNull, 0,
        kExprDrop, // drop the struct
        kExprLocalGet, 0, // array index
        kGCPrefix, kExprArrayGet, array_type_index,
        kExprLocalSet, 1, // set local to end loop
      kExprEnd,
    ]);

  let instance = builder.instantiate();
  assertEquals(0, instance.exports.test_br_on_non_null_with_nullref_nonvoidsig(1));
  assertTraps(kTrapArrayOutOfBounds, () => instance.exports.test_br_on_non_null_with_nullref_nonvoidsig(1234));
  assertTraps(kTrapArrayOutOfBounds, () => instance.exports.test_br_on_non_null_with_nullref_voidsig());
  assertEquals(42, instance.exports.test_br_on_non_null_with_nonnullref_nonvoidsig(1234));
  instance.exports.test_br_on_non_null_with_nonnullref_voidsig(1234);
  assertEquals(42, instance.exports.test_br_on_null_with_nullref_nonvoidsig(1234));
  instance.exports.test_br_on_null_with_nullref_voidsig();
  assertEquals(42, instance.exports.test_br_on_null_with_nonnullref_nonvoidsig(1));
  assertTraps(kTrapArrayOutOfBounds, () => instance.exports.test_br_on_null_with_nonnullref_nonvoidsig(1234));
  instance.exports.test_br_on_null_with_nonnullref_voidsig(1);
  assertTraps(kTrapArrayOutOfBounds, () => instance.exports.test_br_on_null_with_nonnullref_voidsig(1234));
})();

(function testNestedTryCatch() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_v);

  builder.addFunction("main", kSig_i_iii)
    .addBody([
      kExprTry, kWasmI32,
        ...wasmF32Const(3.14),
        ...wasmF32Const(3.14),
        kExprTry, kWasmI32,
          ...wasmI32Const(1),
        kExprCatch, except,
          kExprTry, kWasmI32,
            ...wasmI32Const(2),
          kExprEnd,
        kExprCatch, except,
          ...wasmI32Const(3),
        kExprEnd,
        kExprDrop,
        kExprF32Ne,
      kExprEnd,
    ])
    .exportAs("main");

  let instance = builder.instantiate();
  let result = instance.exports.main();
  assertEquals(0, result);
})();

(function TestReturnCallWithRefs() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  const table_size = 3;
  let table = builder.addTable(kWasmFuncRef, table_size, table_size);

  let array_type_index = builder.addArray(kWasmI32, true);
  let struct_type_index = builder.addStruct([makeField(kWasmI32, true)]);
  let sig_index = builder.addType(kSig_v_v);

  // Function 0
  builder.addImport("imports", "imported_func", kSig_v_v);

  // Function 1
  let f1 = builder.addFunction("main", kSig_v_v)
    .addBody([
      kExprRefNull, array_type_index,
      ...wasmI32Const(13),
      ...wasmI32Const(-2),
      ...wasmI32Const(1),
      kExprCallIndirect, sig_index, table.index,
      kGCPrefix, kExprArraySet, array_type_index,
    ])
    .exportAs("main");

  // Function 2
  let f2 = builder.addFunction("f2", kSig_v_v)
    .addBody([
      ...wasmI32Const(2),
      kExprReturnCallIndirect, sig_index, kTableZero,
    ]);

  // Function 3
  let f3 = builder.addFunction("f3", kSig_v_v)
    .addLocals(kWasmAnyRef, 1)
    .addBody([
      ...wasmI32Const(-8),
      kGCPrefix, kExprStructNew, struct_type_index,
      kExprCallFunction, 0, // call $import0
      kExprLocalSet, 0
    ]);

  builder.addActiveElementSegment(
    table.index, wasmI32Const(0),
    [[kExprRefFunc, f1.index], [kExprRefFunc, f2.index], [kExprRefFunc, f3.index]],
    kWasmFuncRef);

  let instance = builder.instantiate({ imports: { imported_func: function() {} } });
  assertTraps(kTrapNullDereference, () => instance.exports.main());
})();

(function TestRefsParamsAndRetsInIfBlock() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  let array_type_index = builder.addArray(kWasmI32, true);
  let struct_type_index = builder.addStruct([makeField(kWasmI32, true)]);
  let sig_a_in = builder.addType(makeSig([kWasmI32, wasmRefNullType(array_type_index)], [kWasmAnyFunc]));

  // Function 0
  builder.addImport("imports", "imported_func", kSig_v_v);

  // Function 1
  let f1 = builder.addFunction("main", kSig_v_v)
    .addLocals(kWasmAnyRef, 1)
    .addBody([
      ...wasmI32Const(1),
      kExprRefNull, array_type_index,
      ...wasmI32Const(1),
      kExprIf, sig_a_in,
        kExprDrop,
        kExprDrop,
        kExprRefNull, kAnyFuncCode,
      kExprElse,
        kExprDrop,
        kExprDrop,
        kExprRefNull, kAnyFuncCode,
      kExprEnd,
      kExprDrop,
      ...wasmI32Const(-8),
      kGCPrefix, kExprStructNew, struct_type_index,
      kExprLocalSet, 0,
      kExprLocalGet, 0,
      kGCPrefix, kExprRefCast, array_type_index, // invalid cast
      kExprCallFunction, 0,
      ...wasmI32Const(13),
      ...wasmI32Const(-2),
      kGCPrefix, kExprArraySet, array_type_index,
    ])
    .exportAs("main");

  let instance = builder.instantiate({
    "imports": {
      imported_func: () => {
        oob_arr = [1.1, 2.2, 3.3, 4.4];
        double_arr = [1.1, 2.2, 3.3, 4.4];
        obj_arr = [double_arr, double_arr];
      },
    }
  });
  assertTraps(kTrapIllegalCast, () => instance.exports.main());
})();

(function TestCatchExceptionThrownByCalledFunction() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  builder.addMemory(1, 32);
  let $tag = builder.addTag(builder.addType(kSig_v_v));

  // Function 0
  let f1 = builder.addFunction("main", kSig_i_i)
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprTry, kWasmI32,
        kExprCallFunction, 1,
        ...wasmI32Const(0),
      kExprCatch, $tag,
      kExprLocalGet, 0, // oob read offset
      kExprI32LoadMem, 0, 0,
      kExprEnd,
    ])
    .exportAs("main");

  // Function 1
  let f2 = builder.addFunction("f1", kSig_v_v)
    .addBody([
      kExprThrow, $tag,
    ]);

  let instance = builder.instantiate();
  assertThrows(() => instance.exports.main(0xffffffff), WebAssembly.RuntimeError);
})();

(function TestRefTypeReturnedByJSFunctions() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  let array_type_index = builder.addArray(kWasmI32, true);
  let sig_a_a = builder.addType(makeSig([wasmRefNullType(array_type_index)], [wasmRefNullType(array_type_index)]));

  // Function 0
  builder.addImport("imports", "imported_func", sig_a_a);

  // Function 1
  builder.addFunction("main", kSig_v_v)
    .addBody([
      kExprRefNull, array_type_index,
      kExprCallFunction, 0,
      ...wasmI32Const(0),
      kGCPrefix, kExprArrayGet, array_type_index,
      kExprDrop
    ])
    .exportAs("main");

  let instance = builder.instantiate({
    "imports": {
      imported_func: (refVal) => {
        return refVal + 0x41414141;
      },
    }
  });
  assertThrows(() => instance.exports.main(), TypeError);
})();

(function TestRefArrayOperationsWithNegativeLength() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  let array_type_index = builder.addArray(kWasmI32, true);

  // ArrayCopy
  builder.addFunction("test_array_copy", kSig_v_v)
    .addBody([
      ...wasmI32Const(-57),
      ...wasmI32Const(72),
      kGCPrefix, kExprArrayNew, array_type_index,

      ...wasmI32Const(61),
      ...wasmI32Const(91),
      ...wasmI32Const(79),
      kGCPrefix, kExprArrayNew, array_type_index,

      ...wasmI32Const(64),
      ...wasmI32Const(-50),
      kGCPrefix, kExprArrayCopy, array_type_index, array_type_index,
    ])
    .exportAs("test_array_copy");

  // ArrayNewData
  let dummy_byte = 0xff;
  let element_0 = 1000;
  let element_1 = -2222;
  let data_segment = builder.addPassiveDataSegment(
    [dummy_byte, element_0 & 0xff, (element_0 >> 8) & 0xff,
      element_1 & 0xff, (element_1 >> 8) & 0xff]);
  builder.addFunction("test_array_new_segment", kSig_v_v)
    .addBody([
      ...wasmI32Const(64),
      ...wasmI32Const(-50),
      kGCPrefix, kExprArrayNewData, array_type_index, data_segment,
      kExprDrop
    ])
    .exportAs("test_array_new_segment");

  // ArrayFill
  builder.addFunction("test_array_fill", kSig_v_v)
    .addBody([
      ...wasmI32Const(-57),
      ...wasmI32Const(72),
      kGCPrefix, kExprArrayNew, array_type_index,

      ...wasmI32Const(8),  // offset.
      ...wasmI32Const(42),  // value.
      ...wasmI32Const(-6),  // length.
      kGCPrefix, kExprArrayFill, array_type_index,
    ])
    .exportAs("test_array_fill");


  let instance = builder.instantiate();
  assertTraps(kTrapArrayOutOfBounds, () => instance.exports.test_array_copy());
  assertTraps(
    kTrapArrayTooLarge, () => instance.exports.test_array_new_segment());
  assertTraps(kTrapArrayOutOfBounds, () => instance.exports.test_array_fill());
})();

(function TestPreserveRefStackAcrossRecursiveImportCalls() {
  print(arguments.callee.name);
  let count = 100;

  function rec(v) {
    if (--count == 0) {
      return;
    }

    var builder = new WasmModuleBuilder();
    const import_func = builder.addImport('m', 'f', makeSig(v, []));

    let body = [];
    for (let i = 0; i < v.length; i++) {
      body.push(kExprLocalGet, i);
    }
    body.push(kExprCallFunction, import_func);

    builder.addFunction('main', makeSig(v, []))
      .addBody(body).exportFunc();

    const instance = builder.instantiate({
      m: {
        f: f5
      }
    });
    instance.exports.main();
  }

  function f5() {
    rec([kWasmExternRef, kWasmExternRef, kWasmExternRef, kWasmExternRef, kWasmExternRef]);
  }

  rec([]);
})();

(function TestStackOverflowCallingImportedExportedFunction() {
  print(arguments.callee.name);

  function run(f) {
    function t() {
      try {
        return t();
      } catch (e) {
        return f();
      }
    }
    return t();
  }
  const builder = new WasmModuleBuilder();
  builder.addImport("e", "f", kSig_v_v);
  builder.addExport("f", 0);

  const instance = builder.instantiate({
    e: {
      f: () => { },
    },
  });

  function foo() {
    instance.exports.f();
  }

  run(() => {
    return foo();
  });
})();

(function TestWriteBarrierInWasmGCArrays() {
  print(arguments.callee.name);

  const builder = new WasmModuleBuilder();
  let refarraytype = builder.addArray(kWasmArrayRef, true);
  let intarraytype = builder.addArray(kWasmI32, true);

  builder.addFunction(undefined, makeSig([kWasmI32], [wasmRefType(refarraytype)]))
    .exportAs('make_ref_array')
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewDefault, refarraytype
    ]);

  builder.addFunction(undefined, makeSig([kWasmI32], [wasmRefType(intarraytype)]))
    .exportAs('make_int_array')
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewDefault, intarraytype
    ]);

  builder.addFunction(undefined, makeSig([wasmRefNullType(refarraytype), kWasmI32], [kWasmArrayRef]))
    .exportAs('ref_array_get')
      .addBody([
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        kGCPrefix, kExprArrayGet, refarraytype
      ]);

  builder.addFunction(undefined, makeSig([wasmRefType(refarraytype), kWasmI32, wasmRefNullType(intarraytype)], []))
    .exportAs('ref_array_set')
    .addBody([
      kExprLocalGet, 0, // array
      kExprLocalGet, 1, // index
      kExprLocalGet, 2, // value
      kGCPrefix, kExprArraySet, refarraytype,
    ]);

  builder.addFunction(undefined, makeSig([wasmRefNullType(refarraytype), kWasmI32, kWasmI32, kWasmI32], []))
    .exportAs('ref_array_fill')
    .addBody([
      kExprLocalGet, 0, // array
      kExprLocalGet, 1, // offset
      kExprLocalGet, 2,
      kGCPrefix, kExprArrayNewDefault, intarraytype,

      kExprLocalGet, 3, // size
      kGCPrefix, kExprArrayFill, refarraytype
    ]);

  (function testArraySet() {
    const instance = builder.instantiate({});
    var ref_array = instance.exports.make_ref_array(2);
    gc({ type: 'minor' });
    var int_array = instance.exports.make_int_array(1);
    instance.exports.ref_array_set(ref_array, 1, int_array);
    gc({ type: 'major' });
    instance.exports.ref_array_get(ref_array, 1);
  })();

  (function testArrayFill() {
    const instance = builder.instantiate({});
    var ref_array = instance.exports.make_ref_array(1);
    gc({ type: 'minor' });
    instance.exports.ref_array_fill(ref_array, 0, 0, 1);
    gc({ type: 'major' });
    instance.exports.ref_array_get(ref_array, 0);
  })();
})();

(function TestWriteBarrierInWasmGCStructs() {
  print(arguments.callee.name);

  const builder = new WasmModuleBuilder();
  let array_type_index = builder.addArray(kWasmI32, true);
  let struct_type_index = builder.addStruct([makeField(kWasmI32, true), makeField(wasmRefType(array_type_index), true)]);

  builder.addFunction(undefined, makeSig([wasmRefType(array_type_index)], [wasmRefType(struct_type_index)]))
    .exportAs('make_struct')
    .addBody([
      ...wasmI32Const(42),
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct_type_index
    ]);

  builder.addFunction(undefined, makeSig([kWasmI32], [wasmRefType(array_type_index)]))
    .exportAs('make_int_array')
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewDefault, array_type_index
    ]);

  builder.addFunction(undefined, makeSig([wasmRefType(struct_type_index)], [kWasmArrayRef]))
    .exportAs('struct_get')
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructGet, struct_type_index, 1
    ]);

  builder.addFunction(undefined, makeSig([wasmRefType(struct_type_index), wasmRefType(array_type_index)], []))
    .exportAs('struct_set')
    .addBody([
      kExprLocalGet, 0, // struct
      kExprLocalGet, 1, // value
      kGCPrefix, kExprStructSet, struct_type_index, 1
    ]);

  (function testStructSet() {
    const instance = builder.instantiate({});
    var temp_array = instance.exports.make_int_array(1);
    var struct = instance.exports.make_struct(temp_array);
    gc({ type: 'minor' });
    var int_array = instance.exports.make_int_array(1);
    instance.exports.struct_set(struct, int_array);
    gc({ type: 'major' });
    instance.exports.struct_get(struct);
  })();
})();

(function testCopySlotMulti() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  var kSig_d_ilfd = makeSig([kWasmI32, kWasmI64, kWasmF32, kWasmF64], [kWasmF64]);

  // Function 0.
  builder.addFunction("main", kSig_d_ilfd)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprLocalGet, 3,
      kExprCallFunction, 1
    ])
    .exportAs("main");

  // Function 1.
  builder.addFunction("fn1", kSig_d_ilfd)
    .addBody([
      kExprLocalGet, 0,
      kExprF64UConvertI32,
      kExprLocalGet, 1,
      kExprF64UConvertI64,
      kExprLocalGet, 2,
      kExprF64ConvertF32,
      kExprLocalGet, 3,
      kExprF64Add,
      kExprF64Add,
      kExprF64Add
    ]);


  let instance = builder.instantiate({});
  assertEquals(10.0, instance.exports.main(1, 2n, 3.0, 4.0));
})();

(function testBytecodeCompressedSlotOffset() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  let main = builder.addFunction("main", kSig_v_v)
    .addLocals(kWasmI32, 3);
  let body = [
    ...wasmI32Const(42),
    kExprLocalTee, 0,
    kExprLocalTee, 1,
  ];

  for (let i = 0; i < 0x8000; i++) {
    body.push(kExprLocalGet, 0);
    body.push(kExprLocalGet, 1);
    body.push(kExprI32Add);
    body.push(...wasmI32Const(42));
    body.push(kExprI32Sub);
    body.push(kExprLocalSet, 2);
  }
  body.push(...wasmI32Const(42));
  body.push(kExprLocalGet, 2);
  body.push(kExprI32Eq);
  body.push(kExprBrIf, 0);
  body.push(kExprUnreachable);
  main.addBody(body)
    .exportAs("main");

  let instance = builder.instantiate({});
  instance.exports.main();
})();

(function testLoadStoreInstructions() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  builder.addMemory(1, 1, false);

  const i32 = 0;
  const i64 = 1;
  const f32 = 2;
  const f64 = 3;

  let cmp_ops = [
    kExprI32Ne, kExprI64Ne, kExprF32Ne, kExprF64Ne
  ];
  let add_ops = [
    kExprI32Add, kExprI64Add, kExprF32Add, kExprF64Add
  ];
  let vals = [
    [...wasmI32Const(42)], [...wasmI64Const(42)], [...wasmF32Const(42.0)], [...wasmF64Const(42.0)]
  ];
  let zeroes = [
    [...wasmI32Const(0)], [...wasmI64Const(0)], [...wasmF32Const(0.0)], [...wasmF64Const(0.0)]
  ];

  let instrs = [
    { store_op: kExprI32StoreMem, load_op: kExprI32LoadMem, type: i32 },
    { store_op: kExprI32StoreMem8, load_op: kExprI32LoadMem8U, type: i32 },
    { store_op: kExprI32StoreMem8, load_op: kExprI32LoadMem8S, type: i32 },
    { store_op: kExprI32StoreMem16, load_op: kExprI32LoadMem16U, type: i32 },
    { store_op: kExprI32StoreMem16, load_op: kExprI32LoadMem16S, type: i32 },
    { store_op: kExprI64StoreMem, load_op: kExprI64LoadMem, type: i64 },
    { store_op: kExprI64StoreMem8, load_op: kExprI64LoadMem8U, type: i64 },
    { store_op: kExprI64StoreMem8, load_op: kExprI64LoadMem8S, type: i64 },
    { store_op: kExprI64StoreMem16, load_op: kExprI64LoadMem16U, type: i64 },
    { store_op: kExprI64StoreMem16, load_op: kExprI64LoadMem16S, type: i64 },
    { store_op: kExprI64StoreMem32, load_op: kExprI64LoadMem32U, type: i64 },
    { store_op: kExprI64StoreMem32, load_op: kExprI64LoadMem32S, type: i64 },
    { store_op: kExprF32StoreMem, load_op: kExprF32LoadMem, type: f32 },
    { store_op: kExprF64StoreMem, load_op: kExprF64LoadMem, type: f64 },
  ];

  let body = [];
  for (const instr of instrs) {
    let type = instr.type;
    let val = vals[type];
    let zero = zeroes[type]
    let cmp_op = cmp_ops[type];
    let add_op = add_ops[type];

    // r2s_store s2s_load
    body.push(...wasmI32Const(0)); // store index
    for (const v of val) body.push(v);
    for (const z of zero) body.push(z);
    body.push(add_op);
    body.push(instr.store_op, 0, 0);
    body.push(...wasmI32Const(0)); // load index
    body.push(instr.load_op, 0, 0);
    for (const v of val) body.push(v);
    body.push(cmp_op);
    body.push(kExprIf, kWasmVoid);
    body.push(kExprUnreachable);
    body.push(kExprEnd);

    // s2s_loadstore
    body.push(...wasmI32Const(0)); // store index
    body.push(...wasmI32Const(0)); // load index
    body.push(instr.load_op, 0, 0);
    body.push(instr.store_op, 0, 0);

    // r2s_loadstore
    body.push(...wasmI32Const(0)); // store index
    body.push(...wasmI32Const(0));
    body.push(...wasmI32Const(0));
    body.push(kExprI32Add); // load index = 0 + 0
    body.push(instr.load_op, 0, 0);
    body.push(instr.store_op, 0, 0);

    // r2s_load
    body.push(...wasmI32Const(0));
    body.push(...wasmI32Const(0));
    body.push(kExprI32Add); // load index
    body.push(instr.load_op, 0, 0);
    for (const v of val) body.push(v);
    body.push(cmp_op);
    body.push(kExprIf, kWasmVoid);
    body.push(kExprUnreachable);
    body.push(kExprEnd);

    // s2s_store
    body.push(...wasmI32Const(0)); // index
    for (const v of val) body.push(v);
    body.push(instr.store_op, 0, 0);

    // r2r_load
    for (const v of val) body.push(v);
    body.push(...wasmI32Const(0));
    body.push(...wasmI32Const(0));
    body.push(kExprI32Add); // load index = 0 + 0
    body.push(instr.load_op, 0, 0);
    body.push(cmp_op);
    body.push(kExprIf, kWasmVoid);
    body.push(kExprUnreachable);
    body.push(kExprEnd);

    // s2r_load
    for (const v of val) body.push(v);
    body.push(...wasmI32Const(0)); // load index
    body.push(instr.load_op, 0, 0);
    body.push(cmp_op);
    body.push(kExprIf, kWasmVoid);
    body.push(kExprUnreachable);
    body.push(kExprEnd);

    // s2s_loadset
    body.push(...wasmI32Const(0)); // load index
    body.push(instr.load_op, 0, 0);
    body.push(kExprLocalSet, type);
  }

  builder.addFunction("main", kSig_v_v)
    .addLocals(kWasmI32, 1)
    .addLocals(kWasmI64, 1)
    .addLocals(kWasmF32, 1)
    .addLocals(kWasmF64, 1)
    .addBody(body)
    .exportAs("main");

  let instance = builder.instantiate({});
  instance.exports.main();
})();

(function testGCInImportedFunctionWithMultipleResults() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  var kSig_lll_l = makeSig([kWasmI64], [kWasmI64, kWasmI64, kWasmI64]);

  builder.addImport('imports', 'f', kSig_lll_l);

  builder.addFunction("main", kSig_lll_l)
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, 0])
    .exportAs("main")

  function foo(x) {
    //gc();
    return [x, x, x];
  }

  let instance = builder.instantiate({ 'imports': { 'f': foo } });
  for (let i = 0; i < 1e5; i++) {
    assertEquals(instance.exports.main(42n), [42n, 42n, 42n]);
  }
})();

(function TestCallRefWithMultipleRefResults() {
  print(arguments.callee.name);

  const builder = new WasmModuleBuilder();
  let array_type_index = builder.addArray(kWasmI32, true);
  let struct_type_index = builder.addStruct([makeField(kWasmI32, true)]);
  let kSig_xy_v = makeSig([], [wasmRefType(array_type_index), wasmRefType(struct_type_index)]);
  const funcTypeId = builder.addType(kSig_xy_v);

  builder.addFunction("main", kSig_v_v)
    .addBody([
      kExprRefNull, array_type_index,
      kExprCallFunction, 2,
      kExprDrop])
    .exportAs("main");

  let f1 = builder.addFunction("func2", kSig_xy_v)
    .addBody([
      ...wasmI32Const(3),
      kGCPrefix, kExprArrayNewDefault, array_type_index,
      ...wasmI32Const(5),
      kGCPrefix, kExprStructNew, struct_type_index])
    .exportFunc();;

  builder.addFunction("func1", kSig_v_v)
    .addBody([
      kExprRefFunc, f1.index,
      kExprCallRef, funcTypeId,
      ...wasmI32Const(1),
      kGCPrefix, kExprStructSet, struct_type_index, 0,
      kExprDrop]);

  let instance = builder.instantiate({});
  instance.exports.main();
})();

(function TestIndirectCallToDifferentInstanceWithMultipleRefResults() {
  print(arguments.callee.name);

  const builder = new WasmModuleBuilder();
  let array_type_index = builder.addArray(kWasmI32, true);
  let struct_type_index = builder.addStruct([makeField(kWasmI32, true)]);
  let kSig_xy_v = makeSig([], [wasmRefType(array_type_index), wasmRefType(struct_type_index)]);
  let sig_index = builder.addType(kSig_xy_v);

  let table = new WebAssembly.Table({
    initial: 10, maximum: 10, element: 'anyfunc',
  });
  builder.addImportedTable('o', 'table');

  builder.addFunction("main", kSig_v_v)
    .addBody([
      kExprRefNull, array_type_index,
      kExprCallFunction, 2,
      kExprDrop])
    .exportAs("main");

  builder.addFunction("func2", kSig_xy_v)
    .addBody([
      ...wasmI32Const(3),
      kGCPrefix, kExprArrayNewDefault, array_type_index,
      ...wasmI32Const(5),
      kGCPrefix, kExprStructNew, struct_type_index])
    .exportAs("func2");

  builder.addFunction("func1", kSig_v_v)
    .addBody([
      kExprI32Const, 0,
      kExprCallIndirect, sig_index, kTableZero,
      ...wasmI32Const(1),
      kGCPrefix, kExprStructSet, struct_type_index, 0,
      kExprDrop]);

  let instance2 = builder.instantiate({
    o: {
      table: table,
    }
  });
  table.set(0, instance2.exports.func2);

  let instance1 = builder.instantiate({
    o: {
      table: table,
    }
  });

  instance1.exports.main();
})();

(function TestIndirectCallToJSFunctionWithMultipleRefResults() {
  print(arguments.callee.name);

  const builder = new WasmModuleBuilder();
  let array_type_index = builder.addArray(kWasmI32, true);
  let struct_type_index = builder.addStruct([makeField(kWasmI32, true)]);
  let kSig_xy_v = makeSig([], [wasmRefType(array_type_index), wasmRefType(struct_type_index)]);
  let sig_index = builder.addType(kSig_xy_v);

  let table = new WebAssembly.Table({
    initial: 10, maximum: 10, element: 'anyfunc',
  });
  builder.addImportedTable('o', 'table');

  // Function 0
  builder.addImport("o", "fn", kSig_xy_v);
  builder.addExport("fn", 0);

  // Function 1
  builder.addFunction("main", kSig_v_v)
    .addBody([
      kExprRefNull, array_type_index,
      kExprCallFunction, 3,
      kExprDrop])
    .exportAs("main");

  // Function 2
  builder.addFunction("func2", kSig_xy_v)
    .addBody([
      ...wasmI32Const(3),
      kGCPrefix, kExprArrayNewDefault, array_type_index,
      ...wasmI32Const(5),
      kGCPrefix, kExprStructNew, struct_type_index])
    .exportAs("func2");

  // Function 3
  builder.addFunction("func1", kSig_v_v)
    .addBody([
      kExprI32Const, 0,
      kExprCallIndirect, sig_index, kTableZero,
      ...wasmI32Const(1),
      kGCPrefix, kExprStructSet, struct_type_index, 0,
      kExprDrop]);

  let instance2 = builder.instantiate({
    o: {
      table: table,
      fn: () => { return instance2.exports.func2(); },
    }
  });
  table.set(0, instance2.exports.fn);

  let instance1 = builder.instantiate({
    o: {
      table: table,
      fn: () => {},
    }
  });

  instance1.exports.main();
})();

(function testWasmExceptionCaughtAfterTailCall() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_v);

  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprTry, kWasmVoid,
        kExprThrow, except,
      kExprCatchAll,
      kExprEnd,
      kExprReturnCall, 1,
    ])
    .exportAs("main");

  // Function 1
  builder.addFunction("func1", kSig_i_v)
    .addBody([
      kExprTry, kWasmVoid,
        kExprTry, kWasmVoid,
          kExprTry, kWasmVoid,
            kExprThrow, except,
          kExprCatch, except,
          kExprEnd,
        kExprEnd,
      kExprEnd,
      kExprI32Const, 3,
    ]);

  let instance = builder.instantiate({});

  let result = instance.exports.main();
  assertEquals(3, result);
})();

(function TestCodeUnreachableAfterTailCalls() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  let array_type_index = builder.addArray(kWasmI32, true);
  let struct_type_index = builder.addStruct([makeField(kWasmI32, true)]);
  let kSig_array_v = makeSig([], [wasmRefType(array_type_index)]);
  let sig_array_index = builder.addType(kSig_array_v);
  let kSig_struct_v = makeSig([], [wasmRefType(struct_type_index)]);
  let sig_struct_index = builder.addType(kSig_struct_v);

  builder.addImport("o", "imported_func", kSig_array_v);
  builder.addExport("imported_func", 0);

  let table = new WebAssembly.Table({
    initial: 3, maximum: 3, element: 'anyfunc',
  });
  builder.addImportedTable('o', 'table');

  let f3 = builder.addFunction("create_array", kSig_array_v)
    .addBody([
      ...wasmI32Const(1),
      kGCPrefix, kExprArrayNewDefault, array_type_index,
    ])
    .exportAs("create_array");

  builder.addFunction("test_return_callref", makeSig([], [wasmRefType(kSig_array_v)]))
    .addBody([
      kExprBlock, sig_struct_index,
        kExprRefFunc, f3.index,
        kExprReturnCallRef, sig_array_index,
      kExprEnd,
      kExprUnreachable,
   ])
    .exportAs("test_return_callref");

  builder.addFunction("test_return_call_imported", makeSig([], [wasmRefType(kSig_array_v)]))
    .addBody([
      kExprBlock, sig_struct_index,
        kExprReturnCall, 0,  // Tail call imported_func.
      kExprEnd,
      kExprUnreachable,
    ])
    .exportAs("test_return_call_imported");

  builder.addFunction("test_return_call_indirect_wasm_same_instance", makeSig([], [wasmRefType(kSig_array_v)]))
    .addBody([
      kExprBlock, sig_struct_index,
        kExprI32Const, 0,
        kExprReturnCallIndirect, sig_array_index, kTableZero,
      kExprEnd,
      kExprUnreachable,
    ])
    .exportAs("test_return_call_indirect_wasm_same_instance");

  builder.addFunction("test_return_call_indirect_wasm_different_instance", makeSig([], [wasmRefType(kSig_array_v)]))
    .addBody([
      kExprBlock, sig_struct_index,
        kExprI32Const, 2,
        kExprReturnCallIndirect, sig_array_index, kTableZero,
      kExprEnd,
      kExprUnreachable,
    ])
    .exportAs("test_return_call_indirect_wasm_different_instance");

  builder.addFunction("test_return_call_indirect_js", makeSig([], [wasmRefType(kSig_array_v)]))
    .addBody([
      kExprBlock, sig_struct_index,
        kExprI32Const, 1,
        kExprReturnCallIndirect, sig_array_index, kTableZero,
      kExprEnd,
      kExprUnreachable,
    ])
    .exportAs("test_return_call_indirect_js");

  let instance1 = null;
  instance1 = builder.instantiate({
    o: {
      table: table,
      imported_func: function () {
        return instance1.exports.create_array();
      }
    }
  });

  let instance2 = null;
  instance2 = builder.instantiate({
    o: {
      table: table,
      imported_func: function () {
        return instance2.exports.create_array();
      }
    }
  });
  table.set(0, instance1.exports.create_array); // Wasm function, same module.
  table.set(1, instance1.exports.imported_func); // JS function.
  table.set(2, instance2.exports.create_array); // Wasm function, other module.

  instance1.exports.test_return_callref();
  instance1.exports.test_return_call_imported();
  instance1.exports.test_return_call_indirect_wasm_same_instance();
  instance1.exports.test_return_call_indirect_wasm_different_instance();
  instance1.exports.test_return_call_indirect_js();
})();

(function testRefLocalsWithTailCall() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  let array_index1 = builder.addArray(kWasmI16, true, kNoSuperType, true);
  let array_index2 = builder.addArray(kWasmI16, true, kNoSuperType, false);

  builder.addFunction("main", kSig_i_i)
    .addLocals(wasmRefNullType(array_index1), 2)
    .addBody([
      kExprLocalGet, 0,
      kExprIf, kWasmVoid,
        kExprLocalGet, 0,
        kExprReturn,
      kExprEnd,
      kExprLocalGet, 1,
      kGCPrefix, kExprRefCastNull, array_index2,
      ...wasmI32Const(1),
      kExprReturnCall, 0,
    ])
    .exportAs("main");

  let instance = builder.instantiate({});
  assertEquals(1, instance.exports.main(0));
})();
