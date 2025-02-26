// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-staging

// This file is for the most parts a direct port of
// test/mjsunit/wasm/exceptions.js using the new exception handling proposal.
// Tests that are independent of the version of the proposal are not included
// (e.g. tests that only use the `throw` instruction), and some exnref-specific
// tests are added.
// See also exnref-rethrow.js, exnref-global.js and exnref-api.js.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");
d8.file.execute("test/mjsunit/wasm/exceptions-utils.js");

// Test that "exnref" local variables are allowed.
(function TestLocalExnRef() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("push_and_drop_exnref", kSig_v_v)
      .addLocals(kWasmExnRef, 1)
      .addBody([
        kExprLocalGet, 0,
        kExprDrop,
      ]).exportFunc();
  let instance = builder.instantiate();

  assertDoesNotThrow(instance.exports.push_and_drop_exnref);
})();

(function TestCatchEmptyBlocks() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_v);
  builder.addFunction("catch_empty_try", kSig_v_v)
      .addBody([
        kExprTryTable, kWasmVoid, 1,
        kCatchNoRef, except, 0,
        kExprEnd,
      ]).exportFunc();
  builder.addFunction("catch_ref_empty_try", kSig_v_v)
      .addBody([
        kExprBlock, kExnRefCode,
          kExprTryTable, kWasmVoid, 1,
          kCatchRef, except, 0,
          kExprEnd,
          kExprReturn,
        kExprEnd,
        kExprDrop,
      ]).exportFunc();
  builder.addFunction("catch_all_empty_try", kSig_v_v)
      .addBody([
        kExprTryTable, kWasmVoid, 1,
        kCatchAllNoRef, 0,
        kExprEnd,
      ]).exportFunc();
  builder.addFunction("catch_all_ref_empty_try", kSig_v_v)
      .addBody([
        kExprBlock, kExnRefCode,
          kExprTryTable, kWasmVoid, 1,
          kCatchAllRef, 0,
          kExprEnd,
          kExprReturn,
        kExprEnd,
        kExprDrop,
      ]).exportFunc();
  let instance = builder.instantiate();

  assertDoesNotThrow(instance.exports.catch_empty_try);
  assertDoesNotThrow(instance.exports.catch_ref_empty_try);
  assertDoesNotThrow(instance.exports.catch_all_empty_try);
  assertDoesNotThrow(instance.exports.catch_all_ref_empty_try);
})();

(function TestCatchSimple() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_v);
  builder.addFunction("simple_throw_catch_to_0_1", kSig_i_i)
      .addBody([
        kExprBlock, kWasmVoid,
          kExprTryTable, kWasmI32, 1,
          kCatchNoRef, except, 0,
            kExprLocalGet, 0,
            kExprI32Eqz,
            kExprIf, kWasmVoid,
              kExprThrow, except,
            kExprEnd,
            kExprI32Const, 42,
          kExprEnd,
          kExprBr, 1,
        kExprEnd,
        kExprI32Const, 23
      ]).exportFunc();
  let instance = builder.instantiate();

  assertEquals(23, instance.exports.simple_throw_catch_to_0_1(0));
  assertEquals(42, instance.exports.simple_throw_catch_to_0_1(1));
})();

(function TestTrapNotCaught() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction('unreachable_in_try', kSig_v_v)
      .addBody([
        kExprTryTable, kWasmVoid, 1,
        kCatchAllNoRef, 0,
          kExprUnreachable,
        kExprEnd
      ]).exportFunc();
  let instance = builder.instantiate();

  assertTraps(kTrapUnreachable, () => instance.exports.unreachable_in_try());
})();

(function TestTrapInCalleeNotCaught() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let func_div = builder.addFunction('div', kSig_i_ii).addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprI32DivU
  ]);
  builder.addFunction('trap_in_callee', kSig_i_ii)
      .addBody([
        kExprBlock, kWasmVoid,
          kExprTryTable, kWasmI32, 1,
          kCatchAllNoRef, 0,
            kExprLocalGet, 0,
            kExprLocalGet, 1,
            kExprCallFunction, func_div.index,
          kExprEnd,
          kExprBr, 1,
        kExprEnd,
        kExprI32Const, 11,
      ]).exportFunc();
  let instance = builder.instantiate();

  assertEquals(3, instance.exports.trap_in_callee(7, 2));
  assertTraps(kTrapDivByZero, () => instance.exports.trap_in_callee(1, 0));
})();

