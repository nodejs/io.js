// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-to-js-generic-wrapper --wasm-staging --experimental-wasm-type-reflection
// Flags: --wasm-wrapper-tiering-budget=1 --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const kNormal = 0;
const kGetAndCall = 1;

function GetJSFunction(numParams) {
  return () => 15;
}

function GetWasmJSFunction(numParams) {
  return new WebAssembly.Function(
      {parameters: new Array(numParams).fill('i32'), results: []},
      () => 12);
}

function GetReExportedJSFunction(numParams) {
  const paramsTypes = new Array(numParams).fill(kWasmI32);
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  const impIndex = builder.addImport('m', 'foo', sig);
  builder.addExport("foo", impIndex);
  return builder.instantiate(
      {'m': {'foo': GetJSFunction(numParams)}}).exports.foo;
}

function GetReExportedWasmJSFunction(numParams) {
  const paramsTypes = new Array(numParams).fill(kWasmI32);
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  const impIndex = builder.addImport('m', 'foo', sig);
  builder.addExport("foo", impIndex);
  return builder.instantiate(
      {'m': {'foo': GetWasmJSFunction(numParams)}}).exports.foo;
}

function AssertWrapperTierup(instance) {
  if (instance.exports.get) {
    assertTrue(%HasUnoptimizedWasmToJSWrapper(instance.exports.get()));
    instance.exports.main();
    assertTrue(!%HasUnoptimizedWasmToJSWrapper(instance.exports.get()));
    instance.exports.main();
  } else {
    const countBefore = %CountUnoptimizedWasmToJSWrapper(instance);
    instance.exports.main();
    const countAfter = %CountUnoptimizedWasmToJSWrapper(instance);
    assertTrue(countBefore > countAfter);
    instance.exports.main();
  }
}

function TestImportedFunction(numParams, jsFunc, mode) {
  if (mode != kNormal) return;
  const paramsTypes = new Array(numParams).fill(kWasmI32);
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  const impIndex = builder.addImport('m', 'foo', sig);

  let body = [];
  for (let i = 0; i < numParams; ++i) {
    body.push(kExprLocalGet, i);
  }
  body.push(kExprCallFunction, impIndex);

  builder.addFunction('main', sig)
    .addBody(body)
    .exportFunc();

  const instance = builder.instantiate({ 'm': { 'foo': jsFunc } });
  AssertWrapperTierup(instance);
}

function TestIndirectElementSection(numParams, jsFunc, mode) {
  const paramsTypes = new Array(numParams).fill(kWasmI32);
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  const sigId = builder.addType(sig);
  const impIndex = builder.addImport('m', 'foo', sigId);

  const table = builder.addTable(kWasmAnyFunc, 10).index;
  builder.addActiveElementSegment(table, wasmI32Const(0), [impIndex]);

  let body = [];
  for (let i = 0; i < numParams; ++i) {
    body.push(kExprLocalGet, i);
  }
  if (mode == kNormal) {
    body.push(kExprI32Const, 0, kExprCallIndirect, sigId, table);
  } else {
    body.push(kExprI32Const, 0, kExprTableGet, table,
      kGCPrefix, kExprRefCast, sigId, kExprCallRef, sigId);

    builder.addFunction('get', kSig_a_v)
        .addBody([kExprI32Const, 0, kExprTableGet, table])
        .exportFunc();
  }

  builder.addFunction('main', sigId)
    .addBody(body)
    .exportFunc();

  const instance = builder.instantiate({ 'm': { 'foo': jsFunc } });
  AssertWrapperTierup(instance);
}

function TestIndirectJSTableImport(numParams, jsFunc, mode) {
  if (!(jsFunc instanceof WebAssembly.Function)) {
    return;
  }

  const paramsTypes = new Array(numParams).fill(kWasmI32);
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  const sigId = builder.addType(sig);
  const impIndex = builder.addImportedTable('m', 'table', 10, 10, kWasmAnyFunc);

  let body = [];
  for (let i = 0; i < numParams; ++i) {
    body.push(kExprLocalGet, i);
  }
  if (mode == kNormal) {
    body.push(kExprI32Const, 0, kExprCallIndirect, sigId, impIndex);
  } else {
    body.push(kExprI32Const, 0, kExprTableGet, impIndex,
      kGCPrefix, kExprRefCast, sigId, kExprCallRef, sigId);

    builder.addFunction('get', kSig_a_v)
        .addBody([kExprI32Const, 0, kExprTableGet, impIndex])
        .exportFunc();
  }

  builder.addFunction('main', sigId)
    .addBody(body)
    .exportFunc();

  const table = new WebAssembly.Table({
    element: "anyfunc",
    initial: 10,
    maximum: 10
  });

  table.set(0, jsFunc);

  const instance = builder.instantiate({ 'm': { 'table': table } });
  AssertWrapperTierup(instance);
}

