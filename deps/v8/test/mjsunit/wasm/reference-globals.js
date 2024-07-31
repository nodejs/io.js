// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestReferenceGlobals() {
  print(arguments.callee.name);

  var exporting_instance = (function() {
    var builder = new WasmModuleBuilder();

    builder.startRecGroup();
    var sig_index = builder.addType(kSig_i_ii);
    builder.endRecGroup();
    builder.startRecGroup();
    var wrong_sig_index = builder.addType(kSig_i_i);
    builder.endRecGroup();

    var addition_index = builder.addFunction("addition", sig_index)
      .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
      .exportFunc();

    builder.addGlobal(wasmRefType(sig_index), false, false,
                      [kExprRefFunc, addition_index.index])
           .exportAs("global");
    builder.addGlobal(wasmRefNullType(wrong_sig_index), false, false)
      .exportAs("mistyped_global");

    return builder.instantiate({});
  })();

  // Mistyped imported global.
  assertThrows(
    () => {
      var builder = new WasmModuleBuilder();
      var sig_index = builder.addType(kSig_i_ii);
      builder.addImportedGlobal("imports", "global", wasmRefNullType(sig_index),
                                false);
      builder.instantiate(
        {imports: { global: exporting_instance.exports.mistyped_global }})},
    WebAssembly.LinkError,
    /imported global does not match the expected type/
  );

  // Mistyped imported global due to cross-module typechecking.
  assertThrows(
    () => {
      var builder = new WasmModuleBuilder();
      var sig_index = builder.addType(kSig_i_i);
      builder.addImportedGlobal("imports", "global", wasmRefNullType(sig_index),
                                false);
      builder.instantiate(
        {imports: { global: exporting_instance.exports.global }})},
    WebAssembly.LinkError,
    /imported global does not match the expected type/
  );

  // Non-function imported into function-typed global.
  assertThrows(
    () => {
      var builder = new WasmModuleBuilder();
      var sig_index = builder.addType(kSig_i_ii);
      builder.addImportedGlobal("imports", "global", wasmRefNullType(sig_index),
                                false);
      builder.instantiate({imports: { global: 42 }})},
    WebAssembly.LinkError,
    /JS object does not match expected wasm type/
  );

  // Mistyped function import.
  assertThrows(
    () => {
      var builder = new WasmModuleBuilder();
      var sig_index = builder.addType(kSig_i_i);
      builder.addImportedGlobal("imports", "global", wasmRefType(sig_index),
                                false);
      builder.instantiate(
        {imports: { global: exporting_instance.exports.addition }})},
    WebAssembly.LinkError,
    /assigned exported function has to be a subtype of the expected type/
  );

  var instance = (function () {
    var builder = new WasmModuleBuilder();

    var sig_index = builder.addType(kSig_i_ii);

    builder.addImportedGlobal("imports", "global", wasmRefNullType(sig_index),
                              false);

    builder.addFunction("test_import", kSig_i_ii)
      .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprGlobalGet, 0,
                kExprCallRef, sig_index])
      .exportFunc();

    return builder.instantiate({imports: {
      global: exporting_instance.exports.global
    }});
  })();

  // This module is valid.
  assertFalse(instance === undefined);
  assertFalse(instance === null);
  assertFalse(instance === 0);

  // The correct function reference has been passed.
  assertEquals(66, instance.exports.test_import(42, 24));
})();

(function TestStructInitExpr() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  builder.startRecGroup();
  var struct_index = builder.addStruct([{type: kWasmI32, mutability: true}]);
  builder.endRecGroup();
  var composite_struct_index = builder.addStruct(
      [{type: kWasmI32, mutability: true},
       {type: wasmRefNullType(struct_index), mutability: true},
       {type: kWasmI8, mutability: true}]);

  let field1_value = 432;
  let field2_value = -123;
  let field3_value = -555;

  var global0 = builder.addGlobal(
      wasmRefType(struct_index), false, false,
      [...wasmI32Const(field2_value),
       kGCPrefix, kExprStructNew, struct_index]);

  var global = builder.addGlobal(
      wasmRefType(composite_struct_index), false, false,
      [...wasmI32Const(field1_value), kExprGlobalGet, global0.index,
       ...wasmI32Const(field3_value),
       kGCPrefix, kExprStructNew, composite_struct_index]);

  var global_default = builder.addGlobal(
    wasmRefType(composite_struct_index), false, false,
    [kGCPrefix, kExprStructNewDefault, composite_struct_index]);

  builder.addFunction("field_1", kSig_i_v)
    .addBody([
      kExprGlobalGet, global.index,
      kGCPrefix, kExprStructGet, composite_struct_index, 0
    ])
    .exportFunc();

  builder.addFunction("field_2", kSig_i_v)
    .addBody([
      kExprGlobalGet, global.index,
      kGCPrefix, kExprStructGet, composite_struct_index, 1,
      kGCPrefix, kExprStructGet, struct_index, 0
    ])
    .exportFunc();

  builder.addFunction("field_3", kSig_i_v)
    .addBody([
      kExprGlobalGet, global.index,
      kGCPrefix, kExprStructGetS, composite_struct_index, 2])
    .exportFunc();

  builder.addFunction("field_1_default", kSig_i_v)
    .addBody([
      kExprGlobalGet, global_default.index,
      kGCPrefix, kExprStructGet, composite_struct_index, 0])
    .exportFunc();

  builder.addFunction("field_2_default", makeSig([], [kWasmStructRef]))
    .addBody([
      kExprGlobalGet, global_default.index,
      kGCPrefix, kExprStructGet, composite_struct_index, 1])
    .exportFunc();

  builder.addFunction("field_3_default", kSig_i_v)
    .addBody([
      kExprGlobalGet, global_default.index,
      kGCPrefix, kExprStructGetS, composite_struct_index, 2])
    .exportFunc();

  var instance = builder.instantiate({});

  assertEquals(field1_value, instance.exports.field_1());
  assertEquals(field2_value, instance.exports.field_2());
  assertEquals((field3_value << 24) >> 24, instance.exports.field_3());
  assertEquals(0, instance.exports.field_1_default());
  assertEquals(null, instance.exports.field_2_default());
  assertEquals(0, instance.exports.field_3_default());
})();

