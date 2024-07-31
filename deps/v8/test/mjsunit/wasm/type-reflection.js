// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-type-reflection --expose-gc

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestMemoryType() {
  print(arguments.callee.name);
  let mem = new WebAssembly.Memory({initial: 1});
  let type = mem.type();
  assertEquals(1, type.minimum);
  assertEquals(false, type.shared);
  assertEquals(3, Object.getOwnPropertyNames(type).length);

  mem = new WebAssembly.Memory({initial: 2, maximum: 15});
  type = mem.type();
  assertEquals(2, type.minimum);
  assertEquals(15, type.maximum);
  assertEquals(false, type.shared);
  assertEquals("i32", type.index);
  assertEquals(4, Object.getOwnPropertyNames(type).length);

  mem = new WebAssembly.Memory({initial: 2, maximum: 15, index: "i64"});
  type = mem.type();
  assertEquals("i64", type.index);
})();

(function TestMemoryExports() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addMemory(1);
  builder.exportMemoryAs("a")
  let module = new WebAssembly.Module(builder.toBuffer());
  let exports = WebAssembly.Module.exports(module);

  assertEquals("a", exports[0].name);
  assertTrue("type" in exports[0]);
  assertEquals(1, exports[0].type.minimum);
  assertFalse("maximum" in exports[0].type);

  builder = new WasmModuleBuilder();
  builder.addMemory(2, 16);
  builder.exportMemoryAs("b")
  module = new WebAssembly.Module(builder.toBuffer());
  exports = WebAssembly.Module.exports(module);

  assertEquals("b", exports[0].name);
  assertTrue("type" in exports[0]);
  assertEquals(2, exports[0].type.minimum);
  assertEquals(16, exports[0].type.maximum);
})();

(function TestMemoryImports() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "a", 1);
  let module = new WebAssembly.Module(builder.toBuffer());
  let imports = WebAssembly.Module.imports(module);

  assertEquals("a", imports[0].name);
  assertEquals("m", imports[0].module);
  assertTrue("type" in imports[0]);
  assertEquals(1, imports[0].type.minimum);
  assertFalse("maximum" in imports[0].type);

  builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "b", 2, 16);
  module = new WebAssembly.Module(builder.toBuffer());
  imports = WebAssembly.Module.imports(module);

  assertEquals("b", imports[0].name);
  assertEquals("m", imports[0].module);
  assertTrue("type" in imports[0]);
  assertEquals(2, imports[0].type.minimum);
  assertEquals(16, imports[0].type.maximum);
})();

(function TestTableType() {
  print(arguments.callee.name);
  let table = new WebAssembly.Table({initial: 1, element: "funcref"});
  let type = table.type();
  assertEquals(1, type.minimum);
  assertEquals("funcref", type.element);
  assertEquals(undefined, type.maximum);
  assertEquals(2, Object.getOwnPropertyNames(type).length);

  table = new WebAssembly.Table({initial: 2, maximum: 15, element: "funcref"});
  type = table.type();
  assertEquals(2, type.minimum);
  assertEquals(15, type.maximum);
  assertEquals("funcref", type.element);
  assertEquals(3, Object.getOwnPropertyNames(type).length);

  table = new WebAssembly.Table({initial: 1, element: "anyfunc"});
  type = table.type();
  assertEquals(1, type.minimum);
  assertEquals("funcref", type.element);
  assertEquals(undefined, type.maximum);
  assertEquals(2, Object.getOwnPropertyNames(type).length);

  table = new WebAssembly.Table({initial: 2, maximum: 15, element: "anyfunc"});
  type = table.type();
  assertEquals(2, type.minimum);
  assertEquals(15, type.maximum);
  assertEquals("funcref", type.element);
  assertEquals(3, Object.getOwnPropertyNames(type).length);
})();

