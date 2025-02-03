// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turboshaft-wasm-in-js-inlining
// Flags: --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --no-always-sparkplug
// Only tier-up the test functions to get a cleaner and stable trace.
// Flags: --trace-turbo-inlining --turbo-filter='js_*'
// Concurrent inlining leads to additional traces.
// Flags: --no-stress-concurrent-inlining

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");
d8.file.execute("test/mjsunit/mjsunit.js");

const builder = new WasmModuleBuilder();
const array = builder.addArray(kWasmI32, true);
const globalI32 = builder.addGlobal(kWasmI32, true, false);
const globalEqRef = builder.addGlobal(kWasmEqRef, true, false);

let tests = [];
// A helper to avoid repetition (e.g., the Wasm function name) and keep all
// pieces of one test case in one place (e.g., JS arguments, Wasm signature).
function addTestcase(name, wasmSignature, wasmArguments, wasmBody, jsFunctionSrc) {
  assertEquals(wasmSignature.params.length, wasmArguments.length);
  const wasmFunction = builder.addFunction(name, wasmSignature).addBody(wasmBody).exportFunc();

  if (!jsFunctionSrc) {
    // Pass the arguments from the JS function to the Wasm function so that they
    // are not constant after inlining (otherwise constant folding often removes
    // the code from the inlinee).
    // Also, do not use the spread syntax (...args) to call the Wasm export, but
    // instead explicitly list the arguments. Otherwise the JS-to-Wasm wrapper
    // will not be inlined.
    // TODO(353475584): Is it worth lifting this restriction? Is this a
    // restriction also for regular JS inlining?
    const argumentList = Array.from(wasmArguments, (x, i) => 'arg' + i);
    jsFunctionSrc = `
      function js_${name}(${argumentList}) {
          return wasmExports.${name}(${argumentList});
      }`;
  }
  tests[name] = { wasmArguments, jsFunctionSrc };

    // Return the wasmFunction, so that it can still be manually modified.
  return wasmFunction;
}

// =============================================================================
// Testcases where both JS-to-Wasm wrapper inlining and Wasm body inlining
// succeed:
// =============================================================================

addTestcase('empty', kSig_v_v, [], []);
addTestcase('nop', kSig_v_v, [], [kExprNop]);

addTestcase('i32Const', kSig_i_v, [], [...wasmI32Const(42)]);
addTestcase('f32Const', kSig_f_v, [], [...wasmF32Const(42.0)]);
addTestcase('f64Const', kSig_d_v, [], [...wasmF64Const(42.0)]);

function addUnaryTestcase(op, wasmSignature, wasmArgument) {
  addTestcase(op, wasmSignature, [wasmArgument], [
    kExprLocalGet, 0,
    eval('kExpr' + op)
  ]);
}
addUnaryTestcase('I32Eqz', kSig_i_i, 0);
addUnaryTestcase('F32Abs', kSig_f_f, 0);
addUnaryTestcase('F32Neg', kSig_f_f, 0);
addUnaryTestcase('F32Sqrt', kSig_f_f, 0);
addUnaryTestcase('F64Abs', kSig_d_d, 0);
addUnaryTestcase('F64Neg', kSig_d_d, 0);
addUnaryTestcase('F64Sqrt', kSig_d_d, 0);
addUnaryTestcase('F64SConvertI32', kSig_d_i, 0);
addUnaryTestcase('F64UConvertI32', kSig_d_i, 0);
addUnaryTestcase('F32SConvertI32', kSig_f_i, 0);
addUnaryTestcase('F32UConvertI32', kSig_f_i, 0);
addUnaryTestcase('F32ConvertF64', kSig_f_d, 0);
addUnaryTestcase('F64ConvertF32', kSig_d_f, 0);
addUnaryTestcase('F32ReinterpretI32', kSig_f_i, 0);
addUnaryTestcase('I32ReinterpretF32', kSig_i_f, 0);
addUnaryTestcase('I32Clz', kSig_i_i, 0);
addUnaryTestcase('I32SExtendI8', kSig_i_i, 0);
addUnaryTestcase('I32SExtendI16', kSig_i_i, 0);
addUnaryTestcase('RefIsNull', kSig_i_r, null);
// We cannot pass Wasm any refs across the JS-Wasm boundary, hence convert to
// any and back in one test.
// TODO(dlehmann): Update output once arm64 no-ptr-compression calls are fixed.
addTestcase('anyConvertExternConvertAny', kSig_r_r, [{}], [
  kExprLocalGet, 0,
  kGCPrefix, kExprAnyConvertExtern,
  kGCPrefix, kExprExternConvertAny,
]);

