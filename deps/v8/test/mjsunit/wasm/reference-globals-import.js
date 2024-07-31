// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-stringref

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Test type checks when creating a global with a value imported from a global
// from another module.
(function TestReferenceGlobalsImportGlobal() {
  print(arguments.callee.name);

  let exporting_instance = (function() {
    let builder = new WasmModuleBuilder();
    let type_super = builder.addStruct([makeField(kWasmI32, false)]);
    let type_sub =
        builder.addStruct([makeField(kWasmI32, false)], type_super);
    let type_other = builder.addStruct([makeField(kWasmI64, false)]);

    builder.addGlobal(wasmRefType(type_super), false, false,
                      [kExprI32Const, 42,
                       kGCPrefix, kExprStructNew, type_super])
           .exportAs("super");
    builder.addGlobal(wasmRefType(type_sub), false, false,
                      [kExprI32Const, 42,
                       kGCPrefix, kExprStructNew, type_sub])
           .exportAs("sub");
    builder.addGlobal(wasmRefType(type_other), false, false,
            [kExprI64Const, 42,
             kGCPrefix, kExprStructNew, type_other])
           .exportAs("other");
    // null variants
    builder.addGlobal(wasmRefNullType(type_super), false, false,
                      [kExprI32Const, 42,
                       kGCPrefix, kExprStructNew, type_super])
           .exportAs("super_nullable");
    builder.addGlobal(wasmRefNullType(type_sub), false, false,
                      [kExprI32Const, 42,
                       kGCPrefix, kExprStructNew, type_sub])
           .exportAs("sub_nullable");
    builder.addGlobal(wasmRefNullType(type_other), false, false,
            [kExprI64Const, 42,
             kGCPrefix, kExprStructNew, type_other])
           .exportAs("other_nullable");
    return builder.instantiate({});
  })();

  let tests = [
    //valid |type             |imported_global
    [true,  "super",          "super"],
    [true,  "sub",            "sub"],
    [true,  "super",          "sub"], // would be invalid for immutable global!
    [false, "sub",            "super"],
    [false, "sub",            "other"],
    [false, "super",          "super_nullable"],
    [true,  "super_nullable", "super"],
    [true,  "super_nullable", "sub"],
    [true,  "super_nullable", "sub_nullable"],
    [false, "super_nullable", "other_nullable"],
    [false, "sub_nullable",   "super_nullable"],
  ];
  for (let[expected_valid, type, global] of tests) {
    print(`test ${type} imports ${global}`);
    let builder = new WasmModuleBuilder();
    let type_super = builder.addStruct([makeField(kWasmI32, false)]);
    let type_sub =
      builder.addStruct([makeField(kWasmI32, false)], type_super);

    let types = {
      super: wasmRefType(type_super),
      sub: wasmRefType(type_sub),
      super_nullable: wasmRefNullType(type_super),
      sub_nullable: wasmRefNullType(type_sub),
    };
    builder.addImportedGlobal("imports", "global", types[type], false);
    assertNotEquals(exporting_instance.exports[global], undefined);
    let imports = { global: exporting_instance.exports[global] };
    if (expected_valid) {
      builder.addFunction("read_global", makeSig([], [kWasmI32]))
      .addBody([
        kExprGlobalGet, 0,
        kGCPrefix, kExprStructGet, types[type].heap_type, 0,
      ])
      .exportFunc();

      let instance = builder.instantiate({imports});
      assertEquals(42, instance.exports.read_global());
    } else {
      assertThrows(
        () => builder.instantiate({imports}),
        WebAssembly.LinkError,
        /imported global does not match the expected type/
      );
    }
  }
})();

