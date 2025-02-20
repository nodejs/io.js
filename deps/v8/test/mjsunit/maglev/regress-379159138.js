// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function foo(v) {
  let a = 0;
  let b = 0;
  for (let i = 0; i < 10; i++) {
    a += b;
    b = v[i];
  }
  return b;
}

let values = [0, 0, 0, 0, 0, 0, 0, 0, 8.8,,];
%PrepareFunctionForOptimization(foo);
assertEquals(undefined, foo(new Int8Array()));
assertEquals(undefined, foo(values));
%OptimizeMaglevOnNextCall(foo);
assertEquals(undefined, foo(values));
