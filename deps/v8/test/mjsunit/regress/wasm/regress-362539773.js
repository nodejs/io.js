// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-wrapper-tiering-budget=1
// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');
const builder = new WasmModuleBuilder();
const _type = builder.addType(kSig_v_v);
const _import = builder.addImport('m', 'foo', _type);
builder.addExportOfKind(_type, builder, _import);
builder.addFunction('main', _type).addBody([]).exportFunc();
const func = new WebAssembly.Function(
  { parameters: [], results: [] },
  () => {});
const instance = builder.instantiate({ 'm': { 'foo': func } });
instance.exports.main();
