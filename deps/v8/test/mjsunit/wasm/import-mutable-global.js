// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestBasic() {
  let global = new WebAssembly.Global({value: 'i32'}, 1);
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("mod", "g", kWasmI32);
  builder.addFunction("main", kSig_i_v)
    .addBody([kExprGlobalGet, 0])
    .exportAs("main");

  let main = builder.instantiate({mod: {g: global}}).exports.main;
  assertEquals(1, main());
})();

(function TestTypeMismatch() {
  let global = new WebAssembly.Global({value: 'f32'}, 1);
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("mod", "g", kWasmI32);

  assertThrows(() => builder.instantiate({mod: {g: global}}));
})();

(function TestMutableMismatch() {
  let global = new WebAssembly.Global({value: 'f64'}, 1);
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("mod", "g", kWasmF64, true);

  assertThrows(() => builder.instantiate({mod: {g: global}}));

  global = new WebAssembly.Global({value: 'i32', mutable: true}, 1);
  builder = new WasmModuleBuilder();
  builder.addImportedGlobal("mod", "g", kWasmI32);

  assertThrows(() => builder.instantiate({mod: {g: global}}));
})();

(function TestImportMutableAsNumber() {
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("mod", "g", kWasmF32, true);
  assertThrows(() => builder.instantiate({mod: {g: 1234}}));
})();

(function TestImportI64AsNumber() {
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("mod", "g", kWasmI64);
  assertThrows(() => builder.instantiate({mod: {g: 1234}}));
})();

function addGlobalGetterAndSetter(builder, index, name, type) {
  builder.addFunction('get' + name, makeSig([], [type]))
      .addBody([kExprGlobalGet, index])
      .exportFunc();
  builder.addFunction('set' + name, makeSig([type], []))
      .addBody([kExprLocalGet, 0, kExprGlobalSet, index])
      .exportFunc();
}

(function TestImportMutableGlobal() {
  const globalDesc = [
    {name: 'i32', type: kWasmI32},
    {name: 'f32', type: kWasmF32},
    {name: 'f64', type: kWasmF64},
  ];

  let globals = {};
  let builder = new WasmModuleBuilder();

  for (let [index, {name, type}] of globalDesc.entries()) {
    globals[name] = new WebAssembly.Global({value: name, mutable: true});
    builder.addImportedGlobal("mod", name, type, true);
    addGlobalGetterAndSetter(builder, index, name, type);
  }

  let inst = builder.instantiate(
      {mod: {i32: globals.i32, f32: globals.f32, f64: globals.f64}});

  for (let type of ['i32', 'f32', 'f64']) {
    let global = globals[type];
    let get = inst.exports['get' + type];
    let set = inst.exports['set' + type];

    assertEquals(get(), global.value, type);
    set(1234567);
    assertEquals(1234567, global.value, type);
    global.value = 7654321;
    assertEquals(7654321, get(), type);
  }
})();

(function TestImportExportedMutableGlobal() {
  const globalDesc = [
    {name: 'i32', type: kWasmI32},
    {name: 'f32', type: kWasmF32},
    {name: 'f64', type: kWasmF64},
  ];

  let builder1 = new WasmModuleBuilder();
  let builder2 = new WasmModuleBuilder();
  for (let [index, {name, type}] of globalDesc.entries()) {
    builder1.addGlobal(type, true, false).exportAs(name);
    addGlobalGetterAndSetter(builder1, index, name, type);

    builder2.addImportedGlobal("mod", name, type, true);
    addGlobalGetterAndSetter(builder2, index, name, type);
  }
  let inst1 = builder1.instantiate();
  let inst2 = builder2.instantiate({
    mod: {
      i32: inst1.exports.i32,
      f32: inst1.exports.f32,
      f64: inst1.exports.f64
    }
  });

  for (let type of ['i32', 'f32', 'f64']) {
    let get1 = inst1.exports['get' + type];
    let get2 = inst2.exports['get' + type];
    let set1 = inst1.exports['set' + type];
    let set2 = inst2.exports['set' + type];

    assertEquals(get1(), get2(), type);
    set1(1234567);
    assertEquals(1234567, get2(), type);
    set2(7654321);
    assertEquals(7654321, get1(), type);
  }
})();

(function TestImportExportedMutableGlobalI64() {
  function addGettersAndSetters(builder) {
    const index = 0;
    builder.addFunction('geti64_hi', makeSig([], [kWasmI32]))
      .addBody([
        kExprGlobalGet, index,
        kExprI64Const, 32, kExprI64ShrU,
        kExprI32ConvertI64])
      .exportFunc();
    builder.addFunction('geti64_lo', makeSig([], [kWasmI32]))
      .addBody([kExprGlobalGet, index, kExprI32ConvertI64])
      .exportFunc();
    builder.addFunction("seti64", makeSig([kWasmI32, kWasmI32], []))
      .addBody([
        kExprLocalGet, 1, kExprI64UConvertI32,
        kExprLocalGet, 0, kExprI64UConvertI32,
        kExprI64Const, 32, kExprI64Shl,
        kExprI64Ior,
        kExprGlobalSet, index])
      .exportFunc();
  };

  let builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI64, true, false).exportAs('i64');
  addGettersAndSetters(builder);
  let inst1 = builder.instantiate();

  builder = new WasmModuleBuilder();
  builder.addImportedGlobal("mod", 'i64', kWasmI64, true);
  addGettersAndSetters(builder);
  let inst2 = builder.instantiate({ mod: { i64: inst1.exports.i64 } });

  assertEquals(inst1.exports.geti64_lo(), inst2.exports.geti64_lo());
  assertEquals(inst1.exports.geti64_hi(), inst2.exports.geti64_hi());
  inst1.exports.seti64(13579, 24680);
  assertEquals(13579, inst2.exports.geti64_hi());
  assertEquals(24680, inst2.exports.geti64_lo());
  inst2.exports.seti64(97531, 86420);
  assertEquals(97531, inst1.exports.geti64_hi());
  assertEquals(86420, inst1.exports.geti64_lo());
})();

(function TestImportMutableAcrossGc() {
  let builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI32, true, false).exportAs('i32');
  let inst1 = builder.instantiate();

  builder = new WasmModuleBuilder();
  builder.addImportedGlobal("mod", 'i32', kWasmI32, true);
  addGlobalGetterAndSetter(builder, 0, 'i32', kWasmI32);
  let inst2 = builder.instantiate({ mod: { i32: inst1.exports.i32 } });

  delete inst1;
  gc();

  inst2.exports.seti32(0x789abcde);
  assertEquals(0x789abcde, inst2.exports.geti32());
})();

(function TestImportedAndNonImportedMutableGlobal() {
  let global = new WebAssembly.Global({value: 'i32', mutable: true}, 1);
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("mod", "g", kWasmI32, true);
  builder.addGlobal(kWasmI32, true, false).exportAs('i32');
  builder.instantiate({mod: {g: global}});
})();
