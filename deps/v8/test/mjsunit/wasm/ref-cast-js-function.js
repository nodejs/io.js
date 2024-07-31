// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-type-reflection

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function RefCastWasmJSFunction() {
  const paramsTypes = [kWasmI32];
  const retTypes = [];
  const sig = makeSig(paramsTypes, retTypes);

  const builder = new WasmModuleBuilder();
  const sigId = builder.addType(sig);
  const impIndex = builder.addImport('m', 'foo', sigId);

  const table = builder.addTable(kWasmAnyFunc, 10).index;
  builder.addActiveElementSegment(table, wasmI32Const(0), [impIndex]);

  builder.addFunction('main', sigId)
    .addBody([
      kExprLocalGet, 0, kExprI32Const, 0, kExprTableGet, table, kGCPrefix,
      kExprRefCast, sigId, kExprCallRef, sigId
    ])
    .exportFunc();
  const jsFunc = new WebAssembly.Function(
    { parameters: ['i32'], results: [] },
    () => 12 );
  const instance = builder.instantiate({ 'm': { 'foo': jsFunc } });
  instance.exports.main(15);
})();
