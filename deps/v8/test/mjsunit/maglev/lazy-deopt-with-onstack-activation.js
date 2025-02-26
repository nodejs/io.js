// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev --no-always-turbofan

var x = 1;
var do_change = {};
do_change = false;

function g() {
  if (do_change) {
    x = 2;
    return 40;
  }
  return 30;
}

function f() {
  return g() + x;
}

%PrepareFunctionForOptimization(f);
assertEquals(31, f());

%OptimizeMaglevOnNextCall(f);
assertEquals(31, f());
assertTrue(isMaglevved(f));

// Trigger a lazy deopt on the next g() call.
do_change = true;
assertEquals(42, f());
assertFalse(isMaglevved(f));
assertUnoptimized(f);