(function TestTrapViaJSNotCaught() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let imp = builder.addImport('imp', 'ort', kSig_i_v);
  builder.addFunction('div', kSig_i_ii)
      .addBody([
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        kExprI32DivU
      ]).exportFunc();
  builder.addFunction('call_import', kSig_i_v)
      .addBody([
        kExprBlock, kWasmVoid,
          kExprTryTable, kWasmI32, 1,
          kCatchAllNoRef, 0,
            kExprCallFunction, imp,
          kExprEnd,
          kExprBr, 1,
        kExprEnd,
        kExprI32Const, 11,
      ]).exportFunc();
  let exception = undefined;
  let instance;
  function js_import() {
    try {
      instance.exports.div(1, 0);
    } catch (e) {
      exception = e;
    }
    throw exception;
  }
  instance = builder.instantiate({imp: {ort: js_import}});

  let caught = undefined;
  try {
    let res = instance.exports.call_import();
    assertUnreachable('call_import should trap, but returned with ' + res);
  } catch (e) {
    caught = e;
  }
  assertSame(exception, caught);
  assertInstanceof(exception, WebAssembly.RuntimeError);
  assertEquals(exception.message, kTrapMsgs[kTrapDivByZero]);
})();

(function TestManuallyThrownRuntimeErrorCaught() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let imp = builder.addImport('imp', 'ort', kSig_i_v);
  builder.addFunction('call_import', kSig_i_v)
      .addBody([
        kExprBlock, kWasmVoid,
          kExprTryTable, kWasmI32, 1,
          kCatchAllNoRef, 0,
            kExprCallFunction, imp,
          kExprEnd,
          kExprBr, 1,
        kExprEnd,
        kExprI32Const, 11,
      ]).exportFunc();
  function throw_exc() {
    throw new WebAssembly.RuntimeError('My user text');
  }
  let instance = builder.instantiate({imp: {ort: throw_exc}});

  assertEquals(11, instance.exports.call_import());
})();

(function TestExnWithWasmProtoNotCaught() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_v);
  let imp = builder.addImport('imp', 'ort', kSig_v_v);
  let throw_fn = builder.addFunction('throw', kSig_v_v)
                     .addBody([kExprThrow, except])
                     .exportFunc();
  builder.addFunction('test', kSig_v_v)
      .addBody([
        // Calling "throw" directly should produce the expected exception.
        kExprBlock, kWasmVoid,
          kExprTryTable, kWasmVoid, 1,
          kCatchNoRef, except, 0,
            kExprCallFunction, throw_fn.index,
          kExprEnd,
          kExprBr, 1,
        kExprEnd,
        // Calling through JS produces a wrapped exceptions which does not match
        // the catch.
        kExprBlock, kWasmVoid,
          kExprTryTable, kWasmVoid, 1,
          kCatchNoRef, except, 0,
            kExprCallFunction, imp,
          kExprEnd,
          kExprBr, 1,
        kExprEnd
      ]).exportFunc();
  let instance;
  let wrapped_exn;
  function js_import() {
    try {
      instance.exports.throw();
    } catch (e) {
      wrapped_exn = new Error();
      wrapped_exn.__proto__ = e;
      throw wrapped_exn;
    }
  }
  instance = builder.instantiate({imp: {ort: js_import}});
  let caught = undefined;
  try {
    instance.exports.test();
  } catch (e) {
    caught = e;
  }
  assertTrue(!!caught, 'should have trapped');
  assertEquals(caught, wrapped_exn);
  assertInstanceof(caught.__proto__, WebAssembly.Exception);
})();

(function TestStackOverflowNotCaught() {
  print(arguments.callee.name);
  function stack_overflow() {
    %ThrowStackOverflow();
  }
  let builder = new WasmModuleBuilder();
  let sig_v_v = builder.addType(kSig_v_v);
  let kStackOverflow = builder.addImport('', 'stack_overflow', sig_v_v);
  builder.addFunction('try_stack_overflow', kSig_v_v)
      .addBody([
        kExprTryTable, kWasmVoid, 1,
        kCatchAllNoRef, 0,
          kExprCallFunction, 0,
        kExprEnd
      ]).exportFunc();
  let instance = builder.instantiate({'': {'stack_overflow': stack_overflow}});

  assertThrows(() => instance.exports.try_stack_overflow(),
      RangeError, 'Maximum call stack size exceeded');
})();