// Test type checks when creating a global initialized with wasm objects
// provided as externref.
(function TestReferenceGlobalsImportValue() {
  print(arguments.callee.name);

  let exporting_instance = (function() {
    let builder = new WasmModuleBuilder();
    let type_super = builder.addStruct([makeField(kWasmI32, false)]);
    let type_sub =
        builder.addStruct([makeField(kWasmI32, false)], type_super);
    let type_other = builder.addStruct([makeField(kWasmI64, false)]);

    builder.addFunction("create_super", makeSig([], [kWasmExternRef]))
    .addBody([
      kExprI32Const, 42,
      kGCPrefix, kExprStructNew, type_super,
      kGCPrefix, kExprExternConvertAny])
    .exportFunc();
    builder.addFunction("create_sub", makeSig([], [kWasmExternRef]))
    .addBody([
      kExprI32Const, 42,
      kGCPrefix, kExprStructNew, type_sub,
      kGCPrefix, kExprExternConvertAny])
    .exportFunc();
    builder.addFunction("create_other", makeSig([], [kWasmExternRef]))
    .addBody([
      kExprI64Const, 42,
      kGCPrefix, kExprStructNew, type_other,
      kGCPrefix, kExprExternConvertAny])
    .exportFunc();
    builder.addFunction("create_null", makeSig([], [kWasmExternRef]))
    .addBody([
      kExprRefNull, kNullRefCode,
      kGCPrefix, kExprExternConvertAny])
    .exportFunc();

    return builder.instantiate({});
  })();

  let tests = [
    //valid |type             |imported_value
    [true,  "super",          "super"],
    [true,  "sub",            "sub"],
    [true,  "super",          "sub"],
    [false, "sub",            "super"],
    [false, "sub",            "other"],
    [false, "super",          "null"],
    [true,  "super_nullable", "super"],
    [true,  "super_nullable", "sub"],
    [true,  "super_nullable", "sub"],
    [false, "super_nullable", "other"],
    [false, "sub_nullable",   "super"],
    [true,  "super_nullable", "null"],
  ];
  for (let[expected_valid, type, imported_value] of tests) {
    print(`test ${type} imports ${imported_value}`);
    let builder = new WasmModuleBuilder();
    let type_super = builder.addStruct([makeField(kWasmI32, false)]);
    let type_sub =
      builder.addStruct([makeField(kWasmI32, false)], type_super);
    let types = {
      super: wasmRefType(type_super),
      sub: wasmRefType(type_sub),
      super_nullable: wasmRefNullType(type_super),
      sub_nullable: wasmRefNullType(type_sub),
    };
    builder.addImportedGlobal("imports", "global", types[type], false);
    let init_value = exporting_instance.exports[`create_${imported_value}`]();
    let imports = {global: init_value};
    if (expected_valid) {
      builder.addFunction("read_global", makeSig([], [kWasmI32]))
      .addBody([
        kExprBlock, kWasmVoid,
          kExprGlobalGet, 0,
          kExprBrOnNull, 0,
          kGCPrefix, kExprStructGet, types[type].heap_type, 0,
          kExprReturn,
        kExprEnd,
        ...wasmI32Const(-1),
      ])
      .exportFunc();

      let instance = builder.instantiate({imports});
      assertEquals(imported_value == "null" ? -1 : 42,
                   instance.exports.read_global());
    } else {
      assertThrows(
        () => builder.instantiate({imports}),
        WebAssembly.LinkError
      );
    }
  }
})();

(function TestReferenceGlobalsImportInvalidJsValues() {
  print(arguments.callee.name);
  let invalid_values =
      [undefined, {}, [], 0, NaN, null, /regex/, true, false, ""];
  for (let value of invalid_values) {
    print(`test invalid value ${JSON.stringify(value)}`);
    let builder = new WasmModuleBuilder();
    let struct_type = builder.addStruct([makeField(kWasmI32, false)]);
    let ref_type = wasmRefType(struct_type);
    builder.addImportedGlobal("imports", "value", ref_type, false);
    assertThrows(
      () => builder.instantiate({imports: {value}}),
      WebAssembly.LinkError);
  }
})();

