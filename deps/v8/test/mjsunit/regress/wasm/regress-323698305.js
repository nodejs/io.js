// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --stress-compaction

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addImport("q", "imp", kSig_i_i);
builder.addExport("exp", 0);

function gc_wrapper() {
  gc();
}

gc_wrapper[Symbol.toPrimitive] = gc_wrapper;
const imp = { q: { imp: gc_wrapper } };

const instance = builder.instantiate(imp);
instance.exports.exp(gc_wrapper);