// Test that we can distinguish which exception was thrown by using a cascaded
// sequence of nested try blocks with a single catch block each.
(function TestCatchComplex1() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except1 = builder.addTag(kSig_v_v);
  let except2 = builder.addTag(kSig_v_v);
  let except3 = builder.addTag(kSig_v_v);
  builder.addFunction("catch_complex", kSig_i_i)
      .addBody([
        kExprBlock, kWasmVoid,
          kExprTryTable, kWasmI32, 1,
          kCatchNoRef, except2, 0,
            kExprBlock, kWasmVoid,
              kExprTryTable, kWasmI32, 1,
              kCatchNoRef, except1, 0,
                kExprLocalGet, 0,
                kExprI32Eqz,
                kExprIf, kWasmVoid,
                  kExprThrow, except1,
                kExprElse,
                  kExprLocalGet, 0,
                  kExprI32Const, 1,
                  kExprI32Eq,
                  kExprIf, kWasmVoid,
                    kExprThrow, except2,
                  kExprElse,
                    kExprThrow, except3,
                  kExprEnd,
                kExprEnd,
                kExprI32Const, 2,
              kExprEnd,
              kExprBr, 1,
            kExprEnd,
            kExprI32Const, 3,
          kExprEnd,
          kExprBr, 1,
        kExprEnd,
        kExprI32Const, 4,
      ]).exportFunc();
  let instance = builder.instantiate();

  assertEquals(3, instance.exports.catch_complex(0));
  assertEquals(4, instance.exports.catch_complex(1));
  assertWasmThrows(instance, except3, [],
                   () => instance.exports.catch_complex(2));
})();

// Test that we can distinguish which exception was thrown by using a single
// try block with multiple associated catch blocks in sequence.
(function TestCatchComplex2() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except1 = builder.addTag(kSig_v_v);
  let except2 = builder.addTag(kSig_v_v);
  let except3 = builder.addTag(kSig_v_v);
  builder.addFunction("catch_complex", kSig_i_i)
      .addBody([
        kExprBlock, kWasmVoid,
          kExprBlock, kWasmVoid,
            kExprTryTable, kWasmI32, 2,
            kCatchNoRef, except1, 0,
            kCatchNoRef, except2, 1,
              kExprLocalGet, 0,
              kExprI32Eqz,
              kExprIf, kWasmVoid,
                kExprThrow, except1,
              kExprElse,
                kExprLocalGet, 0,
                kExprI32Const, 1,
                kExprI32Eq,
                kExprIf, kWasmVoid,
                  kExprThrow, except2,
                kExprElse,
                  kExprThrow, except3,
                kExprEnd,
              kExprEnd,
              kExprI32Const, 2,
            kExprEnd,
            kExprBr, 2,
          kExprEnd,
          kExprI32Const, 3,
          kExprBr, 1,
        kExprEnd,
        kExprI32Const, 4,
      ]).exportFunc();
  let instance = builder.instantiate();

  assertEquals(3, instance.exports.catch_complex(0));
  assertEquals(4, instance.exports.catch_complex(1));
  assertWasmThrows(instance, except3, [],
                   () => instance.exports.catch_complex(2));
})();

// Test throwing/catching the i32 parameter value.
(function TestThrowCatchParamI() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_i);
  builder.addFunction("throw_catch_param", kSig_i_i)
      .addBody([
        kExprBlock, kWasmI32,
          kExprTryTable, kWasmI32, 1,
          kCatchNoRef, except, 0,
            kExprLocalGet, 0,
            kExprThrow, except,
            kExprI32Const, 2,
          kExprEnd,
          kExprReturn,
        kExprEnd,
        kExprReturn,
      ]).exportFunc();
  let instance = builder.instantiate();

  assertEquals(0, instance.exports.throw_catch_param(0));
  assertEquals(1, instance.exports.throw_catch_param(1));
  assertEquals(10, instance.exports.throw_catch_param(10));
})();

