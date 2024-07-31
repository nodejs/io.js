// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --multi-mapped-mock-allocator

const kSize = 4294967296;
// Skip this test on 32-bit platforms.
if (%ArrayBufferMaxByteLength() >= kSize) {
  const array = new Uint8Array(kSize);

  function f() {
    let result = array["4294967295"];
    assertEquals(0, result);
  }

  function g() {
    array["4294967295"] = 1;
  }

  %PrepareFunctionForOptimization(f);
  for (var i = 0; i < 3; i++) f();
  %OptimizeFunctionOnNextCall(f);
  f();

  %PrepareFunctionForOptimization(g);
  for (var i = 0; i < 3; i++) g();
  %OptimizeFunctionOnNextCall(g);
  g();
}