function TestIndirectWasmTableImport(numParams, jsFunc, mode) {
  const paramsTypes = new Array(numParams).fill(kWasmI32);
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  const sigId = builder.addType(sig);
  const impIndex = builder.addImportedTable('m', 'table', 10, 10, kWasmAnyFunc);

  let body = [];
  for (let i = 0; i < numParams; ++i) {
    body.push(kExprLocalGet, i);
  }

  if (mode == kNormal) {
    body.push(kExprI32Const, 0, kExprCallIndirect, sigId, impIndex);
  } else {
    body.push(kExprI32Const, 0, kExprTableGet, impIndex,
      kGCPrefix, kExprRefCast, sigId, kExprCallRef, sigId);

    builder.addFunction('get', kSig_a_v)
        .addBody([kExprI32Const, 0, kExprTableGet, impIndex])
        .exportFunc();
  }

  builder.addFunction('main', sigId)
    .addBody(body)
    .exportFunc();

  let table;
  {
    const table_module_builder = new WasmModuleBuilder();
    const sigId = table_module_builder.addType(sig);
    const impIndex = table_module_builder.addImport('m', 'foo', sigId);

    const tableId =
        table_module_builder.addTable(kWasmAnyFunc, 10, 10).exportAs('table').index;
    table_module_builder.addActiveElementSegment(
        tableId, wasmI32Const(0), [impIndex]);

    table =
        table_module_builder.instantiate({'m': {'foo': jsFunc}}).exports.table;
  }

  const instance = builder.instantiate({ 'm': { 'table': table } });
  AssertWrapperTierup(instance);
}

function TestIndirectTableSetInJS(numParams, jsFunc, mode) {
  if (!(jsFunc instanceof WebAssembly.Function)) {
    return;
  }

  const paramsTypes = new Array(numParams).fill(kWasmI32);
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  const sigId = builder.addType(sig);

  const table = builder.addTable(kWasmAnyFunc, 10).exportAs('table').index;

  let body = [];
  for (let i = 0; i < numParams; ++i) {
    body.push(kExprLocalGet, i);
  }
  if (mode == kNormal) {
    body.push(kExprI32Const, 0, kExprCallIndirect, sigId, table);
  } else {
    body.push(kExprI32Const, 0, kExprTableGet, table,
      kGCPrefix, kExprRefCast, sigId, kExprCallRef, sigId);

    builder.addFunction('get', kSig_a_v)
        .addBody([kExprI32Const, 0, kExprTableGet, table])
        .exportFunc();
  }

  builder.addFunction('main', sigId)
    .addBody(body)
    .exportFunc();

  const instance = builder.instantiate();

  instance.exports.table.set(0, jsFunc);
  AssertWrapperTierup(instance);
}

function TestIndirectTableSetFromParam(numParams, jsFunc, mode) {
  if (!(jsFunc instanceof WebAssembly.Function)) {
    return;
  }

  const paramsTypes = new Array(numParams).fill(kWasmI32);
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  const sigId = builder.addType(sig);

  const table = builder.addTable(kWasmAnyFunc, 10).index;

  let body = [];
  for (let i = 0; i < numParams; ++i) {
    body.push(kExprLocalGet, i);
  }
  if (mode == kNormal) {
    body.push(kExprI32Const, 0, kExprCallIndirect, sigId, table);
  } else {
    body.push(kExprI32Const, 0, kExprTableGet, table,
      kGCPrefix, kExprRefCast, sigId, kExprCallRef, sigId);

    builder.addFunction('get', kSig_a_v)
        .addBody([kExprI32Const, 0, kExprTableGet, table])
        .exportFunc();
  }

  builder.addFunction('main', sigId)
    .addBody(body)
    .exportFunc();
  builder.addFunction('set', kSig_v_a)
      .addBody([kExprI32Const, 0, kExprLocalGet, 0, kExprTableSet, table])
      .exportFunc();

  const instance = builder.instantiate();

  instance.exports.set(jsFunc);
  AssertWrapperTierup(instance);
}

