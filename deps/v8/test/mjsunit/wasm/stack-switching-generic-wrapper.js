// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-generic-wrapper --expose-gc --allow-natives-syntax
// Flags: --experimental-wasm-stack-switching

// Test the stack-switching export wrapper in combination with the generic
// import wrapper, in particular to test that the generic wrapper switches to
// the central stack.
d8.file.execute("test/mjsunit/wasm/stack-switching-export.js");

// Throw an exception from JS, catch it in wasm and then overflow the stack.
// This tests the implicit stack switch when the exception leaves JS (central
// stack) and re-enters wasm (secondary stack). If the stack limit is not
// updated during unwinding, the stack overflow is likely to crash.
(function testGenericWrapperException() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let func_index = builder.addImport("mod", "func", kSig_v_v);
  builder.addFunction("main", kSig_v_v)
    .addBody([
      kExprTry, kWasmVoid,
        kExprCallFunction, func_index,
      kExprCatchAll,
      kExprEnd,
      kExprCallFunction, 2
    ])
    .exportFunc();
  builder.addFunction("stackOverflow", kSig_v_v)
      .addBody([
          kExprCallFunction, 2
      ]);

  function import_func() {
    %CheckIsOnCentralStack();
    throw new Error();
  }

  let instance = builder.instantiate({ mod: { func: import_func } });
  let main = WebAssembly.promising(instance.exports.main);
  assertThrowsAsync(main(), RangeError, /Maximum call stack size exceeded/);
})();
