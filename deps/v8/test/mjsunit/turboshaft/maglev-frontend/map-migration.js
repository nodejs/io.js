// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

function A() { this.x = 1 }
function B() { this.x = 1 }

function migrate_and_load(o) {
  return o.x;
}

let old_b1 = new B();
let old_b2 = new B();

old_b1.x = 42;
old_b2.x = 186;

%PrepareFunctionForOptimization(migrate_and_load);
assertEquals(1, migrate_and_load(new A()));
(new A()).x = 4.25; // Deprecate map already in feedback
(new B()).x = 4.25; // Deprecate old_b1/old_b2 map
assertEquals(42, migrate_and_load(old_b1));

%OptimizeFunctionOnNextCall(migrate_and_load);
assertEquals(42, migrate_and_load(old_b1));
assertOptimized(migrate_and_load);
// Calling function with deprecated map
assertEquals(186, migrate_and_load(old_b2));
assertOptimized(migrate_and_load);

// Triggering deopt
assertEquals("abc", (migrate_and_load({a: 42, x : "abc"})));
assertUnoptimized(migrate_and_load);