function TestIndirectTableSetFromTableGet(numParams, jsFunc, mode) {
  const paramsTypes = new Array(numParams).fill(kWasmI32);
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  const sigId = builder.addType(sig);
  const impIndex = builder.addImport('m', 'foo', sigId);

  const table = builder.addTable(kWasmAnyFunc, 10).index;
  builder.addActiveElementSegment(
        table, wasmI32Const(1), [impIndex]);

  let body = [];
  for (let i = 0; i < numParams; ++i) {
    body.push(kExprLocalGet, i);
  }
  if (mode == kNormal) {
    body.push(kExprI32Const, 0, kExprCallIndirect, sigId, table);
  } else {
    body.push(kExprI32Const, 0, kExprTableGet, table,
      kGCPrefix, kExprRefCast, sigId, kExprCallRef, sigId);

    builder.addFunction('get', kSig_a_v)
        .addBody([kExprI32Const, 0, kExprTableGet, table])
        .exportFunc();
  }

  builder.addFunction('main', sigId)
    .addBody(body)
    .exportFunc();
  builder.addFunction('set', kSig_v_v)
      .addBody([
        kExprI32Const, 0, // index of table.set
        ...[kExprI32Const, 1, kExprTableGet, table],
        kExprTableSet, table
      ])
      .exportFunc();

  const instance = builder.instantiate({ 'm': { 'foo': jsFunc } });

  instance.exports.set();
  AssertWrapperTierup(instance);
}

function TestIndirectTableSetFromFuncRef(numParams, jsFunc, mode) {
  const paramsTypes = new Array(numParams).fill(kWasmI32);
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  const sigId = builder.addType(sig);
  const impIndex = builder.addImport('m', 'foo', sigId);
  builder.addDeclarativeElementSegment([impIndex]);

  const table = builder.addTable(kWasmAnyFunc, 10).index;

  let body = [];
  for (let i = 0; i < numParams; ++i) {
    body.push(kExprLocalGet, i);
  }
  if (mode == kNormal) {
    body.push(kExprI32Const, 0, kExprCallIndirect, sigId, table);
  } else {
    body.push(kExprI32Const, 0, kExprTableGet, table,
      kGCPrefix, kExprRefCast, sigId, kExprCallRef, sigId);

    builder.addFunction('get', kSig_a_v)
        .addBody([kExprI32Const, 0, kExprTableGet, table])
        .exportFunc();
  }

  builder.addFunction('main', sigId)
    .addBody(body)
    .exportFunc();
  builder.addFunction('set', kSig_v_v)
      .addBody([
        kExprI32Const, 0,  // index of table.set
        kExprRefFunc, impIndex, kExprTableSet, table
      ])
      .exportFunc();

  const instance = builder.instantiate({ 'm': { 'foo': jsFunc } });

  instance.exports.set();
  AssertWrapperTierup(instance);
}

function TestIndirectTableSetFromParamInOtherInstance(numParams, jsFunc, mode) {
  if (!(jsFunc instanceof WebAssembly.Function)) {
    return;
  }

  const paramsTypes = new Array(numParams).fill(kWasmI32);
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  const sigId = builder.addType(sig);

  let table = builder.addTable(kWasmAnyFunc, 10, 10).exportAs('table').index;

  let body = [];
  for (let i = 0; i < numParams; ++i) {
    body.push(kExprLocalGet, i);
  }
  if (mode == kNormal) {
    body.push(kExprI32Const, 0, kExprCallIndirect, sigId, table);
  } else {
    body.push(kExprI32Const, 0, kExprTableGet, table,
      kGCPrefix, kExprRefCast, sigId, kExprCallRef, sigId);

    builder.addFunction('get', kSig_a_v)
        .addBody([kExprI32Const, 0, kExprTableGet, table])
        .exportFunc();
  }

  builder.addFunction('main', sigId)
    .addBody(body)
    .exportFunc();

  const setBuilder = new WasmModuleBuilder();
  table = setBuilder.addImportedTable('m', 'table', 10, 10, kWasmAnyFunc);
  setBuilder.addFunction('set', kSig_v_a)
      .addBody([kExprI32Const, 0, kExprLocalGet, 0, kExprTableSet, table])
      .exportFunc();

  const instance = builder.instantiate();

  const setInstance =
      setBuilder.instantiate({'m': {'table': instance.exports.table}});
  setInstance.exports.set(jsFunc);
  AssertWrapperTierup(instance);
}