(function TestTableExports() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addTable(kWasmAnyFunc, 20).exportAs("a");
  let module = new WebAssembly.Module(builder.toBuffer());
  let exports = WebAssembly.Module.exports(module);

  assertEquals("a", exports[0].name);
  assertTrue("type" in exports[0]);
  assertEquals("funcref", exports[0].type.element);
  assertEquals(20, exports[0].type.minimum);
  assertFalse("maximum" in exports[0].type);

  builder = new WasmModuleBuilder();
  builder.addTable(kWasmAnyFunc, 15, 25).exportAs("b");
  module = new WebAssembly.Module(builder.toBuffer());
  exports = WebAssembly.Module.exports(module);

  assertEquals("b", exports[0].name);
  assertTrue("type" in exports[0]);
  assertEquals("funcref", exports[0].type.element);
  assertEquals(15, exports[0].type.minimum);
  assertEquals(25, exports[0].type.maximum);
})();

(function TestTableImports() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addImportedTable("m", "a", 20, undefined, kWasmAnyFunc);
  let module = new WebAssembly.Module(builder.toBuffer());
  let imports = WebAssembly.Module.imports(module);

  assertEquals("a", imports[0].name);
  assertEquals("m", imports[0].module);
  assertTrue("type" in imports[0]);
  assertEquals("funcref", imports[0].type.element);
  assertEquals(20, imports[0].type.minimum);
  assertFalse("maximum" in imports[0].type);

  builder = new WasmModuleBuilder();
  builder.addImportedTable("m", "b", 15, 25, kWasmAnyFunc);
  module = new WebAssembly.Module(builder.toBuffer());
  imports = WebAssembly.Module.imports(module);

  assertEquals("b", imports[0].name);
  assertEquals("m", imports[0].module);
  assertTrue("type" in imports[0]);
  assertEquals("funcref", imports[0].type.element);
  assertEquals(15, imports[0].type.minimum);
  assertEquals(25, imports[0].type.maximum);
})();

(function TestGlobalType() {
  print(arguments.callee.name);
  let global = new WebAssembly.Global({value: "i32", mutable: true});
  let type = global.type();
  assertEquals("i32", type.value);
  assertEquals(true, type.mutable);
  assertEquals(2, Object.getOwnPropertyNames(type).length);

  global = new WebAssembly.Global({value: "i32"});
  type = global.type();
  assertEquals("i32", type.value);
  assertEquals(false, type.mutable);
  assertEquals(2, Object.getOwnPropertyNames(type).length);

  global = new WebAssembly.Global({value: "i64"});
  type = global.type();
  assertEquals("i64", type.value);
  assertEquals(false, type.mutable);
  assertEquals(2, Object.getOwnPropertyNames(type).length);

  global = new WebAssembly.Global({value: "f32"});
  type = global.type();
  assertEquals("f32", type.value);
  assertEquals(false, type.mutable);
  assertEquals(2, Object.getOwnPropertyNames(type).length);

  global = new WebAssembly.Global({value: "f64"});
  type = global.type();
  assertEquals("f64", type.value);
  assertEquals(false, type.mutable);
  assertEquals(2, Object.getOwnPropertyNames(type).length);
})();

(function TestGlobalExports() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI32, false, false).exportAs("a");
  builder.addGlobal(kWasmF64, true, false).exportAs("b");
  let module = new WebAssembly.Module(builder.toBuffer());
  let exports = WebAssembly.Module.exports(module);

  assertEquals("a", exports[0].name);
  assertTrue("type" in exports[0]);
  assertEquals("i32", exports[0].type.value);
  assertEquals(false, exports[0].type.mutable);

  assertEquals("b", exports[1].name);
  assertTrue("type" in exports[1]);
  assertEquals("f64", exports[1].type.value);
  assertEquals(true, exports[1].type.mutable);
})();