(function TestArrayNewFixedExprNumeric() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  var array_index = builder.addArray(kWasmI16, true);

  let element0_value = -44;
  let element1_value = 55;

  var global0 = builder.addGlobal(
      kWasmI32, false, false,
      wasmI32Const(element0_value));

  var global = builder.addGlobal(
      wasmRefType(array_index), false, false,
      [kExprGlobalGet, global0.index, ...wasmI32Const(element1_value),
       kGCPrefix, kExprArrayNewFixed, array_index, 2]);

  builder.addFunction("get_element", kSig_i_i)
    .addBody([
      kExprGlobalGet, global.index,
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayGetS, array_index])
    .exportFunc();

  var instance = builder.instantiate({});

  assertEquals(element0_value, instance.exports.get_element(0));
  assertEquals(element1_value, instance.exports.get_element(1));
})();

(function TestArrayNewFixedExprRef() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  var struct_index = builder.addStruct([{type: kWasmI32, mutability: false}]);
  var array_index = builder.addArray(wasmRefNullType(struct_index), true);

  let element0_value = 44;
  let element2_value = 55;

  var global0 = builder.addGlobal(
      wasmRefType(struct_index), false, false,
      [...wasmI32Const(element0_value),
       kGCPrefix, kExprStructNew, struct_index]);

  var global = builder.addGlobal(
      wasmRefType(array_index), false, false,
      [kExprGlobalGet, global0.index, kExprRefNull, struct_index,
       ...wasmI32Const(element2_value),
       kGCPrefix, kExprStructNew, struct_index,
       kGCPrefix, kExprArrayNewFixed, array_index, 3]);

  builder.addFunction("element0", kSig_i_v)
    .addBody([
      kExprGlobalGet, global.index,
      kExprI32Const, 0,
      kGCPrefix, kExprArrayGet, array_index,
      kGCPrefix, kExprStructGet, struct_index, 0])
    .exportFunc();

  builder.addFunction("element1", makeSig([], [kWasmStructRef]))
    .addBody([
      kExprGlobalGet, global.index,
      kExprI32Const, 1,
      kGCPrefix, kExprArrayGet, array_index])
    .exportFunc();

  builder.addFunction("element2", kSig_i_v)
    .addBody([
      kExprGlobalGet, global.index,
      kExprI32Const, 2,
      kGCPrefix, kExprArrayGet, array_index,
      kGCPrefix, kExprStructGet, struct_index, 0])
    .exportFunc();

  var instance = builder.instantiate({});

  assertEquals(element0_value, instance.exports.element0());
  assertEquals(null, instance.exports.element1());
  assertEquals(element2_value, instance.exports.element2());
})();

(function TestArrayNew() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  var struct_index = builder.addStruct([makeField(kWasmI64, true)]);
  var array_num_index = builder.addArray(kWasmI64, true);
  var array_ref_index = builder.addArray(wasmRefNullType(struct_index), true);

  let elem1 = -44;
  let elem2 = 15;
  let length = 20;

  let global_elem_1 =
    builder.addGlobal(kWasmI64, false, false, wasmI64Const(elem1));
  let global_elem_2 =
    builder.addGlobal(kWasmI64, false, false, wasmI64Const(elem2));
  let global_length =
    builder.addGlobal(kWasmI32, false, false, wasmI32Const(length));

  var global_array_1 = builder.addGlobal(
      wasmRefType(array_num_index), false, false,
      [kExprGlobalGet, global_elem_1.index,
       kExprGlobalGet, global_length.index,
       kGCPrefix, kExprArrayNew, array_num_index]);

  var global_array_2 = builder.addGlobal(
        wasmRefType(array_ref_index), false, false,
        [kExprGlobalGet, global_elem_2.index,
         kGCPrefix, kExprStructNew, struct_index,
         kExprGlobalGet, global_length.index,
         kGCPrefix, kExprArrayNew, array_ref_index]);

  builder.addFunction("get_elements", kSig_l_i)
    .addBody([
      kExprGlobalGet, global_array_1.index,
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayGet, array_num_index,
      kExprGlobalGet, global_array_2.index,
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayGet, array_ref_index,
      kGCPrefix, kExprStructGet, struct_index, 0,
      kExprI64Add])
    .exportFunc();

  var instance = builder.instantiate({});

  let result = BigInt(elem1 + elem2);

  assertEquals(result, instance.exports.get_elements(0));
  assertEquals(result, instance.exports.get_elements(length / 2));
  assertEquals(result, instance.exports.get_elements(length - 1));
  assertTraps(kTrapArrayOutOfBounds,
              () => instance.exports.get_elements(length));
})();

