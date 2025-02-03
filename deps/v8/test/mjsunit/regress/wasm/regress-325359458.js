// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
let struct = builder.addStruct([makeField(kWasmAnyRef, true)]);

let body = [kExprRefNull, struct];
for (let i = 0; i < 800; i++) {
  body = body.concat(...[
    kGCPrefix, kExprStructGet, struct, 0,
    kGCPrefix, kExprRefCastNull, struct,
  ]);
}
body = body.concat(...[kExprDrop]);
builder.addFunction("main", kSig_v_v).exportFunc().addBody(body);

let instance = builder.instantiate();
// Just check if we can compile without DCHECK failures.
assertThrows(() => instance.exports.main(), WebAssembly.RuntimeError,
             'dereferencing a null pointer');
