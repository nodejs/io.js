// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function assertGlobalIsValid(global) {
  assertSame(WebAssembly.Global.prototype, global.__proto__);
  assertSame(WebAssembly.Global, global.constructor);
  assertTrue(global instanceof Object);
  assertTrue(global instanceof WebAssembly.Global);
}

(function TestConstructor() {

  assertTrue(WebAssembly.Global instanceof Function);
  assertSame(WebAssembly.Global, WebAssembly.Global.prototype.constructor);

  assertThrows(() => new WebAssembly.Global(), TypeError);
  assertThrows(() => new WebAssembly.Global(1), TypeError);
  assertThrows(() => new WebAssembly.Global(""), TypeError);

  assertThrows(() => new WebAssembly.Global({}), TypeError);
  assertThrows(() => new WebAssembly.Global({value: 'foo'}), TypeError);
  assertThrows(() => new WebAssembly.Global({value: 'i128'}), TypeError);

  for (let type of ['i32', 'f32', 'f64', 'i64', 'externref', 'anyfunc']) {
    assertGlobalIsValid(new WebAssembly.Global({value: type}));
  }
})();

// Copied from //src/v8/test/common/value-helper.h
const u32_values = [
  0x00000000, 0x00000001, 0xFFFFFFFF, 0x1B09788B, 0x04C5FCE8, 0xCC0DE5BF,
  // This row is useful for testing lea optimizations on intel.
  0x00000002, 0x00000003, 0x00000004, 0x00000005, 0x00000008, 0x00000009,
  0x273A798E, 0x187937A3, 0xECE3AF83, 0x5495A16B, 0x0B668ECC, 0x11223344,
  0x0000009E, 0x00000043, 0x0000AF73, 0x0000116B, 0x00658ECC, 0x002B3B4C,
  0x88776655, 0x70000000, 0x07200000, 0x7FFFFFFF, 0x56123761, 0x7FFFFF00,
  0x761C4761, 0x80000000, 0x88888888, 0xA0000000, 0xDDDDDDDD, 0xE0000000,
  0xEEEEEEEE, 0xFFFFFFFD, 0xF0000000, 0x007FFFFF, 0x003FFFFF, 0x001FFFFF,
  0x000FFFFF, 0x0007FFFF, 0x0003FFFF, 0x0001FFFF, 0x0000FFFF, 0x00007FFF,
  0x00003FFF, 0x00001FFF, 0x00000FFF, 0x000007FF, 0x000003FF, 0x000001FF,
  // Bit pattern of a quiet NaN and signaling NaN, with or without
  // additional payload.
  0x7FC00000, 0x7F800000, 0x7FFFFFFF, 0x7F876543
];

const f32_values = [
  -Infinity,
  -2.70497e+38,
  -1.4698e+37,
  -1.22813e+35,
  -1.20555e+35,
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
  -6.35647e+08,
  -4.00037e+08,
  -1.81227e+08,
  -5.09256e+07,
  -964300.0,
  -192446.0,
  -28455.0,
  -27194.0,
  -26401.0,
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
  -5.07594e-31,
  -3.65799e-31,
  -1.43718e-34,
  -1.27126e-38,
  -0.0,
  0.0,
  1.17549e-38,
  1.56657e-37,
  4.08512e-29,
  3.31357e-28,
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
  2.61152e+33,
  1.38178e+37,
  1.71306e+37,
  3.31899e+38,
  3.40282e+38,
  Infinity,
  NaN
];