(function TestReferenceGlobalsAbstractTypes() {
  print(arguments.callee.name);
  let exporting_instance = (function() {
    let builder = new WasmModuleBuilder();
    let type_struct = builder.addStruct([makeField(kWasmI32, false)]);
    let type_array = builder.addArray(kWasmI32);

    builder.addFunction("create_struct", makeSig([], [kWasmExternRef]))
    .addBody([
      kExprI32Const, 42,
      kGCPrefix, kExprStructNew, type_struct,
      kGCPrefix, kExprExternConvertAny])
    .exportFunc();
    builder.addFunction("create_array", makeSig([], [kWasmExternRef]))
    .addBody([
      kExprI32Const, 42,
      kGCPrefix, kExprArrayNewFixed, type_array, 1,
      kGCPrefix, kExprExternConvertAny])
    .exportFunc();
    return builder.instantiate({});
  })();

  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("imports", "any1", kWasmAnyRef, false);
  builder.addImportedGlobal("imports", "any2", kWasmAnyRef, false);
  builder.addImportedGlobal("imports", "any3", kWasmAnyRef, false);
  builder.addImportedGlobal("imports", "any4", kWasmAnyRef, false);
  builder.addImportedGlobal("imports", "any4", kWasmAnyRef, false);
  builder.addImportedGlobal("imports", "any5", kWasmAnyRef, false);
  builder.addImportedGlobal("imports", "eq1", kWasmEqRef, false);
  builder.addImportedGlobal("imports", "eq2", kWasmEqRef, false);
  builder.addImportedGlobal("imports", "eq3", kWasmEqRef, false);
  builder.addImportedGlobal("imports", "array", kWasmArrayRef, false);
  builder.addImportedGlobal("imports", "i31ref", kWasmI31Ref, false);
  builder.instantiate({imports : {
    any1: exporting_instance.exports.create_struct(),
    any2: exporting_instance.exports.create_array(),
    any3: 12345, // i31
    any4: null,
    any5: "test string",
    eq1: 12345,
    eq2: exporting_instance.exports.create_array(),
    eq3: exporting_instance.exports.create_struct(),
    array: exporting_instance.exports.create_array(),
    i31ref: -123,
  }});
})();

(function TestReferenceGlobalsStrings() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("imports", "string1", kWasmStringRef, false);
  builder.addImportedGlobal("imports", "string2", kWasmStringRef, false);
  builder.addImportedGlobal("imports", "any", kWasmAnyRef, false);

  builder.addFunction("get_string1", makeSig([], [kWasmExternRef]))
  .addBody([kExprGlobalGet, 0, kGCPrefix, kExprExternConvertAny])
  .exportFunc();
  builder.addFunction("get_string2", makeSig([], [kWasmExternRef]))
  .addBody([kExprGlobalGet, 1, kGCPrefix, kExprExternConvertAny])
  .exportFunc();
  builder.addFunction("get_any", makeSig([], [kWasmExternRef]))
  .addBody([kExprGlobalGet, 2, kGCPrefix, kExprExternConvertAny])
  .exportFunc();

  let instance = builder.instantiate({imports : {
    string1: "Content of string1",
    string2: null,
    any: "Content of any",
  }});

  assertEquals("Content of string1", instance.exports.get_string1());
  assertEquals(null, instance.exports.get_string2());
  assertEquals("Content of any", instance.exports.get_any());
})();

(function TestAnyRefGlobalFromJS() {
  print(arguments.callee.name);
  let anyref_global = new WebAssembly.Global(
      { value: "anyref", mutable: true }, "initial value");
  assertEquals("initial value", anyref_global.value);

  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("imports", "anyref_global", kWasmAnyRef, true);
  let struct_type = builder.addStruct([makeField(kWasmI32, false)]);
  let array_type = builder.addArray(kWasmI32);

  builder.addFunction("get_extern", makeSig([], [kWasmExternRef]))
  .addBody([kExprGlobalGet, 0, kGCPrefix, kExprExternConvertAny])
  .exportFunc();
  builder.addFunction("get_struct_val", makeSig([], [kWasmI32]))
  .addBody([
    kExprGlobalGet, 0,
    kGCPrefix, kExprRefCast, struct_type,
    kGCPrefix, kExprStructGet, struct_type, 0,
  ])
  .exportFunc();
  builder.addFunction("get_array_val", makeSig([], [kWasmI32]))
  .addBody([
    kExprGlobalGet, 0,
    kGCPrefix, kExprRefCast, array_type,
    kExprI32Const, 0,
    kGCPrefix, kExprArrayGet, array_type,
  ])
  .exportFunc();
  builder.addFunction("create_struct", makeSig([kWasmI32], [kWasmExternRef]))
  .addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprStructNew, struct_type,
    kGCPrefix, kExprExternConvertAny])
  .exportFunc();
  builder.addFunction("create_array", makeSig([kWasmI32], [kWasmExternRef]))
  .addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprArrayNewFixed, array_type, 1,
    kGCPrefix, kExprExternConvertAny])
  .exportFunc();

  let instance = builder.instantiate({imports : {anyref_global}});
  let wasm = instance.exports;

  anyref_global.value = "Set anyref from string";
  assertEquals("Set anyref from string", anyref_global.value);
  assertEquals("Set anyref from string", wasm.get_extern());
  anyref_global.value = wasm.create_struct(42);
  assertEquals(42, wasm.get_struct_val());
  anyref_global.value = wasm.create_array(43);
  assertEquals(43, wasm.get_array_val());
  anyref_global.value = null;
  assertEquals(null, anyref_global.value);
  assertEquals(null, wasm.get_extern());
  anyref_global.value = 12345;
  assertEquals(12345, wasm.get_extern());

  let o = {};
  anyref_global.value = o;
  assertEquals(o, anyref_global.value);
  assertEquals(o, wasm.get_extern());
  anyref_global.value = undefined;
  assertEquals(undefined, anyref_global.value);
})();

