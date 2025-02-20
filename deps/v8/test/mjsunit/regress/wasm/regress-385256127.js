// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

const f1 = new WebAssembly.Function(
    { parameters: [], results: []}, (x) => 123);
const f2 = new WebAssembly.Function(
    { parameters: ["i32"], results: ["i32"]}, f1);

const table = new WebAssembly.Table({
  element: "anyfunc",
  initial: 2,
});
table.set(1, f2);

assertEquals(0, f2(42));
