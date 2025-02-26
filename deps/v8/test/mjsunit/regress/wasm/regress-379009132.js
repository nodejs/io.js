// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-wrapper-tiering-budget=1

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
const type = builder.nextTypeIndex();
builder.addType(makeSig([], [wasmRefType(type)]));
const f = builder.addFunction('f', type);
f.addBody([kExprRefFunc, f.index]).exportFunc();
const instance = builder.instantiate();
instance.exports.f();
const func_ref = instance.exports.f();
func_ref.toString();
