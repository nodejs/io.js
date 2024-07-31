// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");
d8.file.execute("test/mjsunit/wasm/exceptions-utils.js");

// Test throwing/catching the reference type value.
(function TestThrowCatchRefParam() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let sig = builder.addType(kSig_i_i);
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction("doubler", sig)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 0, kExprI32Add])
    .exportFunc();

  builder.addFunction("struct_producer",
                      makeSig([kWasmI32], [wasmRefType(struct)]))
    .addBody([kExprLocalGet, 0, kGCPrefix, kExprStructNew, struct])
    .exportFunc();

  for (let [name, code] of
      [["extern", kWasmExternRef],
       ["sig", sig],
       ["struct", struct]]) {
    let type = wasmRefNullType(code);
    let except = builder.addTag(makeSig([type], []));
    builder.addFunction("throw_catch_param_" + name, makeSig([type],[type]))
      .addBody([
        kExprTry, kWasmRefNull, code & kLeb128Mask,
          kExprLocalGet, 0,
          kExprThrow, except,
        kExprCatch, except,
          // fall-through
        kExprEnd,
      ]).exportFunc();
  }

  let instance = builder.instantiate();
  let wasm = instance.exports;

  let o = new Object();
  assertEquals(o, wasm.throw_catch_param_extern(o));
  assertEquals(1, wasm.throw_catch_param_extern(1));
  assertEquals(2.3, wasm.throw_catch_param_extern(2.3));
  assertEquals("str", wasm.throw_catch_param_extern("str"));

  let struct_obj = wasm.struct_producer(42);

  assertEquals(struct_obj, wasm.throw_catch_param_struct(struct_obj));

  let doubler_obj = wasm.doubler;
  assertEquals(doubler_obj, wasm.throw_catch_param_sig(doubler_obj));
  assertEquals(20, wasm.throw_catch_param_sig(doubler_obj)(10));
})();

// Test the encoding of a thrown exception with a null-ref value.
(function TestThrowRefNull() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  let except = builder.addTag(makeSig([wasmRefNullType(struct)], []));
  builder.addFunction("throw_null", kSig_v_v)
    .addBody([kExprRefNull, kNullRefCode, kExprThrow, except])
    .exportFunc();
  let instance = builder.instantiate();

  assertWasmThrows(instance, except, [null],
                   () => instance.exports.throw_null());
})();

