// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --wasm-wrapper-tiering-budget=1

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const func0 =
    new WebAssembly.Function({parameters: [], results: []}, function f1() {});

let builder = new WasmModuleBuilder();
let js_index = builder.addImport("m", "js", kSig_v_v);
builder.addExport("main", 0);

// This causes a wrapper for sig_v_v to get compiled.
let instance = builder.instantiate({ m: { js: function f2() {} } });
instance.exports.main();

const table = new WebAssembly.Table({ element: "anyfunc", initial: 2 });
// Since we have a compiled wrapper, this doesn't bother creating copied
// WasmImportDatas where the table could be set as call origin.
table.grow(8, func0);
// This shouldn't DCHECK that the old table's entries have the old table
// stored as their call origin.
table.grow(1);
