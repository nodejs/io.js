// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-math-intrinsics

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function verbose(args) {
  // print(...args);
}

//=============================================
// Global count of failures
//=============================================
let numFailures = 0;

function reportFailure(name, vals, m, w) {
  print('  error: ' + name + '(' + vals + ') == ' + w + ', expected ' + m);
  numFailures++;
}

let global_imports = {Math: Math};

let inputs = [
  1 / 0,
  -1 / 0,
  0 / 0,
  -2.70497e+38,
  -1.4698e+37,
  -1.22813e+35,
  -1.34584e+34,
  -1.0079e+32,
  -6.49364e+26,
  -3.06077e+25,
  -1.46821e+25,
  -1.17658e+23,
  -1.9617e+22,
  -2.7357e+20,
  -9223372036854775808.0,  // INT64_MIN
  -1.48708e+13,
  -1.89633e+12,
  -4.66622e+11,
  -2.22581e+11,
  -1.45381e+10,
  -2147483904.0,  // First float32 after INT32_MIN
  -2147483648.0,  // INT32_MIN
  -2147483520.0,  // Last float32 before INT32_MIN
  -1.3956e+09,
  -1.32951e+09,
  -1.30721e+09,
  -1.19756e+09,
  -9.26822e+08,
  -5.09256e+07,
  -964300.0,
  -192446.0,
  -28455.0,
  -27194.0,
  -20575.0,
  -17069.0,
  -9167.0,
  -960.178,
  -113.0,
  -62.0,
  -15.0,
  -7.0,
  -1.0,
  -0.0256635,
  -4.60374e-07,
  -3.63759e-10,
  -4.30175e-14,
  -5.27385e-15,
  -1.5707963267948966,
  -1.48084e-15,
  -2.220446049250313e-16,
  -1.05755e-19,
  -3.2995e-21,
  -1.67354e-23,
  -1.11885e-23,
  -1.78506e-30,
  -1.43718e-34,
  -1.27126e-38,
  -0.0,
  3e-88,
  -2e66,
  0.0,
  2e66,
  1.17549e-38,
  1.56657e-37,
  4.08512e-29,
  6.25073e-22,
  4.1723e-13,
  1.44343e-09,
  1.5707963267948966,
  5.27004e-08,
  9.48298e-08,
  5.57888e-07,
  4.89988e-05,
  0.244326,
  1.0,
  12.4895,
  19.0,
  47.0,
  106.0,
  538.324,
  564.536,
  819.124,
  7048.0,
  12611.0,
  19878.0,
  20309.0,
  797056.0,
  1.77219e+09,
  2147483648.0,  // INT32_MAX + 1
  4294967296.0,  // UINT32_MAX + 1
  1.51116e+11,
  4.18193e+13,
  3.59167e+16,
  9223372036854775808.0,   // INT64_MAX + 1
  18446744073709551616.0,  // UINT64_MAX + 1
  3.38211e+19,
  2.67488e+20,
  1.78831e+21,
  9.20914e+21,
  8.35654e+23,
  1.4495e+24,
  5.94015e+25,
  4.43608e+30,
  2.44502e+33,
  1.38178e+37,
  1.71306e+37,
  3.31899e+38,
  3.40282e+38,
];

function genUnop(name, sig) {
  var builder = new WasmModuleBuilder();

  var sig_index = builder.addType(sig);
  builder.addImport('Math', name, sig_index);
  builder.addFunction('main', sig_index)
      .addBody([
        kExprLocalGet, 0,  // --
        kExprCallFunction, 0
      ])  // --
      .exportAs('main');

  return builder.instantiate(global_imports).exports.main;
}

function genBinop(name, sig) {
  var builder = new WasmModuleBuilder();

  var sig_index = builder.addType(sig);
  builder.addImport('Math', name, sig_index);
  builder.addFunction('main', sig_index)
      .addBody([
        kExprLocalGet, 0,  // --
        kExprLocalGet, 1,  // --
        kExprCallFunction, 0
      ])  // --
      .exportAs('main');

  return builder.instantiate(global_imports).exports.main;
}

function assertUnop(name, math_func, wasm_func) {
  for (let val of inputs) {
    verbose('  ', val);
    let m = math_func(val);
    let w = wasm_func(val);
    if (!deepEquals(m, w)) reportFailure(name, [val], m, w);
  }
}

function assertBinop(name, math_func, wasm_func) {
  let inputs2 = [1, 0.5, -1, -0.5, 0, -0, 1 / 0, -1 / 0, 0 / 0];
  for (let val of inputs) {
    verbose('  ', val);
    for (let val2 of inputs2) {
      verbose('    ', val2);
      let m = math_func(val, val2);
      let w = wasm_func(val, val2);
      if (!deepEquals(m, w)) reportFailure(name, [val, val2], m, w);
      m = math_func(val2, val);
      w = wasm_func(val2, val);
      if (!deepEquals(m, w)) reportFailure(name, [val2, val], m, w);
    }
  }
}

(function TestF64() {
  let f64_intrinsics = [
    'acos', 'asin', 'atan', 'cos', 'sin', 'tan', 'exp', 'log', 'atan2', 'pow',
    'sqrt',
  ];

  for (let name of f64_intrinsics) {
    let math_func = Math[name];
    print('Testing (f64) Math.' + name);
    switch (math_func.length) {
      case 1: {
        let wasm_func = genUnop(name, kSig_d_d);
        assertUnop('(f64)' + name, math_func, wasm_func);
        break;
      }
      case 2: {
        let wasm_func = genBinop(name, kSig_d_dd);
        assertBinop('(f64)' + name, math_func, wasm_func);
        break;
      }
      default:
        throw 'Unexpected param count: ' + func.length;
    }
  }
})();

assertEquals(0, numFailures);
