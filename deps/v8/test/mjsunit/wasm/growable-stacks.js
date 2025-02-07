// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --experimental-wasm-jspi
// Flags: --expose-gc --wasm-stack-switching-stack-size=4
// Flags: --experimental-wasm-growable-stacks
// Flags: --stack-size=400 --turboshaft-wasm

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function flatRange(upperBound, gen) {
  const res = [];
  for (let i = 0; i < upperBound; i++) {
    res.push(gen(i));
  }
  return res.flat();
}

(function TestStackGrow() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const call_stack_depth = 430;
  builder.addGlobal(kWasmI32, true).exportAs('depth');
  const numIntArgs = 10;
  const numTypes = Array(numIntArgs).fill(kWasmI32);
  var sig = makeSig(numTypes, numTypes);
  builder.addImport('m', 'import', sig);
  let sig_if = builder.addType(makeSig([], numTypes));
  let deep_calc = builder.addFunction("deep_calc", sig);
  deep_calc.addBody([
      // decrement global
      kExprGlobalGet, 0,
      kExprI32Const, 1,
      kExprI32Sub,
      kExprGlobalSet, 0,
      // should we recurse deeper?
      kExprGlobalGet, 0,
      kExprIf, sig_if,
      // then
      kExprLocalGet, 9,
      kExprI32Const, 1,
      kExprI32Add,
      ...flatRange(numIntArgs - 1, i => [kExprLocalGet, i]),
      kExprCallFunction, deep_calc.index,
      // else
      kExprElse,
      ...flatRange(numIntArgs, i => [kExprLocalGet, i]),
      kExprCallFunction, 0,
      kExprEnd
    ])
  builder.addFunction("test", makeSig([], numTypes))
    .addBody([
      // do stack expensive job
      ...flatRange(numIntArgs, i => [kExprI32Const, i]),
      kExprCallFunction, deep_calc.index,
      ...flatRange(numIntArgs, () => kExprDrop),
      // Reset the recursion budget.
      ...wasmI32Const(call_stack_depth),
      kExprGlobalSet, 0,
      // do more stack expensive job
      // to check limits was updated properly on shrink
      ...flatRange(numIntArgs, i => [kExprI32Const, i]),
      kExprCallFunction, deep_calc.index,
    ]).exportFunc();
  const js_import = new WebAssembly.Suspending((...args) => {
    return Promise.resolve(args)
  })
  const instance = builder.instantiate({ m: { import: js_import } });
  const wrapper = WebAssembly.promising(instance.exports.test);
  instance.exports.depth.value = call_stack_depth;
  assertPromiseResult(
    wrapper(),
    r => {
      [44,45,46,47,48,49,50,51,52,42].forEach(
        (v, i) => assertEquals(r[i], v)
      )
    }
  );
})();

(function TestStackGrowWithGCAndRefParams() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const call_stack_depth = 430;
  builder.addGlobal(kWasmI32, true).exportAs('depth');
  const numRefArgs = 10;
  const refTypes = Array(numRefArgs).fill(kWasmExternRef);
  var sig = makeSig(refTypes, refTypes);
  const imp = builder.addImport('m', 'import', sig);
  let sig_if = builder.addType(makeSig([], refTypes));
  let deep_calc = builder.addFunction("deep_calc", sig);
  deep_calc.addBody([
      // decrement global
      kExprGlobalGet, 0,
      kExprI32Const, 1,
      kExprI32Sub,
      kExprGlobalSet, 0,
      // should we recurse deeper?
      kExprGlobalGet, 0,
      kExprIf, sig_if,
      // then
      ...flatRange(numRefArgs, i => [kExprLocalGet, i]),
      kExprCallFunction, deep_calc.index,
      // else
      kExprElse,
      ...flatRange(numRefArgs, i => [kExprLocalGet, i]),
      kExprCallFunction, imp,
      kExprEnd
    ]);
  builder.addFunction("test", makeSig([kWasmExternRef], refTypes))
    .addBody([
      // do stack expensive job
      ...flatRange(numRefArgs, () => [kExprLocalGet, 0]),
      kExprCallFunction, deep_calc.index,
      ...flatRange(numRefArgs, () => kExprDrop),
      // Reset the recursion budget.
      ...wasmI32Const(call_stack_depth),
      kExprGlobalSet, 0,
      // do more stack expensive job
      // to check limits was updated properly on shrink
      ...flatRange(numRefArgs, () => [kExprLocalGet, 0]),
      kExprCallFunction, deep_calc.index,
    ]).exportFunc();
  const js_import = new WebAssembly.Suspending((...args) => {
    gc();
    return Promise.resolve(args)
  });
  const instance = builder.instantiate({ m: { import: js_import } });
  const wrapper = WebAssembly.promising(instance.exports.test);
  instance.exports.depth.value = call_stack_depth;
  const ref = {};
  assertPromiseResult(
    wrapper(ref),
    res => res.forEach(r => assertEquals(r, ref))
  );
})();