function TestIndirectTableSetFromTableGetInOtherInstance(
    numParams, jsFunc, mode) {
  const paramsTypes = new Array(numParams).fill(kWasmI32);
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  const sigId = builder.addType(sig);
  const impIndex = builder.addImport('m', 'foo', sigId);

  let table = builder.addTable(kWasmAnyFunc, 10, 10).exportAs('table').index;
  builder.addActiveElementSegment(
        table, wasmI32Const(1), [impIndex]);

  let body = [];
  for (let i = 0; i < numParams; ++i) {
    body.push(kExprLocalGet, i);
  }
  if (mode == kNormal) {
    body.push(kExprI32Const, 0, kExprCallIndirect, sigId, table);
  } else {
    body.push(kExprI32Const, 0, kExprTableGet, table,
      kGCPrefix, kExprRefCast, sigId, kExprCallRef, sigId);

    builder.addFunction('get', kSig_a_v)
        .addBody([kExprI32Const, 0, kExprTableGet, table])
        .exportFunc();
  }

  builder.addFunction('main', sigId)
    .addBody(body)
    .exportFunc();

  const setBuilder = new WasmModuleBuilder();
  table = setBuilder.addImportedTable('m', 'table', 10, 10, kWasmAnyFunc);
  setBuilder.addFunction('set', kSig_v_v)
      .addBody([
        kExprI32Const, 0, // index of table.set
        ...[kExprI32Const, 1, kExprTableGet, table],
        kExprTableSet, table
      ])
      .exportFunc();

  const instance = builder.instantiate({ 'm': { 'foo': jsFunc } });

  const setInstance =
      setBuilder.instantiate({'m': {'table': instance.exports.table}});
  setInstance.exports.set(jsFunc);
  AssertWrapperTierup(instance);
}

function TestIndirectTableSetFromTableGetInOtherInstance2(
    numParams, jsFunc, mode) {
  const paramsTypes = new Array(numParams).fill(kWasmI32);
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  let sigId = builder.addType(sig);

  let table = builder.addTable(kWasmAnyFunc, 10, 10).exportAs('table').index;

  let body = [];
  for (let i = 0; i < numParams; ++i) {
    body.push(kExprLocalGet, i);
  }
  if (mode == kNormal) {
    body.push(kExprI32Const, 0, kExprCallIndirect, sigId, table);
  } else {
    body.push(kExprI32Const, 0, kExprTableGet, table,
      kGCPrefix, kExprRefCast, sigId, kExprCallRef, sigId);

    builder.addFunction('get', kSig_a_v)
        .addBody([kExprI32Const, 0, kExprTableGet, table])
        .exportFunc();
  }

  builder.addFunction('main', sigId)
    .addBody(body)
    .exportFunc();

  const setBuilder = new WasmModuleBuilder();
  sigId = setBuilder.addType(sig);
  const impIndex = setBuilder.addImport('m', 'foo', sigId);
  table = setBuilder.addImportedTable('m', 'table', 10, 10, kWasmAnyFunc);
  setBuilder.addActiveElementSegment(
        table, wasmI32Const(1), [impIndex]);
  setBuilder.addFunction('set', kSig_v_v)
      .addBody([
        kExprI32Const, 0, // index of table.set
        ...[kExprI32Const, 1, kExprTableGet, table],
        kExprTableSet, table
      ])
      .exportFunc();

  const instance = builder.instantiate();

  const setInstance = setBuilder.instantiate(
      {'m': {'table': instance.exports.table, 'foo': jsFunc}});
  setInstance.exports.set(jsFunc);
  AssertWrapperTierup(instance);
}

