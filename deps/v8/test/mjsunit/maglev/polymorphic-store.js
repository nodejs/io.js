// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

function f(o, newValue) {
  o.x = newValue;
}

let o1 = { x : '' };
let o2 = { a: 0, x : '' };
let o3 = { a: 0, x : '' };

%PrepareFunctionForOptimization(f);
f(o1, 0);
f(o2, 0);

%OptimizeMaglevOnNextCall(f);
f(o1, 1);
f(o3, 2);
assertEquals(1, o1.x);
assertEquals(2, o3.x);

assertTrue(isMaglevved(f));

let o4 = {a: 0, b: 0, c: 0, x: 0};
f(o4, 3);
assertFalse(isMaglevved(f));
assertEquals(3, o4.x);
