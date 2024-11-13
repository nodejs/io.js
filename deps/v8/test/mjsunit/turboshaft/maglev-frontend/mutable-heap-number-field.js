// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function f(a, b) {
  // We do 2 transitioning stores that transition to a double field. The 2
  // HeapNumbers that will get created should not be GVNed.
  a.z = 42.54;
  b.z = 42.54;
}

let o1 = { x : 15 };
let o2 = { x : 15 };
let o3 = { x : 15 };
let o4 = { x : 15 };

// Adding a property to each object to ensure that the properties array has an
// empty slot (otherwise, the transitioning stores of `f` would need to realloc,
// and this isn't currently optimized in Maglev).
o1.y = 12;
o2.y = 12;
o3.y = 12;
o4.y = 12;

%PrepareFunctionForOptimization(f);
f(o1, o2);

%OptimizeFunctionOnNextCall(f);
f(o3, o4);

// Changing o3.z should not change o4.z.
o3.z += 1;

assertNotEquals(o3, o4);

assertEquals(o3, { x : 15, y : 12, z : 43.54});
assertEquals(o4, { x : 15, y : 12, z : 42.54});