(function TestArrayNewArrayTooLarge() {
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();
  var array_num_index = builder.addArray(kWasmI64, true);

  builder.addGlobal(
      wasmRefType(array_num_index), false, false,
      [...wasmI32Const(0x8ffffff),
       kGCPrefix, kExprArrayNewDefault, array_num_index]);

  assertTraps(kTrapArrayTooLarge, () => builder.instantiate({}));
})();

(function TestI31RefConstantExpr() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  let array_index = builder.addArray(kWasmI31Ref, false);

  let values = [0, 10, -22, 0x7fffffff, -1];

  let global = builder.addGlobal(
      wasmRefType(array_index), true, false,
      [...values.flatMap(
        value => [...wasmI32Const(value), kGCPrefix, kExprRefI31]),
       kGCPrefix, kExprArrayNewFixed, array_index, 5]);

  for (signed of [true, false]) {
    builder.addFunction(`get_${signed ? "s" : "u"}`, kSig_i_i)
        .addBody([kExprGlobalGet, global.index,
                  kExprLocalGet, 0, kGCPrefix, kExprArrayGet, array_index,
                  kGCPrefix, signed ? kExprI31GetS : kExprI31GetU])
        .exportFunc();
  }

  let instance = builder.instantiate();

  assertEquals(values[0], instance.exports.get_s(0));
  assertEquals(values[1], instance.exports.get_s(1));
  assertEquals(values[2], instance.exports.get_s(2));
  assertEquals(values[3] | 0x80000000, instance.exports.get_s(3));
  assertEquals(values[4], instance.exports.get_s(4));

  assertEquals(values[0], instance.exports.get_u(0));
  assertEquals(values[1], instance.exports.get_u(1));
  assertEquals(values[2] & 0x7fffffff, instance.exports.get_u(2));
  assertEquals(values[3], instance.exports.get_u(3));
  assertEquals(values[4] & 0x7fffffff, instance.exports.get_u(4));
})();

(function TestI31RefConstantExprTypeError() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addGlobal(kWasmI31Ref, false, false,
                    [...wasmI64Const(0), kGCPrefix, kExprRefI31]);

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /ref.i31\[0\] expected type i32, found i64.const of type i64/);
})();

(function TestConstantExprFuncIndexOutOfBounds() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct_index = builder.addStruct([makeField(kWasmFuncRef, true)]);
  let func = builder.addFunction("element", kSig_i_i)
    .addBody([kExprLocalGet, 0])
    .exportFunc()

  builder.addGlobal(wasmRefType(struct_index), false, false,
                    [kExprRefFunc, func.index + 1, kExprStructNew,
                     struct_index]);

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /function index #1 is out of bounds/);
})();

(function TestExternConstantExpr() {
  print(arguments.callee.name);

  let imported_struct = (function () {
    let builder = new WasmModuleBuilder();

    let struct = builder.addStruct([makeField(kWasmI32, true)]);

    let global = builder.addGlobal(
        wasmRefType(struct), false, false,
        [kExprI32Const, 42, kGCPrefix, kExprStructNew, struct])
      .exportAs("global");

    return builder.instantiate().exports.global.value;
  })();

  let builder = new WasmModuleBuilder();

  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  let imported = builder.addImportedGlobal("m", "ext", kWasmExternRef, false)

  let internal = builder.addGlobal(
    kWasmAnyRef, false, false,
    [kExprGlobalGet, imported, kGCPrefix, kExprAnyConvertExtern]);

  builder.addGlobal(
      kWasmExternRef, false, false,
      [kExprGlobalGet, internal.index, kGCPrefix, kExprExternConvertAny])
    .exportAs("exported")

  builder.addFunction("getter", kSig_i_v)
    .addBody([kExprGlobalGet, internal.index,
              kGCPrefix, kExprRefCast, struct,
              kGCPrefix, kExprStructGet, struct, 0])
    .exportFunc();

  builder.addFunction("getter_fail", kSig_i_v)
    .addBody([kExprGlobalGet, internal.index,
              kGCPrefix, kExprRefCast, kI31RefCode,
              kGCPrefix, kExprI31GetS])
    .exportFunc();

  let instance = builder.instantiate({m: {ext: imported_struct}});

  assertSame(instance.exports.exported.value, imported_struct);
  assertEquals(42, instance.exports.getter());
  assertTraps(kTrapIllegalCast, () => instance.exports.getter_fail());
})();