(function TestGlobalImports() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("m", "a", kWasmI32);
  builder.addImportedGlobal("m", "b", kWasmF64, true);
  let module = new WebAssembly.Module(builder.toBuffer());
  let imports = WebAssembly.Module.imports(module);

  assertEquals("a", imports[0].name);
  assertEquals("m", imports[0].module);
  assertTrue("type" in imports[0]);
  assertEquals("i32", imports[0].type.value);
  assertEquals(false, imports[0].type.mutable);

  assertEquals("b", imports[1].name);
  assertEquals("m", imports[1].module);
  assertTrue("type" in imports[1]);
  assertEquals("f64", imports[1].type.value);
  assertEquals(true, imports[1].type.mutable);
})();

(function TestMemoryConstructorWithMinimum() {
  print(arguments.callee.name);
  let mem = new WebAssembly.Memory({minimum: 1});
  assertTrue(mem instanceof WebAssembly.Memory);
  let type = mem.type();
  assertEquals(1, type.minimum);
  assertEquals(false, type.shared);
  assertEquals("i32", type.index);
  assertEquals(3, Object.getOwnPropertyNames(type).length);

  mem = new WebAssembly.Memory({minimum: 1, maximum: 5, shared: false});
  assertTrue(mem instanceof WebAssembly.Memory);
  type = mem.type();
  assertEquals(1, type.minimum);
  assertEquals(5, type.maximum);
  assertEquals(false, type.shared);
  assertEquals("i32", type.index);
  assertEquals(4, Object.getOwnPropertyNames(type).length);

  mem = new WebAssembly.Memory({initial: 1, maximum: 5, shared: true});
  assertTrue(mem instanceof WebAssembly.Memory);
  type = mem.type();
  assertEquals(1, type.minimum);
  assertEquals(5, type.maximum);
  assertEquals(true, type.shared);
  assertEquals("i32", type.index);
  assertEquals(4, Object.getOwnPropertyNames(type).length);

  assertThrows(
      () => new WebAssembly.Memory({minimum: 1, initial: 2}), TypeError,
      /The properties 'initial' and 'minimum' are not allowed at the same time/);

  assertThrows(
      () => new WebAssembly.Memory({minimum: 1, initial: 2, maximum: 5}),
      TypeError,
      /The properties 'initial' and 'minimum' are not allowed at the same time/);
})();

(function TestTableConstructorWithMinimum() {
  print(arguments.callee.name);
  let table = new WebAssembly.Table({minimum: 1, element: 'funcref'});
  assertTrue(table instanceof WebAssembly.Table);
  let type = table.type();
  assertEquals(1, type.minimum);
  assertEquals('funcref', type.element);
  assertEquals(2, Object.getOwnPropertyNames(type).length);

  table = new WebAssembly.Table({minimum: 1, element: 'funcref', maximum: 5});
  assertTrue(table instanceof WebAssembly.Table);
  type = table.type();
  assertEquals(1, type.minimum);
  assertEquals(5, type.maximum);
  assertEquals('funcref', type.element);
  assertEquals(3, Object.getOwnPropertyNames(type).length);

  assertThrows(
      () => new WebAssembly.Table({minimum: 1, initial: 2, element: 'funcref'}),
      TypeError,
      /The properties 'initial' and 'minimum' are not allowed at the same time/);

  assertThrows(
      () => new WebAssembly.Table({minimum: 1, initial: 2, element: 'funcref',
                                 maximum: 5}),
      TypeError,
      /The properties 'initial' and 'minimum' are not allowed at the same time/);
})();