function addBinaryTestcase(op, wasmSignature, wasmArgument0, wasmArgument1) {
  addTestcase(op, wasmSignature, [wasmArgument0, wasmArgument1], [
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    eval('kExpr' + op)
  ]);
}
addBinaryTestcase('I32Add', kSig_i_ii, 3, 7);
addBinaryTestcase('I32Sub', kSig_i_ii, 3, 7);
addBinaryTestcase('I32Mul', kSig_i_ii, 3, 7);
addBinaryTestcase('I32And', kSig_i_ii, 3, 7);
addBinaryTestcase('I32Ior', kSig_i_ii, 3, 7);
addBinaryTestcase('I32Xor', kSig_i_ii, 3, 7);
addBinaryTestcase('I32Shl', kSig_i_ii, 3, 7);
addBinaryTestcase('I32ShrS', kSig_i_ii, 3, 7);
addBinaryTestcase('I32ShrU', kSig_i_ii, 3, 7);
addBinaryTestcase('I32Ror', kSig_i_ii, 3, 7);
addBinaryTestcase('I32Rol', kSig_i_ii, 3, 7);
addBinaryTestcase('I32Eq', kSig_i_ii, 3, 7);
addBinaryTestcase('I32Ne', kSig_i_ii, 3, 7);
addBinaryTestcase('I32LtS', kSig_i_ii, 3, 7);
addBinaryTestcase('I32LeS', kSig_i_ii, 3, 7);
addBinaryTestcase('I32LtU', kSig_i_ii, 3, 7);
addBinaryTestcase('I32LeU', kSig_i_ii, 3, 7);
addBinaryTestcase('I32GtS', kSig_i_ii, 3, 7);
addBinaryTestcase('I32GeS', kSig_i_ii, 3, 7);
addBinaryTestcase('I32GtU', kSig_i_ii, 3, 7);
addBinaryTestcase('I32GeU', kSig_i_ii, 3, 7);
addBinaryTestcase('F32CopySign', kSig_f_ff, 3, 7);
addBinaryTestcase('F32Add', kSig_f_ff, 3, 7);
addBinaryTestcase('F32Sub', kSig_f_ff, 3, 7);
addBinaryTestcase('F32Mul', kSig_f_ff, 3, 7);
addBinaryTestcase('F32Div', kSig_f_ff, 3, 7);
addBinaryTestcase('F32Eq', kSig_i_ff, 3, 7);
addBinaryTestcase('F32Ne', kSig_i_ff, 3, 7);
addBinaryTestcase('F32Lt', kSig_i_ff, 3, 7);
addBinaryTestcase('F32Le', kSig_i_ff, 3, 7);
addBinaryTestcase('F32Gt', kSig_i_ff, 3, 7);
addBinaryTestcase('F32Ge', kSig_i_ff, 3, 7);
addBinaryTestcase('F32Min', kSig_f_ff, 3, 7);
addBinaryTestcase('F32Max', kSig_f_ff, 3, 7);
addBinaryTestcase('F64Add', kSig_d_dd, 3, 7);
addBinaryTestcase('F64Sub', kSig_d_dd, 3, 7);
addBinaryTestcase('F64Mul', kSig_d_dd, 3, 7);
addBinaryTestcase('F64Div', kSig_d_dd, 3, 7);
addBinaryTestcase('F64Eq', kSig_i_dd, 3, 7);
addBinaryTestcase('F64Ne', kSig_i_dd, 3, 7);
addBinaryTestcase('F64Lt', kSig_i_dd, 3, 7);
addBinaryTestcase('F64Le', kSig_i_dd, 3, 7);
addBinaryTestcase('F64Gt', kSig_i_dd, 3, 7);
addBinaryTestcase('F64Ge', kSig_i_dd, 3, 7);
addBinaryTestcase('F64Min', kSig_d_dd, 3, 7);
addBinaryTestcase('F64Max', kSig_d_dd, 3, 7);
// This currently cannot be inlined when passing `eqref`s as an argument from JS
// because the JS-to-Wasm wrapper inlining bails out in that case.
// So go through through globals instead.
addTestcase('RefEq', kSig_i_v, [], [
  kExprGlobalGet, globalEqRef.index,
  kExprGlobalGet, globalEqRef.index,
  kExprRefEq,
])

