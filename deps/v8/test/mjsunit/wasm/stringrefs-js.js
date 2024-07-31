// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-stringref

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function assertInvalid(fn, message) {
  let builder = new WasmModuleBuilder();
  fn(builder);
  assertThrows(() => builder.toModule(), WebAssembly.CompileError,
               `WebAssembly.Module(): ${message}`);
}

(function TestPassthrough() {
  let kSig_w_w = makeSig([kWasmStringRef], [kWasmStringRef]);
  let builder = new WasmModuleBuilder();

  builder.addFunction("passthrough", kSig_w_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
    ]);

  let instance = builder.instantiate()

  assertEquals('foo', instance.exports.passthrough('foo'));
  assertEquals(null, instance.exports.passthrough(null));

  assertThrows(()=>instance.exports.passthrough(3),
               TypeError, "type incompatibility when transforming from/to JS");
  assertThrows(()=>instance.exports.passthrough({}),
               TypeError, "type incompatibility when transforming from/to JS");
  assertThrows(()=>instance.exports.passthrough({valueOf: ()=>'foo'}),
               TypeError, "type incompatibility when transforming from/to JS");
  assertThrows(()=>instance.exports.passthrough(undefined),
               TypeError, "type incompatibility when transforming from/to JS");
  assertThrows(()=>instance.exports.passthrough(),
               TypeError, "type incompatibility when transforming from/to JS");
})();

(function TestSwap() {
  let kSig_ww_ww = makeSig([kWasmStringRef, kWasmStringRef],
                           [kWasmStringRef, kWasmStringRef]);
  let builder = new WasmModuleBuilder();

  builder.addFunction("swap", kSig_ww_ww)
    .exportFunc()
    .addBody([
      kExprLocalGet, 1,
      kExprLocalGet, 0,
    ]);

  let instance = builder.instantiate()

  assertArrayEquals(['bar', 'foo'], instance.exports.swap('foo', 'bar'));
  assertArrayEquals(['bar', null], instance.exports.swap(null, 'bar'));
  assertArrayEquals([null, 'foo'], instance.exports.swap('foo', null));
})();

(function TestCallout() {
  let kSig_w_w = makeSig([kWasmStringRef], [kWasmStringRef]);
  let builder = new WasmModuleBuilder();

  builder.addImport("env", "transformer", kSig_w_w)
  builder.addFunction("transform", kSig_w_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, 0
    ]);

  let instance = builder.instantiate(
      { env: { transformer: x=>x.toUpperCase() } });

  assertEquals('FOO', instance.exports.transform('foo'));
})();

(function TestViewsUnsupported() {
  let kSig_x_v = makeSig([], [kWasmStringViewWtf8]);
  let kSig_y_v = makeSig([], [kWasmStringViewWtf16]);
  let kSig_z_v = makeSig([], [kWasmStringViewIter]);
  let builder = new WasmModuleBuilder();

  builder.addFunction("stringview_wtf8", kSig_x_v)
    .exportFunc()
    .addLocals(kWasmStringViewWtf8, 1)
    .addBody([
      kExprLocalGet, 0,
    ]);
  builder.addFunction("stringview_wtf16", kSig_y_v)
    .exportFunc()
    .addLocals(kWasmStringViewWtf16, 1)
    .addBody([
      kExprLocalGet, 0,
    ]);
  builder.addFunction("stringview_iter", kSig_z_v)
    .exportFunc()
    .addLocals(kWasmStringViewIter, 1)
    .addBody([
      kExprLocalGet, 0,
    ]);

  let instance = builder.instantiate()

  assertThrows(()=>instance.exports.stringview_wtf8(),
               TypeError, "type incompatibility when transforming from/to JS");
  assertThrows(()=>instance.exports.stringview_wtf16(),
               TypeError, "type incompatibility when transforming from/to JS");
  assertThrows(()=>instance.exports.stringview_iter(),
               TypeError, "type incompatibility when transforming from/to JS");
})();

