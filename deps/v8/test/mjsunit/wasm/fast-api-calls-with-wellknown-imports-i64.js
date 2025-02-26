// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbo-fast-api-calls --expose-fast-api --no-liftoff --wasm-fast-api
// Flags: --wasm-lazy-compilation
// Flags: --no-wasm-native-module-cache
// Flags: --fast-api-allow-float-in-sim

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function TestI64AsNumber(type1, v1, type2, v2, typeE, expected) {
  const fast_c_api = new d8.test.FastCAPI();
  const boundImport =
      Function.prototype.call.bind(fast_c_api.sum_int64_as_number);

  const builder = new WasmModuleBuilder();
  const sig = makeSig(
      [kWasmExternRef, type1, type2],
      [typeE],
  );
  const imp_index = builder.addImport('mod', 'foo', sig);
  builder.addFunction('main', sig)
      .addBody([
        kExprLocalGet, 0, // receiver
        kExprLocalGet, 1, // param
        kExprLocalGet, 2, // param
        kExprCallFunction, imp_index
      ])
      .exportFunc();

  const instance = builder.instantiate({'mod': {'foo': boundImport}});

  fast_c_api.reset_counts();
  assertEquals(expected, instance.exports.main(fast_c_api, v1, v2));
  assertEquals(1, fast_c_api.fast_call_count());
}

TestI64AsNumber(kWasmF64, 2.5, kWasmF64, 3.5, kWasmF64, 5);
TestI64AsNumber(kWasmF32, 2.5, kWasmF32, 3.5, kWasmF32, 5);
TestI64AsNumber(kWasmI32, 2, kWasmI32, 3, kWasmI32, 5);

TestI64AsNumber(kWasmF64, 2.5, kWasmF32, 3.5, kWasmI32, 5);
TestI64AsNumber(kWasmF32, 2.5, kWasmF64, 3.5, kWasmF32, 5);
TestI64AsNumber(kWasmI32, 2, kWasmF64, 3.5, kWasmF32, 5);
TestI64AsNumber(kWasmF32, 2.5, kWasmI32, 3, kWasmF64, 5);

assertThrows(
    () => TestI64AsNumber(kWasmF64, 2 ** 66, kWasmI32, 0, kWasmF64), Error,
    `First number is out of int64_t range.`);
assertThrows(
    () => TestI64AsNumber(kWasmF32, 2 ** 66, kWasmI32, 0, kWasmF64), Error,
    `First number is out of int64_t range.`);

function TestI64AsBigInt(type1, v1, type2, v2, typeE, expected, errorMessage) {
  const fast_c_api = new d8.test.FastCAPI();
  const boundImport =
      Function.prototype.call.bind(fast_c_api.sum_int64_as_bigint);

  const builder = new WasmModuleBuilder();
  const sig = makeSig(
      [kWasmExternRef, type1, type2],
      [typeE],
  );
  const imp_index = builder.addImport('mod', 'foo', sig);
  builder.addFunction('main', sig)
      .addBody([
        kExprLocalGet, 0, // receiver
        kExprLocalGet, 1, // param
        kExprLocalGet, 2, // param
        kExprCallFunction, imp_index
      ])
      .exportFunc();

  const instance = builder.instantiate({'mod': {'foo': boundImport}});

  fast_c_api.reset_counts();

  if (typeof expected === "bigint") {
    assertEquals(expected, instance.exports.main(fast_c_api, v1, v2));
    assertEquals(0, fast_c_api.slow_call_count());
    assertEquals(1, fast_c_api.fast_call_count());
  } else {
    assertThrows(
        () => instance.exports.main(fast_c_api, v1, v2), Error, expected);
  }
}

TestI64AsBigInt(kWasmI64, 2n, kWasmI64, 3n, kWasmI64, 5n);
TestI64AsBigInt(kWasmI64, 2n, kWasmI64, 3n, kWasmF64, "Cannot convert a BigInt value to a number");
TestI64AsBigInt(kWasmF32, 2.5, kWasmI64, 3n, kWasmI64,"Did not get a BigInt as first parameter.");
TestI64AsBigInt(kWasmI64, 2n, kWasmI64, 3n, kWasmI32, "Cannot convert a BigInt value to a number");

TestI64AsBigInt(kWasmF64, 2.5, kWasmF32, 3.5, kWasmI32, "Did not get a BigInt as first parameter.");
TestI64AsBigInt(kWasmF32, 2.5, kWasmF64, 3.5, kWasmF32, "Did not get a BigInt as first parameter.");
TestI64AsBigInt(kWasmI32, 2, kWasmF64, 3.5, kWasmF32, "Did not get a BigInt as first parameter.");
TestI64AsBigInt(kWasmF32, 2.5, kWasmI32, 3, kWasmF64, "Did not get a BigInt as first parameter.");