(function TestFunctionConstructor() {
  print(arguments.callee.name);
  let toolong = new Array(1000 + 1);
  let desc = Object.getOwnPropertyDescriptor(WebAssembly, 'Function');
  assertEquals(typeof desc.value, 'function');
  assertTrue(desc.writable);
  assertFalse(desc.enumerable);
  assertTrue(desc.configurable);
  // TODO(7742): The length should probably be 2 instead.
  assertEquals(WebAssembly.Function.length, 1);
  assertEquals(WebAssembly.Function.name, 'Function');
  assertThrows(
      () => WebAssembly.Function(), TypeError, /must be invoked with 'new'/);
  assertThrows(
    () => new WebAssembly.Function(), TypeError,
    /Argument 0 must be a function type/);
  assertThrows(
    () => new WebAssembly.Function({}), TypeError,
    /Argument 0 must be a function type with 'parameters'/);
  assertThrows(
    () => new WebAssembly.Function({parameters:[]}), TypeError,
    /Argument 0 must be a function type with 'results'/);
  assertThrows(
    () => new WebAssembly.Function({parameters:['foo'], results:[]}), TypeError,
    /Argument 0 parameter type at index #0 must be a value type/);
  assertThrows(
    () => new WebAssembly.Function({parameters:[], results:['foo']}), TypeError,
    /Argument 0 result type at index #0 must be a value type/);
  assertThrows(
    () => new WebAssembly.Function({parameters:toolong, results:[]}), TypeError,
    /Argument 0 contains too many parameters/);
  assertThrows(
    () => new WebAssembly.Function({parameters:[], results:toolong}), TypeError,
    /Argument 0 contains too many results/);
  assertThrows(
    () => new WebAssembly.Function({parameters:[], results:[]}), TypeError,
    /Argument 1 must be a function/);
  assertThrows(
    () => new WebAssembly.Function({parameters:[], results:[]}, {}), TypeError,
    /Argument 1 must be a function/);
  assertDoesNotThrow(
    () => new WebAssembly.Function({parameters:[], results:[]}, _ => 0));
})();

(function TestFunctionConstructorWithWasmExportedFunction() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addFunction('func', kSig_i_i)
      .addBody([...wasmI32Const(42)])
      .exportFunc();

  const instance = builder.instantiate();

  assertDoesNotThrow(
      () => new WebAssembly.Function(
          {parameters: [], results: []}, instance.exports.func));

  assertDoesNotThrow(() => {
    const rewrapped = new WebAssembly.Function(
      {parameters: ['f32'], results: ['i32']}, instance.exports.func);
    rewrapped(1)
    rewrapped(NaN);
  });

  assertThrows(
    () => {
      const rewrapped = new WebAssembly.Function(
        {parameters: ['i64'], results: ['i32']}, instance.exports.func);
      rewrapped(0n);
    },
    TypeError,
    "Cannot convert a BigInt value to a number");
  assertThrows(
    () => {
      const rewrapped = new WebAssembly.Function(
        {parameters: ['i32'], results: ['i64']}, instance.exports.func);
      rewrapped(0);
    },
    TypeError,
    "Cannot convert 42 to a BigInt");
})();

(function TestFunctionConstructorWithWasmJSFunction() {
  print(arguments.callee.name);
  const func = new WebAssembly.Function({parameters: ['i32'], results: ['i32']}, _ => 0);

  assertDoesNotThrow(
      () => new WebAssembly.Function({parameters: ['i32'], results: ['i32']}, func));
  assertDoesNotThrow(() => {
    const rewrapped = new WebAssembly.Function({parameters: ['f32'], results: ['i32']}, func);
    rewrapped(42);
    rewrapped(NaN);
  });
  assertThrows(
      () => {
        const rewrapped = new WebAssembly.Function(
          {parameters: ['i64'], results: ['i32']}, func);
        rewrapped(0n);
      },
      TypeError,
      "Cannot convert a BigInt value to a number");
  assertThrows(
      () => {
        const rewrapped = new WebAssembly.Function(
          {parameters: ['i32'], results: ['i64']}, func);
        rewrapped(0);
      },
      TypeError,
      "Cannot convert 0 to a BigInt");
})();

(function TestFunctionConstructorNonArray1() {
  print(arguments.callee.name);
  let log = [];  // Populated with a log of accesses.
  let two = { toString: () => "2" };  // Just a fancy "2".
  let logger = new Proxy({ length: two, "0": "i32", "1": "f32"}, {
    get: function(obj, prop) { log.push(prop); return Reflect.get(obj, prop); },
    set: function(obj, prop, val) { assertUnreachable(); }
  });
  let fun = new WebAssembly.Function({parameters:logger, results:[]}, _ => 0);
  assertArrayEquals(["i32", "f32"], fun.type().parameters);
  assertArrayEquals(["length", "0", "1"], log);
})();

(function TestFunctionConstructorNonArray2() {
  print(arguments.callee.name);
  let throw1 = { get length() { throw new Error("cannot see length"); }};
  let throw2 = { length: { toString: _ => { throw new Error("no length") } } };
  let throw3 = { length: "not a length value, this also throws" };
  assertThrows(
    () => new WebAssembly.Function({parameters:throw1, results:[]}), Error,
    /cannot see length/);
  assertThrows(
    () => new WebAssembly.Function({parameters:throw2, results:[]}), Error,
    /no length/);
  assertThrows(
    () => new WebAssembly.Function({parameters:throw3, results:[]}), TypeError,
    /Argument 0 contains parameters without 'length'/);
  assertThrows(
    () => new WebAssembly.Function({parameters:[], results:throw1}), Error,
    /cannot see length/);
  assertThrows(
    () => new WebAssembly.Function({parameters:[], results:throw2}), Error,
    /no length/);
  assertThrows(
    () => new WebAssembly.Function({parameters:[], results:throw3}), TypeError,
    /Argument 0 contains results without 'length'/);
})();

(function TestFunctionConstructedFunction() {
  print(arguments.callee.name);
  let fun = new WebAssembly.Function({parameters:[], results:[]}, _ => 0);
  assertTrue(fun instanceof WebAssembly.Function);
  assertTrue(fun instanceof Function);
  assertTrue(fun instanceof Object);
  assertSame(fun.__proto__, WebAssembly.Function.prototype);
  assertSame(fun.__proto__.__proto__, Function.prototype);
  assertSame(fun.__proto__.__proto__.__proto__, Object.prototype);
  assertSame(fun.constructor, WebAssembly.Function);
  assertEquals(typeof fun, 'function');
  assertDoesNotThrow(() => fun());
})();

(function TestFunctionExportedFunction() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("fun", kSig_v_v).addBody([]).exportFunc();
  let instance = builder.instantiate();
  let fun = instance.exports.fun;
  assertTrue(fun instanceof WebAssembly.Function);
  assertTrue(fun instanceof Function);
  assertTrue(fun instanceof Object);
  assertSame(fun.__proto__, WebAssembly.Function.prototype);
  assertSame(fun.__proto__.__proto__, Function.prototype);
  assertSame(fun.__proto__.__proto__.__proto__, Object.prototype);
  assertSame(fun.constructor, WebAssembly.Function);
  assertEquals(typeof fun, 'function');
  assertDoesNotThrow(() => fun());
})();

