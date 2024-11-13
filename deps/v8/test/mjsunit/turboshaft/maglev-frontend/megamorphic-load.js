// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function load(o) {
  return o.x;
}

let o1 = { x : 42 };
let o2 = { a : {}, x : 2.5 };
let o3 = 42;
let o4 = { b : 42, c: {}, x : 5.35 };
let o5 = { u : 14, c : 2.28, d: 4.2, x : 5 };

%PrepareFunctionForOptimization(load);
assertEquals(42, load(o1));
assertEquals(2.5, load(o2));
assertEquals(undefined, load(o3));
assertEquals(5.35, load(o4));
assertEquals(5, load(o5));
%OptimizeFunctionOnNextCall(load);
assertEquals(42, load(o1));
assertEquals(2.5, load(o2));
assertEquals(undefined, load(o3));
assertEquals(5.35, load(o4));
assertEquals(5, load(o5));
assertOptimized(load);
