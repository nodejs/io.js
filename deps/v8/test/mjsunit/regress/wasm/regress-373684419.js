// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();
let f = builder.addFunction('f', kSig_v_v).addBody([]);
builder.addStart(f.index);
builder.asyncInstantiate();
d8.terminate();