// Function arguments and locals.
addTestcase('passthroughI32', kSig_i_i, [13], [kExprLocalGet, 0]);
addTestcase('localTee', kSig_i_i, [42], [
  kExprLocalGet, 0,
  ...wasmI32Const(7),
  kExprLocalTee, 1,
  kExprI32Add,
  kExprLocalGet, 1,
  kExprI32Add,
]).addLocals(kWasmI32, 1);
addTestcase('localSwap', kSig_i_i, [42], [
  ...wasmI32Const(3),
  kExprLocalSet, 2,  // Initialize local_2 = 3.

  // Ring-swap: local 2 to parameter, local 1 to local 2, parameter to local 1,
  // stack as temporary.
  kExprLocalGet, 0,
  kExprLocalGet, 2,
  kExprLocalSet, 0,
  kExprLocalGet, 1,
  kExprLocalSet, 2,
  kExprLocalSet, 1,

  // Use subtraction since it's not commutative.
  kExprLocalGet, 2,  // value: 0
  kExprLocalGet, 1,  // value: parameter
  kExprI32Sub,
  kExprLocalGet, 0,  // value: 3
  kExprI32Sub,  // = (0 - p) - 3 = -10
]).addLocals(kWasmI32, 2);

addTestcase('globalSetGet', kSig_i_i, [42], [
  kExprLocalGet, 0,
  kExprGlobalSet, globalI32.index,
  kExprGlobalGet, globalI32.index,
  kExprGlobalGet, globalI32.index,
  kExprI32Add,
  kExprGlobalGet, globalI32.index,
  kExprI32Add,
]);

// =============================================================================
// Testcases that we (currently) do not inline (the JS-to-Wasm wrapper or body):
// =============================================================================

// TODO(dlehmann,353475584): We want to support the following `arrayLen`
// testcase soon to reach feature parity with the TurboFan Wasm-in-JS inlining,
// but it's not there yet. `createArray` requires allocation, which might get
// complicated with allocation folding, so that's not in the MVP yet.
addTestcase('createArray', makeSig([kWasmI32], [kWasmExternRef]), [42], [
  kExprLocalGet, 0,
  kGCPrefix, kExprArrayNewDefault, array,
  kGCPrefix, kExprExternConvertAny,
]);
addTestcase('arrayLen', makeSig([kWasmExternRef], [kWasmI32]), [null], [
  kExprLocalGet, 0,
  kGCPrefix, kExprAnyConvertExtern,
  kGCPrefix, kExprRefCastNull, array,
  kGCPrefix, kExprArrayLen,
], `
const array = wasmExports.createArray(42);
function js_arrayLen() {
  wasmExports.arrayLen(array);
}`);

// TODO(dlehmann,353475584): Support i64 in Wasm signatures when inlining the
// JS-to-Wasm wrapper on 32-bit architectures.
// addTestcase('passthroughI64', kSig_l_l, [13n], [kExprLocalGet, 0]);

// TODO(dlehmann,353475584): We don't support i64 ops yet, since that requires
// `Int64LoweringReducer`, which isn't compatible with the JS pipeline yet.
// Also, there is no wrapper inlining for i64 on 32-bit architectures right
// now, so pass as i32 on the boundary.
addTestcase(`i64Add`, kSig_i_ii, [13, 23], [
  kExprLocalGet, 0,
  kExprI64SConvertI32,
  kExprLocalGet, 1,
  kExprI64SConvertI32,
  kExprI64Add,
  kExprI32ConvertI64,
]);

// TODO(dlehmann,353475584): This would require supporting multi-value in
// JS-to-Wasm wrapper inlining first. (Right now, we bailout already at this
// first step.)
addTestcase('multiValue', kSig_ii_v, [], [
  ...wasmI32Const(3),
  ...wasmI32Const(7),
]);

// TODO(dlehmann,353475584): Support control-flow in the inlinee (much later).
addTestcase('brNoInline', kSig_i_v, [], [
  ...wasmI32Const(42),
  // Need to use a `br`anch here, as `return` is actually optimized.
  kExprBr, 0,
]);

addTestcase('trapNoInline', kSig_v_v, [], [
  kExprUnreachable,
], `function js_trapNoInline() {
  try {
    wasmExports.trapNoInline();
  } catch (e) {
    return e.toString();
  }
}`);


// Compile and instantiate the Wasm module, define the caller JS functions,
// call with arguments defined above.
const wasmExports = builder.instantiate({}).exports;
for (const [ name, { wasmArguments, jsFunctionSrc } ] of Object.entries(tests)) {
  print(`\nTest: ${name}`);

  // Use `eval` to define the JS functions, such that they have a more useful
  // name during debugging and in the `--trace-turbo` output. (Otherwise they
  // would be all called `jsFunction-0`, `-1`, etc.).
  // We also cannot use the spread operator but need to generate slightly
  // different code in the JS callers, see above.
  eval(jsFunctionSrc);
  const jsFunction = eval('js_' + name);

  %PrepareFunctionForOptimization(jsFunction);
  let resultUnopt = jsFunction(...wasmArguments);
  assertUnoptimized(jsFunction);

  %OptimizeFunctionOnNextCall(jsFunction);
  let resultOpt = jsFunction(...wasmArguments);
  assertOptimized(jsFunction);

  assertEquals(resultUnopt, resultOpt);
}