(function TestFunctionTypeOfConstructedFunction() {
  print(arguments.callee.name);
  let testcases = [
    {parameters:[], results:[]},
    {parameters:["i32"], results:[]},
    {parameters:["i64"], results:["i32"]},
    {parameters:["f64", "f64", "i32"], results:[]},
    {parameters:["f32"], results:["f32"]},
  ];
  testcases.forEach(function(expected) {
    let fun = new WebAssembly.Function(expected, _ => 0);
    let type = fun.type();
    assertEquals(expected, type)
  });
})();

(function TestFunctionTypeOfExportedFunction() {
  print(arguments.callee.name);
  let testcases = [
    [kSig_v_v, {parameters:[], results:[]}],
    [kSig_v_i, {parameters:["i32"], results:[]}],
    [kSig_i_l, {parameters:["i64"], results:["i32"]}],
    [kSig_v_ddi, {parameters:["f64", "f64", "i32"], results:[]}],
    [kSig_f_f, {parameters:["f32"], results:["f32"]}],
  ];
  testcases.forEach(function([sig, expected]) {
    let builder = new WasmModuleBuilder();
    builder.addFunction("fun", sig).addBody([kExprUnreachable]).exportFunc();
    let instance = builder.instantiate();
    let type = instance.exports.fun.type();
    assertEquals(expected, type)
  });
})();