(function TestJSToWasm() {
  print(arguments.callee.name);

  let producer = (function() {
    let builder = new WasmModuleBuilder();

    let struct = builder.addStruct([makeField(kWasmI64, true)]);
    let array = builder.addArray(wasmRefNullType(struct), true);
    let sig = builder.addType(kSig_i_ii);

    let struct_builder =
      builder.addFunction("struct_builder",
                          makeSig([kWasmI64], [wasmRefType(struct)]))
        .addBody([kExprLocalGet, 0, kGCPrefix, kExprStructNew, struct])
        .exportFunc();
    let array_builder =
      builder.addFunction("array_builder",
                          makeSig([kWasmI32], [wasmRefType(array)]))
        .addBody([kExprLocalGet, 0, kGCPrefix, kExprArrayNewDefault, array])
        .exportFunc();
    let adder = builder.addFunction("adder", sig)
      .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
      .exportFunc();

    return builder.instantiate().exports;
  })();

  let struct_obj = producer.struct_builder(42n);
  let array_obj = producer.array_builder(10);
  let function_obj = producer.adder;
  let external_obj = {};

  let tag = new WebAssembly.Tag({
    parameters: ["externref", "anyfunc", "anyref", "eqref", "i31ref",
                 "structref", "arrayref"]});
  let values_mistyped = [external_obj, 123, function_obj, struct_obj, -33,
                         struct_obj, array_obj];
  assertThrows(() => new WebAssembly.Exception(tag, values_mistyped), TypeError,
               /.* must be null \(if nullable\) or a Wasm function object/);
  let values = [external_obj, function_obj, 123, struct_obj, -33,
                struct_obj, array_obj];
  let exn = new WebAssembly.Exception(tag, values);
  // Make sure we roundtrip corretly through wasm using
  // the Exception constructor/getArg.
  for (i = 0 ; i < values.length; i++) {
    assertEquals(exn.getArg(tag, i), values[i]);
  }

  let t = () => { throw exn; }

  // Make sure objects in a JS exception object are correctly encoded as wasm
  // objects.
  let consumer = (function () {
    let builder = new WasmModuleBuilder();
    let struct = builder.addStruct([makeField(kWasmI64, true)]);
    builder.addArray(wasmRefNullType(struct), true);
    let sig = builder.addType(kSig_i_ii);
    let tag_sig = builder.addType(makeSig([
      kWasmExternRef, kWasmFuncRef, kWasmAnyRef, kWasmEqRef, kWasmI31Ref,
      kWasmStructRef, kWasmArrayRef], []));
    let throwing = builder.addImport("m", "t", kSig_i_v);
    let tag_index = builder.addImportedTag("m", "tag", tag_sig);
    builder.addFunction("catching", kSig_i_v)
      .addLocals(kWasmExternRef, 1)
      .addLocals(kWasmFuncRef, 1)
      .addLocals(kWasmAnyRef, 1)
      .addLocals(kWasmEqRef, 1)
      .addLocals(kWasmI31Ref, 1)
      .addLocals(kWasmStructRef, 1)
      .addLocals(kWasmArrayRef, 1)
      .addBody([
        kExprTry, kWasmI32,
          kExprCallFunction, throwing,
        kExprCatch, tag_index,
          kExprLocalSet, 6,
          kExprLocalSet, 5,
          kExprLocalSet, 4,
          kExprLocalSet, 3,
          kExprLocalSet, 2,
          kExprLocalSet, 1,
          kExprLocalSet, 0,

          // adder(10, 20)
          kExprI32Const, 10, kExprI32Const, 20,
          kExprLocalGet, 1, kGCPrefix, kExprRefCast, sig, kExprCallRef, sig,

          // 123
          kExprLocalGet, 2, kGCPrefix, kExprRefCast, kI31RefCode,
          kGCPrefix, kExprI31GetS,

          // (i32) struct(42l).get_0
          kExprLocalGet, 3, kGCPrefix, kExprRefCast, struct,
          kGCPrefix, kExprStructGet, struct, 0, kExprI32ConvertI64,

          // -33
          kExprLocalGet, 4, kGCPrefix, kExprI31GetS,

          // (i32) struct(42l).get_0
          kExprLocalGet, 5, kGCPrefix, kExprRefCast, struct,
          kGCPrefix, kExprStructGet, struct, 0, kExprI32ConvertI64,

          // array.length
          kExprLocalGet, 6, kGCPrefix, kExprArrayLen,

          // Now add them all up.
          kExprI32Add, kExprI32Add, kExprI32Add, kExprI32Add, kExprI32Add,
        kExprEnd])
      .exportFunc()

    return builder.instantiate({m: {t: t, tag: tag}}).exports
  })();

  assertEquals(30 + 123 + 42 + (-33) + 42 + 10, consumer.catching());
})();

(function TestWasmToJS() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();

  let sig = builder.addType(kSig_i_ii);

  let adder = builder.addFunction("adder", sig)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();

  let tag = builder.addTag(makeSig([kWasmFuncRef, kWasmFuncRef], []));
  builder.addExportOfKind("tag", kExternalTag, tag);

  builder.addFunction("thrower", kSig_v_v)
    .addBody([kExprRefNull, kFuncRefCode, kExprRefFunc, adder.index,
              kExprThrow, tag])
    .exportFunc();

  let wasm = builder.instantiate().exports;

  // Make sure objects thrown from Wasm and retrieved with getArg are correctly
  // decoded as JS objects.
  try { wasm.thrower() }
  catch (e) {
    assertEquals(null, e.getArg(wasm.tag, 0));
    assertEquals(99, e.getArg(wasm.tag, 1)(44, 55));
  }
})();
