// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let instance = (() => {
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  /**
   * type_producer    -> create type and pass as anyref
   *                     (implicit externalize by calling convention)
   * type_externalize -> create type, externalize, pass by externref
   * type_consumer    -> consume type by anyref
   *                     (implicit internalize by calling convention)
   * type_internalize -> consume type by externref, internalize
   */

  builder.addFunction('struct_producer', makeSig([kWasmI32], [kWasmEqRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct])
    .exportFunc();

  builder.addFunction('struct_externalize',
                      makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('struct_consumer',
                      makeSig([kWasmEqRef], [kWasmI32, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprRefIsNull,
      kExprBlock, kWasmVoid,
        kExprLocalGet, 0,
        kExprBrOnNull, 0,
        kGCPrefix, kExprRefCast, struct,
        kGCPrefix, kExprStructGet, struct, 0, // value
        kExprI32Const, 0, // isNull
        kExprReturn,
      kExprEnd,
      kExprDrop,
      kExprI32Const, 0, // value (placeholder)
      kExprI32Const, 1, // isNull
    ])
    .exportFunc();

  builder.addFunction('struct_internalize',
                      makeSig([kWasmExternRef], [kWasmI32, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprRefIsNull,
      kExprBlock, kWasmVoid,
        kExprLocalGet, 0,
        kGCPrefix, kExprAnyConvertExtern,
        kExprBrOnNull, 0,
        kGCPrefix, kExprRefCast, struct,
        kGCPrefix, kExprStructGet, struct, 0, // value
        kExprI32Const, 0, // isNull
        kExprReturn,
      kExprEnd,
      kExprDrop,
      kExprI32Const, 0, // value (placeholder)
      kExprI32Const, 1, // isNull
    ])
    .exportFunc();

    builder.addFunction('i31_producer', makeSig([kWasmI32], [kWasmEqRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprRefI31])
    .exportFunc();

    builder.addFunction('i31_externalize',
                        makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprRefI31,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

    builder.addFunction('i31_consumer',
                        makeSig([kWasmEqRef], [kWasmI32, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprRefIsNull,
      kExprBlock, kWasmVoid,
        kExprLocalGet, 0,
        kExprBrOnNull, 0,
        kGCPrefix, kExprRefCast, kI31RefCode,
        kGCPrefix, kExprI31GetS, // value
        kExprI32Const, 0, // isNull
        kExprReturn,
      kExprEnd,
      kExprDrop,
      kExprI32Const, 0, // value (placeholder)
      kExprI32Const, 1, // isNull
    ])
    .exportFunc();

    builder.addFunction('i31_internalize',
                        makeSig([kWasmExternRef], [kWasmI32, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprRefIsNull,
      kExprBlock, kWasmVoid,
        kExprLocalGet, 0,
        kGCPrefix, kExprAnyConvertExtern,
        kExprBrOnNull, 0,
        kGCPrefix, kExprRefCast, kI31RefCode,
        kGCPrefix, kExprI31GetS, // value
        kExprI32Const, 0, // isNull
        kExprReturn,
      kExprEnd,
      kExprDrop,
      kExprI32Const, 0, // value (placeholder)
      kExprI32Const, 1, // isNull
    ])
    .exportFunc();

    let array = builder.addArray(kWasmI32, true);

    builder.addFunction('array_producer', makeSig([kWasmI32], [kWasmEqRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewFixed, array, 1])
    .exportFunc();

    builder.addFunction('array_externalize',
                        makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewFixed, array, 1,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

    builder.addFunction('array_consumer',
                        makeSig([kWasmEqRef], [kWasmI32, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprRefIsNull,
      kExprBlock, kWasmVoid,
        kExprLocalGet, 0,
        kExprBrOnNull, 0,
        kGCPrefix, kExprRefCast, array,
        kExprI32Const, 0,
        kGCPrefix, kExprArrayGet, array, // value
        kExprI32Const, 0, // isNull
        kExprReturn,
      kExprEnd,
      kExprDrop,
      kExprI32Const, 0, // value (placeholder)
      kExprI32Const, 1, // isNull
    ])
    .exportFunc();

    builder.addFunction('array_internalize',
                        makeSig([kWasmExternRef], [kWasmI32, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprRefIsNull,
      kExprBlock, kWasmVoid,
        kExprLocalGet, 0,
        kGCPrefix, kExprAnyConvertExtern,
        kExprBrOnNull, 0,
        kGCPrefix, kExprRefCast, array,
        kExprI32Const, 0,
        kGCPrefix, kExprArrayGet, array, // value
        kExprI32Const, 0, // isNull
        kExprReturn,
      kExprEnd,
      kExprDrop,
      kExprI32Const, 0, // value (placeholder)
      kExprI32Const, 1, // isNull
    ])
    .exportFunc();

  return builder.instantiate({});
})();

for (let type of ["struct", "i31", "array"]) {
  for (let consume of ["consumer", "internalize"]) {
    let fnConsume = instance.exports[`${type}_${consume}`];
    // A null is converted to (ref null none).
    assertEquals([0, 1], fnConsume(null));
    if (consume == "internalize") {
      // Passing a JavaScript object is fine on internalize but fails on
      // casting it to dataref/arrayref/i31ref.
      var errorType = WebAssembly.RuntimeError;
      var errorMsg = "illegal cast";
    } else {
      // Passing a JavaScript object fails as it is not convertible to eqref.
      var errorType = TypeError;
      var errorMsg = "type incompatibility when transforming from/to JS";
    }
    assertThrows(() => fnConsume({}), errorType, errorMsg);

    for (let produce of ["producer", "externalize"]) {
      let fnProduce = instance.exports[`${type}_${produce}`];
      // Test roundtrip of a value produced in Wasm passed back to Wasm.
      let obj42 = fnProduce(42);
      assertEquals([42, 0], fnConsume(obj42));
    }
  }
}

// Differently to structs and arrays, the i31 value is directly accessible in
// JavaScript. Similarly, a JS smi can be internalized as an i31ref.
let createHeapNumber = (x) => x + x;
assertEquals(12345, instance.exports.i31_externalize(12345));
assertEquals([12345, 0], instance.exports.i31_internalize(12345));
assertEquals([11, 0], instance.exports.i31_internalize(createHeapNumber(5.5)));