(function TestFunctionExports() {
  print(arguments.callee.name);
  let testcases = [
    [kSig_v_v, {parameters:[], results:[]}],
    [kSig_v_i, {parameters:["i32"], results:[]}],
    [kSig_i_l, {parameters:["i64"], results:["i32"]}],
    [kSig_v_ddi, {parameters:["f64", "f64", "i32"], results:[]}],
    [kSig_f_f, {parameters:["f32"], results:["f32"]}],
  ];
  testcases.forEach(function([sig, expected]) {
    let builder = new WasmModuleBuilder();
    builder.addFunction("fun", sig).addBody([kExprUnreachable]).exportFunc();
    let module = new WebAssembly.Module(builder.toBuffer());
    let exports = WebAssembly.Module.exports(module);
    assertEquals("fun", exports[0].name);
    assertTrue("type" in exports[0]);
    assertEquals(expected, exports[0].type);
  });
})();

(function TestFunctionImports() {
  print(arguments.callee.name);
  let testcases = [
    [kSig_v_v, {parameters:[], results:[]}],
    [kSig_v_i, {parameters:["i32"], results:[]}],
    [kSig_i_l, {parameters:["i64"], results:["i32"]}],
    [kSig_v_ddi, {parameters:["f64", "f64", "i32"], results:[]}],
    [kSig_f_f, {parameters:["f32"], results:["f32"]}],
  ];
  testcases.forEach(function([sig, expected]) {
    let builder = new WasmModuleBuilder();
    builder.addImport("m", "fun", sig);
    let module = new WebAssembly.Module(builder.toBuffer());
    let imports = WebAssembly.Module.imports(module);
    assertEquals("fun", imports[0].name);
    assertEquals("m", imports[0].module);
    assertTrue("type" in imports[0]);
    assertEquals(expected, imports[0].type);
  });
})();

(function TestFunctionConstructedCoercions() {
  print(arguments.callee.name);
  let obj1 = { valueOf: _ => 123.45 };
  let obj2 = { toString: _ => "456" };
  let gcer = { valueOf: _ => gc() };
  let testcases = [
    { params: { sig: ["i32"],
                val: [23.5],
                exp: [23], },
      result: { sig: ["i32"],
                val: 42.7,
                exp: 42, },
    },
    { params: { sig: ["i32", "f32", "f64"],
                val: [obj1,  obj2,  "789"],
                exp: [123,   456,   789], },
      result: { sig: [],
                val: undefined,
                exp: undefined, },
    },
    { params: { sig: ["i32", "f32", "f64"],
                val: [gcer,  {},    "xyz"],
                exp: [0,     NaN,   NaN], },
      result: { sig: ["f64"],
                val: gcer,
                exp: NaN, },
    },
  ];
  testcases.forEach(function({params, result}) {
    let p = params.sig; let r = result.sig; var params_after;
    function testFun() { params_after = arguments; return result.val; }
    let fun = new WebAssembly.Function({parameters:p, results:r}, testFun);
    let result_after = fun.apply(undefined, params.val);
    assertArrayEquals(params.exp, params_after);
    assertEquals(result.exp, result_after);
  });
})();

(function TestFunctionTableSetI64() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let fun = new WebAssembly.Function({parameters:[], results:["i64"]}, _ => 0n);
  let table = new WebAssembly.Table({element: "anyfunc", initial: 2});
  let table_index = builder.addImportedTable("m", "table", 2);
  let sig_index = builder.addType(kSig_l_v);
  table.set(0, fun);
  builder.addFunction('main', kSig_v_i)
      .addBody([
        kExprLocalGet, 0,
        kExprCallIndirect, sig_index, table_index,
        kExprDrop
      ])
      .exportFunc();
  let instance = builder.instantiate({ m: { table: table }});
  assertDoesNotThrow(() => instance.exports.main(0));
  assertTraps(kTrapFuncSigMismatch, () => instance.exports.main(1));
  table.set(1, fun);
  assertDoesNotThrow(() => instance.exports.main(1));
})();