(function TestDefinedGlobals() {
  let kSig_w_v = makeSig([], [kWasmStringRef]);
  let kSig_v_w = makeSig([kWasmStringRef], []);
  let kSig_x_v = makeSig([], [kWasmStringViewWtf8]);
  let kSig_y_v = makeSig([], [kWasmStringViewWtf16]);
  let kSig_z_v = makeSig([], [kWasmStringViewIter]);
  let builder = new WasmModuleBuilder();

  builder.addGlobal(kWasmStringRef, true, false).exportAs('w');
  builder.addGlobal(kWasmStringViewWtf8, true, false).exportAs('x');
  builder.addGlobal(kWasmStringViewWtf16, true, false).exportAs('y');
  builder.addGlobal(kWasmStringViewIter, true, false).exportAs('z');

  builder.addFunction("get_stringref", kSig_w_v)
    .exportFunc()
    .addBody([
      kExprGlobalGet, 0,
    ]);
  builder.addFunction("set_stringref", kSig_v_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprGlobalSet, 0
    ]);

  builder.addFunction("get_stringview_wtf8", kSig_x_v)
    .exportFunc()
    .addBody([
      kExprGlobalGet, 1,
    ]);
  builder.addFunction("get_stringview_wtf16", kSig_y_v)
    .exportFunc()
    .addBody([
      kExprGlobalGet, 2,
    ]);
  builder.addFunction("get_stringview_iter", kSig_z_v)
    .exportFunc()
    .addBody([
      kExprGlobalGet, 3,
    ]);

  let instance = builder.instantiate()

  assertEquals(null, instance.exports.get_stringref());
  instance.exports.set_stringref('foo');
  assertEquals('foo', instance.exports.get_stringref());

  assertEquals('foo', instance.exports.w.value);
  instance.exports.w.value = 'bar';
  assertEquals('bar', instance.exports.w.value);
  assertEquals('bar', instance.exports.get_stringref());

  assertThrows(()=>instance.exports.get_stringview_wtf8(),
               TypeError, "type incompatibility when transforming from/to JS");
  assertThrows(()=>instance.exports.get_stringview_wtf16(),
               TypeError, "type incompatibility when transforming from/to JS");
  assertThrows(()=>instance.exports.get_stringview_iter(),
               TypeError, "type incompatibility when transforming from/to JS");

  let unsupportedGlobalMessage = (mode, type) => {
    return `${mode} WebAssembly.Global.value: ${type} has no JS representation`;
  }
  for (let [global, type] of [[instance.exports.x, 'stringview_wtf8'],
                              [instance.exports.y, 'stringview_wtf16'],
                              [instance.exports.z, 'stringview_iter']]) {
    assertThrows(()=>global.value, TypeError,
                 unsupportedGlobalMessage('get', type));
    assertThrows(()=>{global.value = null}, TypeError,
                 unsupportedGlobalMessage('set', type));
  }
})();

(function TestImportedGlobals() {
  for (let type of ['stringview_wtf8', 'stringview_wtf16',
                    'stringview_iter']) {
    let msg = "WebAssembly.Global(): Descriptor property 'value' must be" +
        " a WebAssembly type";

    assertThrows(()=>new WebAssembly.Global({ mutable: true, value: type }),
                 TypeError, msg);
    assertThrows(()=>new WebAssembly.Global({ mutable: true, value: type },
                                            null),
                 TypeError, msg);
  }

  // String with default initializer.
  // TODO(12868): Is this the intended behavior?
  let null_str = new WebAssembly.Global({ value: 'stringref' });
  assertEquals(null, null_str.value);

  let kSig_w_v = makeSig([], [kWasmStringRef]);
  let kSig_v_w = makeSig([kWasmStringRef], []);
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal('env', 'w', kWasmStringRef, true)
  builder.addFunction("get_stringref", kSig_w_v)
    .exportFunc()
    .addBody([
      kExprGlobalGet, 0,
    ]);
  builder.addFunction("set_stringref", kSig_v_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprGlobalSet, 0
    ]);

  let w = new WebAssembly.Global({ mutable: true, value: 'stringref' },
                                 null);
  let instance = builder.instantiate({env: {w: w}})

  assertEquals(null, instance.exports.get_stringref());
  instance.exports.set_stringref('foo');
  assertEquals('foo', instance.exports.get_stringref());

  assertEquals('foo', w.value);
  w.value = 'bar';
  assertEquals('bar', w.value);
  assertEquals('bar', instance.exports.get_stringref());
})();

