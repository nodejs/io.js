// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
const typeId = builder.addType(makeSig([kWasmS128], []));
const importId = builder.addImport('mod', 'foo', typeId);
builder.addDeclarativeElementSegment([importId]);

builder.addFunction('main', kSig_v_v)
  .addBody([
    ... wasmS128Const(0, 0),
    kExprRefFunc, importId,
    kExprCallRef, typeId,
]).exportFunc();
const instance = builder.instantiate({mod: {foo: assertUnreachable}});
assertThrows(instance.exports.main, TypeError);