(function TestFunctionModuleImportMatchingSig() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let fun = new WebAssembly.Function({parameters:[], results:["i32"]}, _ => 7);
  let fun_index = builder.addImport("m", "fun", kSig_i_v)
  builder.addFunction('main', kSig_i_v)
      .addBody([
        kExprCallFunction, fun_index
      ])
      .exportFunc();
  let instance = builder.instantiate({ m: { fun: fun }});
  assertEquals(7, instance.exports.main());
})();

(function TestFunctionModuleImportMismatchingSig() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let fun1 = new WebAssembly.Function({parameters:[], results:[]}, _ => 7);
  let fun2 = new WebAssembly.Function({parameters:["i32"], results:[]}, _ => 8);
  let fun3 = new WebAssembly.Function({parameters:[], results:["f32"]}, _ => 9);
  let fun_index = builder.addImport("m", "fun", kSig_i_v)
  builder.addFunction('main', kSig_i_v)
      .addBody([
        kExprCallFunction, fun_index
      ])
      .exportFunc();
  assertThrows(
    () => builder.instantiate({ m: { fun: fun1 }}), WebAssembly.LinkError,
    /imported function does not match the expected type/);
  assertThrows(
    () => builder.instantiate({ m: { fun: fun2 }}), WebAssembly.LinkError,
    /imported function does not match the expected type/);
  assertThrows(
    () => builder.instantiate({ m: { fun: fun3 }}), WebAssembly.LinkError,
    /imported function does not match the expected type/);
})();

(function TestFunctionModuleImportReExport () {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let fun = new WebAssembly.Function({parameters:[], results:["i32"]}, _ => 7);
  let fun_index = builder.addImport("m", "fun", kSig_i_v)
  builder.addExport("fun1", fun_index);
  builder.addExport("fun2", fun_index);
  let instance = builder.instantiate({ m: { fun: fun }});
  assertSame(instance.exports.fun1, instance.exports.fun2);
  assertSame(fun, instance.exports.fun1);
})();

(function TestCallIndirectJSFunction() {
  print(arguments.callee.name);
  let imp = new WebAssembly.Function(
    {parameters:["i32", "i32", "i32"], results:["i32"]},
    function(a, b, c) { if (c) return a; return b; });

  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_i_iii);
  let fun_index = builder.addImport("m", "imp", kSig_i_iii)
  builder.addTable(kWasmFuncRef, 1, 1);
  let table_index = 0;
  let segment = builder.addActiveElementSegment(
      table_index, wasmI32Const(0), [[kExprRefFunc, 0]], kWasmFuncRef);

  let main = builder.addFunction("rc", kSig_i_i)
      .addBody([...wasmI32Const(-2), kExprI32Const, 3, kExprLocalGet, 0,
                kExprI32Const, 0, kExprCallIndirect, sig_index, table_index])
      .exportFunc();

  let instance = builder.instantiate({ m: { imp: imp }});

  assertEquals(instance.exports.rc(1), -2);
  assertEquals(instance.exports.rc(0), 3);
})();

(function TestWebAssemblyFunctionBind() {
  print(arguments.callee.name);
  let fn = new WebAssembly.Function(
    {parameters:["i32", "i32", "i32"], results:["i32"]},
    function(a, b, c) { if (c) return a; return b; });

  const bound = fn.bind(null, 42)
  assertTrue(bound instanceof Function);
  assertFalse(bound instanceof WebAssembly.Function);
  assertEquals(bound(0, true), 42);
})();