function TestU64AsNumber(type1, v1, type2, v2, typeE, expected) {
  const fast_c_api = new d8.test.FastCAPI();
  const boundImport =
      Function.prototype.call.bind(fast_c_api.sum_int64_as_number);

  const builder = new WasmModuleBuilder();
  const sig = makeSig(
      [kWasmExternRef, type1, type2],
      [typeE],
  );
  const imp_index = builder.addImport('mod', 'foo', sig);
  builder.addFunction('main', sig)
      .addBody([
        kExprLocalGet, 0, // receiver
        kExprLocalGet, 1, // param
        kExprLocalGet, 2, // param
        kExprCallFunction, imp_index
      ])
      .exportFunc();

  const instance = builder.instantiate({'mod': {'foo': boundImport}});

  fast_c_api.reset_counts();
  assertEquals(expected, instance.exports.main(fast_c_api, v1, v2));
  assertEquals(1, fast_c_api.fast_call_count());
}

TestU64AsNumber(kWasmF64, 2.5, kWasmF64, 3.5, kWasmF64, 5);
TestU64AsNumber(kWasmF32, 2.5, kWasmF32, 3.5, kWasmF32, 5);
TestU64AsNumber(kWasmI32, 2, kWasmI32, 3, kWasmI32, 5);

TestU64AsNumber(kWasmF64, 2.5, kWasmF32, 3.5, kWasmI32, 5);
TestU64AsNumber(kWasmF32, 2.5, kWasmF64, 3.5, kWasmF32, 5);
TestU64AsNumber(kWasmI32, 2, kWasmF64, 3.5, kWasmF32, 5);
TestU64AsNumber(kWasmF32, 2.5, kWasmI32, 3, kWasmF64, 5);

assertThrows(
    () => TestU64AsNumber(kWasmF64, 2 ** 66, kWasmI32, 0, kWasmF64), Error,
    `First number is out of int64_t range.`);
assertThrows(
    () => TestU64AsNumber(kWasmF32, 2 ** 66, kWasmI32, 0, kWasmF64), Error,
    `First number is out of int64_t range.`);

function TestU64AsBigInt(type1, v1, type2, v2, typeE, expected, errorMessage) {
  const fast_c_api = new d8.test.FastCAPI();
  const boundImport =
      Function.prototype.call.bind(fast_c_api.sum_uint64_as_bigint);

  const builder = new WasmModuleBuilder();
  const sig = makeSig(
      [kWasmExternRef, type1, type2],
      [typeE],
  );
  const imp_index = builder.addImport('mod', 'foo', sig);
  builder.addFunction('main', sig)
      .addBody([
        kExprLocalGet, 0, // receiver
        kExprLocalGet, 1, // param
        kExprLocalGet, 2, // param
        kExprCallFunction, imp_index
      ])
      .exportFunc();

  const instance = builder.instantiate({'mod': {'foo': boundImport}});

  fast_c_api.reset_counts();

  if (typeof expected === "bigint") {
    assertEquals(expected, instance.exports.main(fast_c_api, v1, v2));
    assertEquals(0, fast_c_api.slow_call_count());
    assertEquals(1, fast_c_api.fast_call_count());
  } else {
    assertThrows(
        () => instance.exports.main(fast_c_api, v1, v2), Error, expected);
  }
}

TestU64AsBigInt(kWasmI64, 2n, kWasmI64, 3n, kWasmI64, 5n);
TestU64AsBigInt(kWasmI64, 2n, kWasmI64, 3n, kWasmF64, "Cannot convert a BigInt value to a number");
TestU64AsBigInt(kWasmF32, 2.5, kWasmI64, 3n, kWasmI64,"Did not get a BigInt as first parameter.");
TestU64AsBigInt(kWasmI64, 2n, kWasmI64, 3n, kWasmI32, "Cannot convert a BigInt value to a number");

TestU64AsBigInt(kWasmF64, 2.5, kWasmF32, 3.5, kWasmI32, "Did not get a BigInt as first parameter.");
TestU64AsBigInt(kWasmF32, 2.5, kWasmF64, 3.5, kWasmF32, "Did not get a BigInt as first parameter.");
TestU64AsBigInt(kWasmI32, 2, kWasmF64, 3.5, kWasmF32, "Did not get a BigInt as first parameter.");
TestU64AsBigInt(kWasmF32, 2.5, kWasmI32, 3, kWasmF64, "Did not get a BigInt as first parameter.");
