// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --maglev --allow-natives-syntax
// Flags: --maglev-skip-migration-check-for-polymorphic-access

// Like no-deopt-deprecated-map.js, but we pass a flag to skip the map
// migration.

// This object will later have a deprecated map.
let o1 = {y: 0, a: 1};

// These 2 objects will always have the same map. We use two
// of them to make sure we're not embedding the object itself
// as a constant.
let o2_1 = {y: 0, a: 1};
let o2_2 = {y: 0, a: 1};

// An unrelated object just to make the IC polymorphic.
let o3 = {x: 0, y: 0, a: 1};

// Make o1's map deprecated. o2_1 and o2_2 have the same map.
o2_1.a = 3.1415;
o2_2.a = 4.12;

function foo(o) {
  o.y = 2; // Polymorpic property store.
}
%PrepareFunctionForOptimization(foo);

// Collect the feedback.
foo(o2_1);
foo(o2_2);
foo(o3);  // Make it polymorphic.
%OptimizeMaglevOnNextCall(foo);
foo(o2_1);

assertTrue(isMaglevved(foo));

// Calling foo with a deprecated map will deopt, since we don't try to migrate
// the object.
foo(o1);

assertFalse(isMaglevved(foo));