// Test throwing/catching the f32 parameter value.
(function TestThrowCatchParamF() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_f);
  builder.addFunction("throw_catch_param", kSig_f_f)
      .addBody([
        kExprBlock, kWasmF32,
          kExprTryTable, kWasmF32, 1,
          kCatchNoRef, except, 0,
            kExprLocalGet, 0,
            kExprThrow, except,
            kExprF32Const, 0, 0, 0, 0,
          kExprEnd,
          kExprReturn,
        kExprEnd,
        kExprReturn,
      ]).exportFunc();
  let instance = builder.instantiate();

  assertEquals(5.0, instance.exports.throw_catch_param(5.0));
  assertEquals(10.5, instance.exports.throw_catch_param(10.5));
})();

(function TestThrowCatchParamL() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_l);
  builder.addFunction("throw_catch_param", kSig_i_i)
      .addLocals(kWasmI64, 1)
      .addBody([
        kExprLocalGet, 0,
        kExprI64UConvertI32,
        kExprLocalSet, 1,
        kExprBlock, kWasmI64,
          kExprTryTable, kWasmI32, 1,
          kCatchNoRef, except, 0,
            kExprLocalGet, 1,
            kExprThrow, except,
            kExprI32Const, 2,
          kExprEnd,
          kExprBr, 1,
        kExprEnd,
        kExprLocalGet, 1,
        kExprI64Eq,
        kExprIf, kWasmI32,
          kExprI32Const, 1,
        kExprElse,
          kExprI32Const, 0,
        kExprEnd,
      ]).exportFunc();
  let instance = builder.instantiate();

  assertEquals(1, instance.exports.throw_catch_param(5));
  assertEquals(1, instance.exports.throw_catch_param(0));
  assertEquals(1, instance.exports.throw_catch_param(-1));
})();

// Test throwing/catching the F64 parameter value
(function TestThrowCatchParamD() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_d);
  builder.addFunction("throw_catch_param", kSig_d_d)
      .addBody([
        kExprTryTable, kWasmF64, 1,
        kCatchNoRef, except, 0,
          kExprLocalGet, 0,
          kExprThrow, except,
          kExprF64Const, 0, 0, 0, 0, 0, 0, 0, 0,
          kExprReturn,
        kExprEnd,
      ]).exportFunc();
  let instance = builder.instantiate();

  assertEquals(5.0, instance.exports.throw_catch_param(5.0));
  assertEquals(10.5, instance.exports.throw_catch_param(10.5));
})();

(function TestThrowBeforeUnreachable() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_v);
  builder.addFunction('throw_before_unreachable', kSig_i_v)
      .addBody([
        kExprBlock, kWasmVoid,
          kExprTryTable, kWasmVoid, 1,
          kCatchAllNoRef, 0,
            kExprThrow, except,
            kExprUnreachable,
          kExprEnd,
          kExprI32Const, 0,
          kExprReturn,
        kExprEnd,
        kExprI32Const, 42,
      ]).exportFunc();

  let instance = builder.instantiate();
  assertEquals(42, instance.exports.throw_before_unreachable());
})();

(function TestUnreachableInCatchAll() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_v);
  builder.addFunction('throw_before_unreachable', kSig_i_v)
      .addBody([
        kExprBlock, kWasmVoid,
          kExprTryTable, kWasmVoid, 1,
          kCatchAllNoRef, 0,
            kExprThrow, except,
          kExprEnd,
          kExprI32Const, 0,
          kExprReturn,
        kExprEnd,
        kExprI32Const, 42,
        kExprUnreachable,
      ]).exportFunc();

  let instance = builder.instantiate();
})();

(function TestThrowWithLocal() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_v);
  builder.addFunction('throw_with_local', kSig_i_v)
    .addLocals(kWasmI32, 4)
    .addBody([
        kExprI32Const, 42,
        kExprF64Const, 0, 0, 0, 0, 0, 0, 0, 0,
        kExprBlock, kWasmF32,
          kExprBlock, kWasmVoid,
            kExprTryTable, kWasmF32, 1,
            kCatchAllNoRef, 0,
              kExprThrow, except,
            kExprEnd,
            kExprBr, 1,
          kExprEnd,
          kExprF32Const, 0, 0, 0, 0,
        kExprEnd,
        // Leave the '42' on the stack.
        kExprDrop,  // Drop the f32.
        kExprDrop,  // Drop the f64.
    ]).exportFunc();

  let instance = builder.instantiate();
  assertEquals(42, instance.exports.throw_with_local());
})();

