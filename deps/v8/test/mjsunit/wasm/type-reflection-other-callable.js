// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --wasm-wrapper-tiering-budget=1

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
let sig0 = builder.addType(kSig_v_v);

builder.addFunction("test", makeSig([wasmRefType(sig0)], []))
  .exportFunc()
  .addBody([
    kExprLocalGet, 0,
    kExprCallRef, sig0,
  ]);

let instance = builder.instantiate();

let tier_up = new WebAssembly.Function(
    {parameters: [], results: []}, (() => {}).bind(null));

// Bug #1: make sure we compile a wrapper that doesn't crash when called.
instance.exports.test(tier_up);
instance.exports.test(tier_up);

// Bug #2: make sure we don't retrieve the wrong wrapper from the cache.
let prepare_wrapper =
    new WebAssembly.Function({parameters: [], results: []}, () => 42);
instance.exports.test(prepare_wrapper);
instance.exports.test(prepare_wrapper);
let use_cached_wrapper = new WebAssembly.Function(
    {parameters: [], results: []}, (() => {}).bind(null));
instance.exports.test(use_cached_wrapper);
instance.exports.test(use_cached_wrapper);
