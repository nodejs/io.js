// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Note: The wasm module builder is not available for performance tests.
 * To change the wasm code, switch the use_module_builder flag to true, update
 * the code and run it using d8. It will print the bytes that then have to be
 * updated for the !use_module_builder path.
 */
let use_module_builder = false;
if (use_module_builder) {
  d8.file.execute('../../mjsunit/wasm/wasm-module-builder.js');
}

/**
 * Test performance of iterating over an array of integers adding up their
 * values.
 * The different suites measure the following:
 * WasmLoop:   The loop is implemented in wasm iterating over a wasm array.
 * JSLoop:     The loop is implemented in JS iterating over a wasm array
 *             stored in a wasm struct and calling webassembly helpers for the
 *             element access. This tests inlining performance of wasm into JS.
 * PureJSLoop: The loop is implemented in JS iterating over a JS array.
 */
(function() {
  // Compile and instantiate wasm.
  let instance;

  if (use_module_builder) {
    let builder = new WasmModuleBuilder();
    let backingStore = builder.addArray(kWasmI32, true);
    let arrayStruct = builder.addStruct([
      makeField(kWasmI32 /*length*/, true),
      makeField(wasmRefType(backingStore), true)
    ]);

    builder.addFunction('createArray',
        makeSig([kWasmI32 /*length*/], [kWasmExternRef]))
      .addLocals(kWasmI32, 1)  // i
      .addLocals(wasmRefType(backingStore), 1) // backingStore
      .addBody([
        // i = length;
        kExprLocalGet, 0, // length
        kExprLocalTee, 1,
        // backingStore = new backingStore[length];
        kGCPrefix, kExprArrayNewDefault, backingStore,
        kExprLocalSet, 2,
        // while (true)
        kExprLoop, kWasmVoid,
          // backingStore[--i] = i;
          kExprLocalGet, 2, // backingStore
          kExprLocalGet, 1, // i
          kExprI32Const, 1,
          kExprI32Sub,
          kExprLocalTee, 1,
          kExprLocalGet, 1, // i
          kGCPrefix, kExprArraySet, backingStore,
          // if (i != 0) continue;
          kExprLocalGet, 1,
          kExprI32Const, 0,
          kExprI32Ne,
          kExprBrIf, 0,
          // break;
        kExprEnd,
        // return new arrayStruct(length, backingStore);
        kExprLocalGet, 0, // length
        kExprLocalGet, 2, // backingStore
        kGCPrefix, kExprStructNew, arrayStruct,
        kGCPrefix, kExprExternConvertAny,
      ])
      .exportFunc();

    builder.addFunction('getLength',
        makeSig([kWasmExternRef], [kWasmI32]))
      .addBody([
        kExprLocalGet, 0,
        kGCPrefix, kExprAnyConvertExtern,
        kGCPrefix, kExprRefCast, arrayStruct,
        kGCPrefix, kExprStructGet, arrayStruct, 0,
      ])
      .exportFunc();

    builder.addFunction('get', makeSig([kWasmExternRef, kWasmI32], [kWasmI32]))
      .addBody([
        kExprLocalGet, 0,
        kGCPrefix, kExprAnyConvertExtern,
        kGCPrefix, kExprRefCast, arrayStruct,
        kGCPrefix, kExprStructGet, arrayStruct, 1,
        kExprLocalGet, 1,
        kGCPrefix, kExprArrayGet, backingStore,
      ])
      .exportFunc();

    builder.addFunction('wasmSumArray', makeSig([kWasmExternRef], [kWasmI32]))
      .addLocals(kWasmI32, 2)  // index, result
      .addBody([
        // index = cast<arrayStruct>(internalize(local.get 0)).length;
        kExprLocalGet, 0,
        kGCPrefix, kExprAnyConvertExtern,
        kGCPrefix, kExprRefCast, arrayStruct,
        kGCPrefix, kExprStructGet, arrayStruct, 0,
        kExprLocalTee, 1,
        // if (index == 0) return 0;
        kExprI32Eqz,
        kExprIf, kWasmVoid,
          kExprI32Const, 0,
          kExprReturn,
        kExprEnd,

        // while (true)
        kExprLoop, kWasmVoid,
          // result =
          //   cast<arrayStruct>(internalize(local.get 0))
          //     .backingStore[--index]
          //   + result;
          kExprLocalGet, 0,
          kGCPrefix, kExprAnyConvertExtern,
          kGCPrefix, kExprRefCast, arrayStruct,
          kGCPrefix, kExprStructGet, arrayStruct, 1,

          kExprLocalGet, 1,
          kExprI32Const, 1,
          kExprI32Sub,
          kExprLocalTee, 1,

          kGCPrefix, kExprArrayGet, backingStore,
          kExprLocalGet, 2,  // result
          kExprI32Add,
          kExprLocalSet, 2,
          // if (index != 0) continue;
          kExprLocalGet, 1,
          kExprI32Const, 0,
          kExprI32Ne,
          kExprBrIf, 0,
        kExprEnd,
        // return result;
        kExprLocalGet, 2,
      ])
      .exportFunc();

    print(builder.toBuffer());
    instance = builder.instantiate({});
  } else {
    instance = new WebAssembly.Instance(new WebAssembly.Module(new Uint8Array([
      0, 97, 115, 109, 1, 0, 0, 0, 1, 36, 6, 80, 0, 94, 127, 1, 80, 0, 95, 2,
      127, 1, 100, 0, 1, 96, 1, 127, 1, 111, 96, 1, 111, 1, 127, 96, 2, 111,
      127, 1, 127, 96, 1, 111, 1, 127, 3, 5, 4, 2, 3, 4, 5, 7, 48, 4, 11, 99,
      114, 101, 97, 116, 101, 65, 114, 114, 97, 121, 0, 0, 9, 103, 101, 116, 76,
      101, 110, 103, 116, 104, 0, 1, 3, 103, 101, 116, 0, 2, 12, 119, 97, 115,
      109, 83, 117, 109, 65, 114, 114, 97, 121, 0, 3, 10, 147, 1, 4, 49, 2, 1,
      127, 1, 100, 0, 32, 0, 34, 1, 251, 7, 0, 33, 2, 3, 64, 32, 2, 32, 1, 65,
      1, 107, 34, 1, 32, 1, 251, 14, 0, 32, 1, 65, 0, 71, 13, 0, 11, 32, 0, 32,
      2, 251, 0, 1, 251, 27, 11, 13, 0, 32, 0, 251, 26, 251, 22, 1, 251, 2, 1,
      0, 11, 18, 0, 32, 0, 251, 26, 251, 22, 1, 251, 2, 1, 1, 32, 1, 251, 11, 0,
      11, 62, 1, 2, 127, 32, 0, 251, 26, 251, 22, 1, 251, 2, 1, 0, 34, 1, 69, 4,
      64, 65, 0, 15, 11, 3, 64, 32, 0, 251, 26, 251, 22, 1, 251, 2, 1, 1, 32, 1,
      65, 1, 107, 34, 1, 251, 11, 0, 32, 2, 106, 33, 2, 32, 1, 65, 0, 71, 13, 0,
      11, 32, 2, 11, 0, 51, 4, 110, 97, 109, 101, 1, 44, 4, 0, 11, 99, 114, 101,
      97, 116, 101, 65, 114, 114, 97, 121, 1, 9, 103, 101, 116, 76, 101, 110,
      103, 116, 104, 2, 3, 103, 101, 116, 3, 12, 119, 97, 115, 109, 83, 117,
      109, 65, 114, 114, 97, 121
    ])), {});
  }

  let wasm = instance.exports;

  let arrayLength = 10_000;
  let myArrayStruct = wasm.createArray(arrayLength);
  let jsArray = Array.from(Array(arrayLength).keys());

  // expected = 0 + 1 + 2 + ... + (arrayLength - 1)
  let expected = (arrayLength - 1) * arrayLength / 2;

  let benchmarks = [
    function WasmLoop() {
      assertEquals(expected, wasm.wasmSumArray(myArrayStruct));
    },
    function JSLoop() {
      let get = wasm.get;
      let length = wasm.getLength(myArrayStruct);
      let result = 0;
      for (let i = 0; i < length; ++i) {
        result += get(myArrayStruct, i);
      }
      assertEquals(expected, result);
    },
    function PureJSLoop() {
      let length = jsArray.length;
      let result = 0;
      for (let i = 0; i < length; ++i) {
        result += jsArray[i];
      }
      assertEquals(expected, result);
    }
  ];

  for (let fct of benchmarks) {
    createSuite(fct.name, 100, fct);
  }
})();