(function TestDefinedTables() {
  let kSig_w_v = makeSig([], [kWasmStringRef]);
  let kSig_v_w = makeSig([kWasmStringRef], []);
  let kSig_x_v = makeSig([], [kWasmStringViewWtf8]);
  let kSig_y_v = makeSig([], [kWasmStringViewWtf16]);
  let kSig_z_v = makeSig([], [kWasmStringViewIter]);
  let builder = new WasmModuleBuilder();

  builder.addTable(kWasmStringRef, 1).exportAs('w');
  builder.addTable(kWasmStringViewWtf8, 1).exportAs('x');
  builder.addTable(kWasmStringViewWtf16, 1).exportAs('y');
  builder.addTable(kWasmStringViewIter, 1).exportAs('z');

  builder.addFunction("get_stringref", kSig_w_v)
    .exportFunc()
    .addBody([
      kExprI32Const, 0,
      kExprTableGet, 0,
    ]);
  builder.addFunction("set_stringref", kSig_v_w)
    .exportFunc()
    .addBody([
      kExprI32Const, 0,
      kExprLocalGet, 0,
      kExprTableSet, 0,
    ]);

  builder.addFunction("get_stringview_wtf8", kSig_x_v)
    .exportFunc()
    .addBody([
      kExprI32Const, 0,
      kExprTableGet, 1,
    ]);
  builder.addFunction("get_stringview_wtf16", kSig_y_v)
    .exportFunc()
    .addBody([
      kExprI32Const, 0,
      kExprTableGet, 2,
    ]);
  builder.addFunction("get_stringview_iter", kSig_z_v)
    .exportFunc()
    .addBody([
      kExprI32Const, 0,
      kExprTableGet, 3,
    ]);

  let instance = builder.instantiate()

  assertEquals(null, instance.exports.get_stringref());
  instance.exports.set_stringref('foo');
  assertEquals('foo', instance.exports.get_stringref());

  assertEquals('foo', instance.exports.w.get(0));
  instance.exports.w.set(0, 'bar');
  assertEquals('bar', instance.exports.w.get(0));
  assertEquals('bar', instance.exports.get_stringref());

  assertThrows(()=>instance.exports.get_stringview_wtf8(),
               TypeError, "type incompatibility when transforming from/to JS");
  assertThrows(()=>instance.exports.get_stringview_wtf16(),
               TypeError, "type incompatibility when transforming from/to JS");
  assertThrows(()=>instance.exports.get_stringview_iter(),
               TypeError, "type incompatibility when transforming from/to JS");

  for (let [table, type] of [[instance.exports.x, 'stringview_wtf8'],
                             [instance.exports.y, 'stringview_wtf16'],
                             [instance.exports.z, 'stringview_iter']]) {
    let unsupportedGetMessage =
        `WebAssembly.Table.get(): ${type} has no JS representation`;
    let unsupportedSetMessage =
        'WebAssembly.Table.set(): Argument 1 is invalid for table: '
        + `${type} has no JS representation`;
    assertThrows(()=>table.get(0), TypeError, unsupportedGetMessage);
    assertThrows(()=>{table.set(0, null);}, TypeError, unsupportedSetMessage);
  }
})();

(function TestImportedTables() {
  for (let type of ['stringview_wtf8', 'stringview_wtf16',
                    'stringview_iter']) {
    let msg = "WebAssembly.Table(): Descriptor property 'element' must be" +
        " a WebAssembly reference type";

    assertThrows(()=>new WebAssembly.Table({ element: type, initial: 1 }),
                 TypeError, msg);
    assertThrows(()=>new WebAssembly.Table({ element: type, initial: 1 },
                                            null),
                 TypeError, msg);
  }

  assertThrows(()=>new WebAssembly.Table({ element: 'stringref', initial: 1 }),
               TypeError,
               "WebAssembly.Table(): " +
               "Missing initial value when creating stringref table");

  let kSig_w_v = makeSig([], [kWasmStringRef]);
  let kSig_v_w = makeSig([kWasmStringRef], []);
  let builder = new WasmModuleBuilder();
  builder.addImportedTable('env', 't', 0, undefined, kWasmStringRef);
  builder.addFunction("get_stringref", kSig_w_v)
    .exportFunc()
    .addBody([
      kExprI32Const, 0,
      kExprTableGet, 0,
    ]);
  builder.addFunction("set_stringref", kSig_v_w)
    .exportFunc()
    .addBody([
      kExprI32Const, 0,
      kExprLocalGet, 0,
      kExprTableSet, 0,
    ]);

  let t = new WebAssembly.Table({ element: 'stringref', initial: 1 },
                                 null);
  let instance = builder.instantiate({env: {t: t}})

  assertEquals(null, instance.exports.get_stringref());
  instance.exports.set_stringref('foo');
  assertEquals('foo', instance.exports.get_stringref());

  assertEquals('foo', t.get(0));
  t.set(0, 'bar');
  assertEquals('bar', t.get(0));
  assertEquals('bar', instance.exports.get_stringref());
})();