function TestIndirectTableSetFromFuncRefInOtherInstance(numParams, jsFunc, mode) {
  const paramsTypes = new Array(numParams).fill(kWasmI32);
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  let sigId = builder.addType(sig);

  let table = builder.addTable(kWasmAnyFunc, 10, 10).exportAs('table').index;

  let body = [];
  for (let i = 0; i < numParams; ++i) {
    body.push(kExprLocalGet, i);
  }
  if (mode == kNormal) {
    body.push(kExprI32Const, 0, kExprCallIndirect, sigId, table);
  } else {
    body.push(kExprI32Const, 0, kExprTableGet, table,
      kGCPrefix, kExprRefCast, sigId, kExprCallRef, sigId);

    builder.addFunction('get', kSig_a_v)
        .addBody([kExprI32Const, 0, kExprTableGet, table])
        .exportFunc();
  }

  builder.addFunction('main', sigId)
    .addBody(body)
    .exportFunc();

  const setBuilder = new WasmModuleBuilder();
  sigId = setBuilder.addType(sig);
  const impIndex = setBuilder.addImport('m', 'foo', sigId);
  setBuilder.addDeclarativeElementSegment([impIndex]);
  table = setBuilder.addImportedTable('m', 'table', 10, 10, kWasmAnyFunc);
  setBuilder.addFunction('set', kSig_v_v)
      .addBody([
        kExprI32Const, 0,  // index of table.set
        kExprRefFunc, impIndex, kExprTableSet, table
      ])
      .exportFunc();

  const instance = builder.instantiate();
  const setInstance = setBuilder.instantiate(
      {'m': {'table': instance.exports.table, 'foo': jsFunc}});
  setInstance.exports.set(jsFunc);

  AssertWrapperTierup(instance);
}

function TestCallRefFromParam(numParams, jsFunc, mode) {
  if (mode != kNormal) return;
  if (!(jsFunc instanceof WebAssembly.Function)) {
    return;
  }

  const paramsTypes = new Array(numParams).fill(kWasmI32);
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  let sigId = builder.addType(sig);

  let body = [];
  for (let i = 1; i <= numParams; ++i) {
    body.push(kExprLocalGet, ...wasmUnsignedLeb(i));
  }
  body.push(kExprLocalGet, 0, kExprCallRef, sigId);

  const mainParams = [wasmRefNullType(sigId), ...paramsTypes];
  builder.addFunction('main', makeSig(mainParams, retTypes))
    .addBody(body)
    .exportFunc();
  const instance = builder.instantiate();

  assertTrue(%HasUnoptimizedWasmToJSWrapper(jsFunc));
  instance.exports.main(jsFunc);
  assertTrue(! %HasUnoptimizedWasmToJSWrapper(jsFunc));
  instance.exports.main(jsFunc);
}

function TestCallRefFromFuncRef(numParams, jsFunc, mode) {
  if (mode != kNormal) return;
  const paramsTypes = new Array(numParams).fill(kWasmI32);
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  const sigId = builder.addType(sig);
  const impIndex = builder.addImport('m', 'foo', sigId);
  builder.addDeclarativeElementSegment([impIndex]);

  let body = [];
  for (let i = 0; i < numParams; ++i) {
    body.push(kExprLocalGet, i);
  }
  body.push(kExprRefFunc, impIndex, kExprCallRef, sigId);

  builder.addFunction('main', sigId)
    .addBody(body)
    .exportFunc();

  builder.addFunction('get', kSig_a_v)
      .addBody([kExprRefFunc, impIndex])
      .exportFunc();

  const instance = builder.instantiate({ 'm': { 'foo': jsFunc } });

  AssertWrapperTierup(instance);
}

(function Test() {
  const kinds = [
    GetJSFunction, GetWasmJSFunction, GetReExportedJSFunction,
    GetReExportedWasmJSFunction
  ];

  const tests = [
    TestImportedFunction, TestIndirectElementSection, TestIndirectJSTableImport,
    TestIndirectWasmTableImport, TestIndirectTableSetInJS,
    TestIndirectTableSetFromParam, TestIndirectTableSetFromTableGet,
    TestIndirectTableSetFromFuncRef,
    TestIndirectTableSetFromParamInOtherInstance,
    TestIndirectTableSetFromTableGetInOtherInstance,
    TestIndirectTableSetFromTableGetInOtherInstance2,
    TestIndirectTableSetFromFuncRefInOtherInstance, TestCallRefFromParam,
    TestCallRefFromFuncRef
  ];

  const modes = [kNormal, kGetAndCall];

  let numParams = 0;
  for (const test of tests) {
    console.log('\n', test.name);
    for (const kind of kinds) {
      console.log('>', kind.name);
      for (const mode of modes) {
        console.log(
            mode == kNormal ? '  > CallIndirect' : '  > Table.get + ref.call');
        test(numParams, kind(numParams), mode);
        numParams++;
      }
    }
  }
})();