const f64_values = [
  -2e66,
  -2.220446049250313e-16,
  -9223373136366403584.0,
  -9223372036854775808.0,  // INT64_MIN
  -2147483649.5,
  -2147483648.25,
  -2147483648.0,
  -2147483647.875,
  -2147483647.125,
  -2147483647.0,
  -999.75,
  -2e66,
  -1.75,
  -1.5707963267948966,
  -1.0,
  -0.5,
  -0.0,
  0.0,
  3e-88,
  0.125,
  0.25,
  0.375,
  0.5,
  1.0,
  1.17549e-38,
  1.56657e-37,
  1.0000001,
  1.25,
  1.5707963267948966,
  2,
  3.1e7,
  5.125,
  6.25,
  888,
  982983.25,
  2147483647.0,
  2147483647.375,
  2147483647.75,
  2147483648.0,
  2147483648.25,
  2147483649.25,
  9223372036854775808.0,  // INT64_MAX + 1
  9223373136366403584.0,
  18446744073709551616.0,  // UINT64_MAX + 1
  2e66,
  Infinity,
  -Infinity,
  NaN
];

function GlobalI32(value, mutable = false) {
  return new WebAssembly.Global({value: 'i32', mutable}, value);
}

function GlobalI64(mutable = false) {
  let builder = new WasmModuleBuilder();
  builder.addGlobal(kWasm64, mutable, false).exportAs('i64');
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module);
  return instance.exports.i64;
}

function GlobalF32(value, mutable = false) {
  return new WebAssembly.Global({value: 'f32', mutable}, value);
}

function GlobalF64(value, mutable = false) {
  return new WebAssembly.Global({value: 'f64', mutable}, value);
}

(function TestDefaultValue() {
  assertSame(0, GlobalI32().value);
  assertSame(0, GlobalF32().value);
  assertSame(0, GlobalF64().value);
})();

(function TestValueOf() {
  assertTrue(WebAssembly.Global.prototype.valueOf instanceof Function);
  assertSame(0, WebAssembly.Global.prototype.valueOf.length);

  for (let u32_value of u32_values) {
    let i32_value = u32_value | 0;

    assertSame(i32_value, GlobalI32(u32_value).valueOf());
    assertSame(i32_value, GlobalI32(i32_value).valueOf());
  }

  assertThrows(() => GlobalI64().valueOf());

  for (let f32_value of f32_values) {
    assertSame(Math.fround(f32_value), GlobalF32(f32_value).valueOf());
  }

  for (let f64_value of f64_values) {
    assertSame(f64_value, GlobalF64(f64_value).valueOf());
  }
})();

(function TestGetValue() {
  let getter =
      Object.getOwnPropertyDescriptor(WebAssembly.Global.prototype, 'value')
          .get;
  assertTrue(getter instanceof Function);
  assertSame(0, getter.length);

  for (let u32_value of u32_values) {
    let i32_value = u32_value | 0;
    assertSame(i32_value, GlobalI32(u32_value).value);
    assertSame(i32_value, GlobalI32(i32_value).value);
  }

  assertThrows(() => GlobalI64().value);

  for (let f32_value of f32_values) {
    assertSame(Math.fround(f32_value), GlobalF32(f32_value).value);
  }

  for (let f64_value of f64_values) {
    assertSame(f64_value, GlobalF64(f64_value).value);
  }
})();

(function TestSetValueImmutable() {
  assertThrows(() => GlobalI32().value = 0);
  assertThrows(() => GlobalI64().value = 0);
  assertThrows(() => GlobalF32().value = 0);
  assertThrows(() => GlobalF64().value = 0);
})();

(function TestSetValue() {
  let setter =
      Object.getOwnPropertyDescriptor(WebAssembly.Global.prototype, 'value')
          .set;
  assertTrue(setter instanceof Function);
  assertSame(1, setter.length);

  for (let u32_value of u32_values) {
    let i32_value = u32_value | 0;

    let global = GlobalI32(0, true);
    global.value = u32_value;
    assertSame(i32_value, global.value);

    global.value = i32_value;
    assertSame(i32_value, global.value);
  }

  assertThrows(() => GlobalI64(true).value = 0);

  for (let f32_value of f32_values) {
    let global = GlobalF32(0, true);
    global.value = f32_value;
    assertSame(Math.fround(f32_value), global.value);
  }

  for (let f64_value of f64_values) {
    let global = GlobalF64(0, true);
    global.value = f64_value;
    assertSame(f64_value, global.value);
  }
})();
