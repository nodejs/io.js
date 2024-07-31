// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

var builder = new WasmModuleBuilder();

var sig_index = builder.addType(
    {params: [wasmRefType(kWasmStructRef)], results: []});

builder.addFunction('main', sig_index).addBody([]).exportFunc();

var instance = builder.instantiate();

assertThrows(() => instance.exports.main(undefined), TypeError);
assertThrows(() => instance.exports.main(null), TypeError);