(function TestEqRefGlobalFromJS() {
  print(arguments.callee.name);
  let eqref_global = new WebAssembly.Global(
      { value: "eqref", mutable: true }, null);
  assertEquals(null, eqref_global.value);

  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("imports", "eqref_global", kWasmEqRef, true);
  let struct_type = builder.addStruct([makeField(kWasmI32, false)]);
  let array_type = builder.addArray(kWasmI32);

  builder.addFunction("get_extern", makeSig([], [kWasmExternRef]))
  .addBody([kExprGlobalGet, 0, kGCPrefix, kExprExternConvertAny])
  .exportFunc();
  builder.addFunction("get_struct_val", makeSig([], [kWasmI32]))
  .addBody([
    kExprGlobalGet, 0,
    kGCPrefix, kExprRefCast, struct_type,
    kGCPrefix, kExprStructGet, struct_type, 0,
  ])
  .exportFunc();
  builder.addFunction("get_array_val", makeSig([], [kWasmI32]))
  .addBody([
    kExprGlobalGet, 0,
    kGCPrefix, kExprRefCast, array_type,
    kExprI32Const, 0,
    kGCPrefix, kExprArrayGet, array_type,
  ])
  .exportFunc();
  builder.addFunction("create_struct", makeSig([kWasmI32], [kWasmExternRef]))
  .addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprStructNew, struct_type,
    kGCPrefix, kExprExternConvertAny])
  .exportFunc();
  builder.addFunction("create_array", makeSig([kWasmI32], [kWasmExternRef]))
  .addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprArrayNewFixed, array_type, 1,
    kGCPrefix, kExprExternConvertAny])
  .exportFunc();

  let instance = builder.instantiate({imports : {eqref_global}});
  let wasm = instance.exports;

  eqref_global.value = wasm.create_struct(42);
  assertEquals(42, wasm.get_struct_val());
  eqref_global.value = wasm.create_array(43);
  assertEquals(43, wasm.get_array_val());
  eqref_global.value = null;
  assertEquals(null, eqref_global.value);
  assertEquals(null, wasm.get_extern());
  eqref_global.value = 12345;
  assertEquals(12345, wasm.get_extern());

  assertThrows(() => eqref_global.value = {}, TypeError);
  assertThrows(() => eqref_global.value = undefined, TypeError);
  assertThrows(() => eqref_global.value = "string", TypeError);
})();

