// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-staging
// Flags: --wasm-wrapper-tiering-budget=1

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const builder = new WasmModuleBuilder();
const impIndex = builder.addImport('m', 'foo', kSig_v_v);
const table = builder.addTable(kWasmAnyFunc, 10).exportAs('table').index;
builder.addActiveElementSegment(table, wasmI32Const(0), [impIndex]);
const reexport =
    builder.instantiate({'m': {'foo': () => console.log('Hello World')}})
        .exports.table.get(0);

function toBeOptimized() {
  reexport();
}

%PrepareFunctionForOptimization(toBeOptimized);
toBeOptimized();
toBeOptimized();
toBeOptimized();

%OptimizeFunctionOnNextCall(toBeOptimized);
toBeOptimized();
