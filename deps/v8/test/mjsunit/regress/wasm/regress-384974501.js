// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --nowasm-generic-wrapper --wasm-staging

let func = new WebAssembly.Function(
    {parameters: [], results: []}, () => {});
let table = new WebAssembly.Table({element: 'anyfunc', initial: 2});
table.set(0, func);
table.grow(1, func);

// Same thing with a function that can't use the generic wrapper because its
// signature isn't JS-compatible.
let func2 = new WebAssembly.Function(
    {parameters: ['v128'], results: []}, (_) => {});
table.set(1, func2);
table.grow(1, func2);
