// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestMultipleInstances() {
  print("TestMultipleInstances");

  var builder = new WasmModuleBuilder();

  let g = builder.addGlobal(kWasmI32, true, false);
  let sig_index = builder.addType(kSig_i_v);
  builder.addFunction("get", sig_index)
    .addBody([
      kExprGlobalGet, g.index])
    .exportAs("get");
  builder.addFunction("set", kSig_v_i)
    .addBody([
      kExprLocalGet, 0,
      kExprGlobalSet, g.index])
    .exportAs("set");

  let module = new WebAssembly.Module(builder.toBuffer());

  let a = new WebAssembly.Instance(module);
  let b = new WebAssembly.Instance(module);

  assertEquals(0, a.exports.get());
  assertEquals(0, b.exports.get());

  a.exports.set(1);

  assertEquals(1, a.exports.get());
  assertEquals(0, b.exports.get());

  b.exports.set(6);

  assertEquals(1, a.exports.get());
  assertEquals(6, b.exports.get());

  a.exports.set(7);

  assertEquals(7, a.exports.get());
  assertEquals(6, b.exports.get());

})();

function TestImported(type, val, expected) {
  print("TestImported " + type + "(" + val +")" + " = " + expected);
  var builder = new WasmModuleBuilder();
  var sig = makeSig([], [type]);
  var g = builder.addImportedGlobal("uuu", "foo", type);
  builder.addFunction("main", sig)
    .addBody([kExprGlobalGet, g])
    .exportAs("main");
  builder.addGlobal(kWasmI32, false, false);  // pad

  var instance = builder.instantiate({uuu: {foo: val}});
  assertEquals(expected, instance.exports.main());
}

TestImported(kWasmI32, 300.1, 300);
TestImported(kWasmF32, 87234.87238, Math.fround(87234.87238));
TestImported(kWasmF64, 77777.88888, 77777.88888);


(function TestImportedMultipleInstances() {
  print("TestImportedMultipleInstances");

  var builder = new WasmModuleBuilder();

  let g = builder.addImportedGlobal("mod", "g", kWasmI32);
  let sig_index = builder.addType(kSig_i_v);
  builder.addFunction("main", sig_index)
    .addBody([
      kExprGlobalGet, g])
    .exportAs("main");

  let module = new WebAssembly.Module(builder.toBuffer());

  print("  i 100...");
  let i100 = new WebAssembly.Instance(module, {mod: {g: 100}});
  assertEquals(100, i100.exports.main());

  print("  i 300...");
  let i300 = new WebAssembly.Instance(module, {mod: {g: 300}});
  assertEquals(300, i300.exports.main());
})();

function TestExported(type, val, expected) {
  print("TestExported " + type + "(" + val +")" + " = " + expected);
  var builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI32, false, false);  // pad
  builder.addGlobal(type, false, false, val).exportAs("foo");
  builder.addGlobal(kWasmI32, false, false);  // pad

  var instance = builder.instantiate();
  assertEquals(expected, instance.exports.foo.value);
}

TestExported(kWasmI32, wasmI32Const(455.5), 455);
TestExported(kWasmF32, wasmF32Const(-999.34343),
             Math.fround(-999.34343));
TestExported(kWasmF64, wasmF64Const(87347.66666), 87347.66666);

(function TestI64Exported() {
  var builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI32, false, false);  // pad
  builder.addGlobal(kWasmI64, false, false, wasmI64Const(1234)).exportAs("foo");
  builder.addGlobal(kWasmI32, false, false);  // pad

  var instance = builder.instantiate();
  assertTrue(instance.exports.foo instanceof WebAssembly.Global);
  assertEquals(instance.exports.foo.value, 1234n);
})();

function TestImportedExported(type, val, expected) {
  print("TestImportedExported " + type + "(" + val + ")" + " = " + expected);
  var builder = new WasmModuleBuilder();
  var i = builder.addImportedGlobal("ttt", "foo", type);
  builder.addGlobal(kWasmI32, false, false);  // pad
  builder.addGlobal(type, false, false, [kExprGlobalGet, i]).exportAs("bar");
  builder.addGlobal(kWasmI32, false, false);  // pad

  var instance = builder.instantiate({ttt: {foo: val}});
  assertEquals(expected, instance.exports.bar.value);
}

TestImportedExported(kWasmI32, 415.5, 415);
TestImportedExported(kWasmF32, -979.34343, Math.fround(-979.34343));
TestImportedExported(kWasmF64, 81347.66666, 81347.66666);

function TestGlobalIndexSpace(type, val) {
  print("TestGlobalIndexSpace(" + val + ") = " + val);
  var builder = new WasmModuleBuilder();
  var im = builder.addImportedGlobal("nnn", "foo", type);
  assertEquals(0, im);
  var def = builder.addGlobal(type, false, false, [kExprGlobalGet, im]);
  assertEquals(1, def.index);

  var sig = makeSig([], [type]);
  builder.addFunction("main", sig)
    .addBody([kExprGlobalGet, def.index])
    .exportAs("main");

  var instance = builder.instantiate({nnn: {foo: val}});
  assertEquals(val, instance.exports.main());
}

TestGlobalIndexSpace(kWasmI32, 123);
TestGlobalIndexSpace(kWasmF32, 54321.125);
TestGlobalIndexSpace(kWasmF64, 12345.678);

(function TestAccessesInBranch() {
  print("TestAccessesInBranches");

  var builder = new WasmModuleBuilder();

  let g = builder.addGlobal(kWasmI32, true, false);
  let h = builder.addGlobal(kWasmI32, true, false);
  let sig_index = builder.addType(kSig_i_i);
  builder.addFunction("get", sig_index)
    .addBody([
      kExprLocalGet, 0,
      kExprIf, kWasmI32,
      kExprGlobalGet, g.index,
      kExprElse,
      kExprGlobalGet, h.index,
      kExprEnd])
    .exportAs("get");
  builder.addFunction("set", kSig_v_ii)
    .addBody([
      kExprLocalGet, 0,
      kExprIf, kWasmVoid,
      kExprLocalGet, 1,
      kExprGlobalSet, g.index,
      kExprElse,
      kExprLocalGet, 1,
      kExprGlobalSet, h.index,
      kExprEnd])
    .exportAs("set");

  let module = new WebAssembly.Module(builder.toBuffer());

  let a = new WebAssembly.Instance(module);
  let get = a.exports.get;
  let set = a.exports.set;

  assertEquals(0, get(0));
  assertEquals(0, get(1));
  set(0, 1);
  assertEquals(1, get(0));
  assertEquals(0, get(1));

  set(0, 7);
  assertEquals(7, get(0));
  assertEquals(0, get(1));

  set(1, 9);
  assertEquals(7, get(0));
  assertEquals(9, get(1));

})();

(function testAssignUndefinedToGlobal() {
  print(arguments.callee.name);
  let i32_global = new WebAssembly.Global({mutable: true, value: 'i32'});
  i32_global.value = undefined;
  assertSame(0, i32_global.value);
  let i64_global = new WebAssembly.Global({mutable: true, value: 'i64'});
  assertThrows(() => {
    i64_global.value = undefined;
  }, TypeError);
  let f32_global = new WebAssembly.Global({mutable: true, value: 'f32'});
  f32_global.value = undefined;
  assertSame(NaN, f32_global.value);
  let f64_global = new WebAssembly.Global({mutable: true, value: 'f64'});
  f64_global.value = undefined;
  assertSame(NaN, f64_global.value);
})();