(function TestStructRefGlobalFromJS() {
  print(arguments.callee.name);
  let structref_global = new WebAssembly.Global(
      { value: "structref", mutable: true }, null);
  assertNull(structref_global.value);

  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("imports", "structref_global", kWasmStructRef, true);
  let struct_type = builder.addStruct([makeField(kWasmI32, false)]);
  let array_type = builder.addArray(kWasmI32);

  builder.addFunction("get_struct_val", makeSig([], [kWasmI32]))
  .addBody([
    kExprGlobalGet, 0,
    kGCPrefix, kExprRefCast, struct_type,
    kGCPrefix, kExprStructGet, struct_type, 0,
  ])
  .exportFunc();
  builder.addFunction("create_struct", makeSig([kWasmI32], [kWasmExternRef]))
  .addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprStructNew, struct_type,
    kGCPrefix, kExprExternConvertAny])
  .exportFunc();
  builder.addFunction("create_array", makeSig([kWasmI32], [kWasmExternRef]))
  .addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprArrayNewFixed, array_type, 1,
    kGCPrefix, kExprExternConvertAny])
  .exportFunc();

  let instance = builder.instantiate({imports : {structref_global}});
  let wasm = instance.exports;

  structref_global.value = wasm.create_struct(42);
  assertEquals(42, wasm.get_struct_val());
  structref_global.value = null;
  assertEquals(null, structref_global.value);

  assertThrows(() => structref_global.value = undefined, TypeError);
  assertThrows(() => structref_global.value = "string", TypeError);
  assertThrows(() => structref_global.value = wasm.create_array(1), TypeError);
})();

(function TestArrayRefGlobalFromJS() {
  print(arguments.callee.name);
  let arrayref_global = new WebAssembly.Global(
      { value: "arrayref", mutable: true }, null);
  assertNull(arrayref_global.value);

  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("imports", "arrayref_global", kWasmArrayRef, true);
  let struct_type = builder.addStruct([makeField(kWasmI32, false)]);
  let array_type = builder.addArray(kWasmI32);

  builder.addFunction("get_array_val", makeSig([], [kWasmI32]))
  .addBody([
    kExprGlobalGet, 0,
    kGCPrefix, kExprRefCast, array_type,
    kExprI32Const, 0,
    kGCPrefix, kExprArrayGet, array_type,
  ])
  .exportFunc();
  builder.addFunction("create_struct", makeSig([kWasmI32], [kWasmExternRef]))
  .addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprStructNew, struct_type,
    kGCPrefix, kExprExternConvertAny])
  .exportFunc();
  builder.addFunction("create_array", makeSig([kWasmI32], [kWasmExternRef]))
  .addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprArrayNewFixed, array_type, 1,
    kGCPrefix, kExprExternConvertAny])
  .exportFunc();

  let instance = builder.instantiate({imports : {arrayref_global}});
  let wasm = instance.exports;

  arrayref_global.value = wasm.create_array(43);
  assertEquals(43, wasm.get_array_val());
  arrayref_global.value = null;
  assertEquals(null, arrayref_global.value);

  assertThrows(() => arrayref_global.value = undefined, TypeError);
  assertThrows(() => arrayref_global.value = "string", TypeError);
  assertThrows(() => arrayref_global.value = wasm.create_struct(1), TypeError);
})();

(function TestI31RefGlobalFromJS() {
  print(arguments.callee.name);
  let i31ref_global = new WebAssembly.Global(
      { value: "i31ref", mutable: true }, 123);
  assertEquals(123, i31ref_global.value);

  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("imports", "i31ref_global", kWasmI31Ref, true);
  let struct_type = builder.addStruct([makeField(kWasmI32, false)]);

  builder.addFunction("get_i31", makeSig([], [kWasmI32]))
  .addBody([
    kExprGlobalGet, 0,
    kGCPrefix, kExprI31GetS
  ])
  .exportFunc();
  builder.addFunction("create_struct",
                      makeSig([kWasmI32], [wasmRefType(struct_type)]))
  .addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprStructNew, struct_type,
  ])
  .exportFunc();

  let instance = builder.instantiate({imports : {i31ref_global}});
  let wasm = instance.exports;
  assertEquals(123, i31ref_global.value);

  i31ref_global.value = 42;
  assertEquals(42, i31ref_global.value);
  assertEquals(42, wasm.get_i31());
  i31ref_global.value = null;
  assertEquals(null, i31ref_global.value);

  assertThrows(() => i31ref_global.value = undefined, TypeError);
  assertThrows(() => i31ref_global.value = "string", TypeError);
  assertThrows(() => i31ref_global.value = wasm.create_struct(1), TypeError);
  assertThrows(() => i31ref_global.value = Math.pow(2, 33), TypeError);
})();
