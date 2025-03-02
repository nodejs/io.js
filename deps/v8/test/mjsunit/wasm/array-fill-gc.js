// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestRepeatedArrayFillWithGC() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let struct = builder.addStruct([makeField(kWasmI64, true)]);
  let array = builder.addArray(wasmRefNullType(struct), true);

  let gcFunc = builder.addImport("m", "gc", kSig_v_v);

  // This needs to be large enough, so that we call into C++ instead of running
  // a loop in the generated code. (see kArrayNewMinimumSizeForMemSet)
  let arrayLength = 50;

  builder.addFunction("create", makeSig([kWasmI64], [wasmRefNullType(array)]))
  .addLocals(wasmRefNullType(array), 1)
  .addLocals(wasmRefNullType(struct), 1)
  .addBody([
    // Create the fill value (young generation).
    kExprLocalGet, 0,
    kGCPrefix, kExprStructNew, struct,
    kExprLocalTee, 2,
    // Create an array.
    // As the size is larger than kArrayNewMinimumSizeForMemSet (16), this will
    // call to C++ for initializing the elements. The fill value is passed as a
    // pointer. There were two issues with that:
    // 1) For a reference value on pointer-compressed builds, this emitted a
    //    __ ChangeInt32ToInt64() operation causing the value to become
    //    untagged.
    // 2) This untagged value was then stored in an untagged stack slot. So any
    //    GC would not visit this stack slot. Note that this bug doesn't seem
    //    possible to be triggered as the stack slot is not GVN'ed (so the
    //    second fill initializes a separate stack slot).
    kExprI32Const, arrayLength,
    kGCPrefix, kExprArrayNew, array,
    kExprDrop,
    // Trigger a gc.
    kExprCallFunction, gcFunc,
    // Create another array.
    // The initialization emitted the same __ ChangeInt32ToInt64() for the fill
    // value (the struct.new above). Due to GVN the compiler decides to reuse
    // the instruction from above. However, due to the GC in between this
    // "int64" now contains an outdated pointer pointing into the previous
    // half-space for the young generation. (Note that anyways we should pass
    // the decompressed pointer, zeroing out the upper half doesn't fit to our
    // calling convention, it just happens to not be an issue as we only use the
    // value to store it inside the tagged (compressed) slots inside the array.)
    kExprLocalGet, 2,
    kExprI32Const, arrayLength,
    kGCPrefix, kExprArrayNew, array,
  ]).exportFunc();

  builder.addFunction("get",
    makeSig([wasmRefNullType(array), kWasmI32], [kWasmI64]))
  .addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kGCPrefix, kExprArrayGet, array,
    kGCPrefix, kExprStructGet, struct, 0,
  ]).exportFunc();

  let wasm = builder.instantiate({m: {gc}}).exports;
  let arr = wasm.create(43n);
  for (let i = 0; i < arrayLength; ++i) {
    assertEquals(43n, wasm.get(arr, i));
  }
})();