(function TestCatchlessTry() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_v);
  builder.addFunction('catchless_try', kSig_v_i)
    .addBody([
        kExprTryTable, kWasmVoid, 0,
          kExprLocalGet, 0,
          kExprIf, kWasmVoid,
            kExprThrow, except,
          kExprEnd,
        kExprEnd,
    ]).exportFunc();

  let instance = builder.instantiate();
  assertDoesNotThrow(() => instance.exports.catchless_try(0));
  assertWasmThrows(instance, except, [],
                   () => instance.exports.catchless_try(1));
})();

// Test catch-ref + unpacking.
(function TestCatchRef() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_i);
  let sig = builder.addType(makeSig([], [kWasmI32, kWasmExnRef]));
  builder.addFunction("catch_ref_i32", kSig_i_v)
      .addBody([
        kExprBlock, sig,
          kExprTryTable, kWasmVoid, 1,
          kCatchRef, except, 0,
            kExprI32Const, 1,
            kExprThrow, except,
          kExprEnd,
          kExprI32Const, 2,
          kExprReturn,
        kExprEnd,
        kExprDrop,
  ]).exportFunc();
  let instance = builder.instantiate();

  assertEquals(1, instance.exports.catch_ref_i32());
})();

// Test catch-all-ref.
(function TestCatchAllRef() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_v);
  let sig = builder.addType(makeSig([], [kWasmExnRef]));
  let g = builder.addGlobal(kWasmExnRef, true, false);
  builder.addExportOfKind("g", kExternalGlobal, g.index);
  builder.addFunction("catch_all_ref", kSig_v_v)
      .addBody([
        kExprBlock, sig,
          kExprTryTable, kWasmVoid, 1,
          kCatchAllRef, 0,
            kExprThrow, except,
          kExprEnd,
          kExprReturn,
        kExprEnd,
        kExprThrowRef
  ]).exportFunc();
  let instance = builder.instantiate();

  assertThrows(instance.exports.catch_all_ref, WebAssembly.Exception);
})();

(function TestCatchRefTwoParams() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addTag(kSig_v_ii);
  let sig = builder.addType(makeSig([], [kWasmI32, kWasmI32, kWasmExnRef]));
  builder.addFunction("catch_ref_two_params", kSig_ii_v)
      .addBody([
        kExprBlock, sig,
          kExprTryTable, kWasmVoid, 1,
          kCatchRef, except, 0,
            kExprI32Const, 1, kExprI32Const, 2,
            kExprThrow, except,
          kExprEnd,
          kExprI32Const, 3, kExprI32Const, 4,
          kExprReturn,
        kExprEnd,
        kExprDrop,
  ]).exportFunc();
  let instance = builder.instantiate();

  assertEquals([1, 2], instance.exports.catch_ref_two_params());
})();

(function TestThrowNoExn() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("throw_noexn", kSig_v_v)
      .addBody([
          kExprRefNull, kNullExnRefCode,
          kExprThrowRef
  ]).exportFunc();
  let instance = builder.instantiate();

  assertTraps(kTrapRethrowNull, () => instance.exports.throw_noexn());
})();

(function TestJSNonNullableExnRef() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let import_sig = makeSig([], [wasmRefType(kWasmExnRef)]);
  let imported = builder.addImport('m', 'i', import_sig);
  builder.addFunction("call_import", kSig_v_v)
      .addBody([
          kExprCallFunction, imported,
          kExprDrop,
      ]).exportFunc();
  let export_sig = makeSig([wasmRefType(kWasmExnRef)], []);
  builder.addFunction("export", export_sig)
      .addBody([]).exportFunc();
  let instance = builder.instantiate({m: {i: () => {}}});
  assertThrows(instance.exports.call_import);
  assertThrows(instance.exports.export);
})();