(function TestStackGrowHeavyFrame() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const call_stack_depth = 10;
  builder.addGlobal(kWasmI32, true).exportAs('depth');
  const numIntArgs = 10;
  const numTypes = Array(numIntArgs).fill(kWasmI32);
  var sig = makeSig(numTypes, numTypes);
  const imp = builder.addImport('m', 'import', sig);
  let sig_if = builder.addType(makeSig([], numTypes));
  let deep_calc = builder.addFunction("deep_calc", sig);
  deep_calc
    .addLocals(kWasmI64, 512) // make a frame bigger than 4kb
    .addBody([
      // decrement global
      kExprGlobalGet, 0,
      kExprI32Const, 1,
      kExprI32Sub,
      kExprGlobalSet, 0,
      // should we recurse deeper?
      kExprGlobalGet, 0,
      kExprIf, sig_if,
      // then
      kExprLocalGet, 9,
      kExprI32Const, 1,
      kExprI32Add,
      ...flatRange(numIntArgs - 1, i => [kExprLocalGet, i]),
      kExprCallFunction, deep_calc.index,
      // else
      kExprElse,
      ...flatRange(numIntArgs, i => [kExprLocalGet, i]),
      kExprCallFunction, imp,
      kExprEnd
    ]);
  builder.addFunction("test", makeSig([], numTypes))
    .addBody([
      // do stack expensive job
      ...flatRange(numIntArgs, i => [kExprI32Const, i + 1]),
      kExprCallFunction, deep_calc.index,
      ...Array(numIntArgs).fill(kExprDrop),
      // Reset the recursion budget.
      ...wasmI32Const(call_stack_depth),
      kExprGlobalSet, 0,
      // do more stack expensive job
      // to check limits was updated properly on shrink
      ...flatRange(numIntArgs, i => [kExprI32Const, i + 1]),
      kExprCallFunction, deep_calc.index,
    ]).exportFunc();
  const js_import = new WebAssembly.Suspending((...args) => {
    return Promise.resolve(args);
  })
  const instance = builder.instantiate({ m: { import: js_import } });
  const wrapper = WebAssembly.promising(instance.exports.test);
  instance.exports.depth.value = call_stack_depth;
  assertPromiseResult(
    wrapper(),
    r => {
      [3,4,5,6,7,8,9,10,11,1].forEach(
        (v, i) => assertEquals(r[i], v)
      )
    }
  );
})();

(function TestStackOverflow() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("test", kSig_i_v)
      .addBody([
          kExprCallFunction, 0
          ]).exportFunc();
  let instance = builder.instantiate();
  let wrapper = WebAssembly.promising(instance.exports.test);
  assertThrowsAsync(wrapper(), RangeError, /Maximum call stack size exceeded/);
})();

(function TestStackOverflowWithHeavyFrames() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("test", kSig_i_r)
      .addLocals(kWasmI64, 512) // make a frame bigger than 4kb
      .addBody([
          kExprLocalGet, 0,
          kExprCallFunction, 0
          ]).exportFunc();
  let instance = builder.instantiate();
  let wrapper = WebAssembly.promising(instance.exports.test);
  assertThrowsAsync(wrapper(), RangeError, /Maximum call stack size exceeded/);
})();
