// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function to_string(n) {
  return String(n);
}

%PrepareFunctionForOptimization(to_string);
assertEquals(String(453), to_string(453));
%OptimizeFunctionOnNextCall(to_string);
assertEquals(String(453), to_string(453));
assertEquals("Symbol(abc)", to_string(Symbol("abc")));
assertOptimized(to_string);

let called = false;
let o1 = { toString: function() { called = true; return 42; } };
assertEquals("42", to_string(o1));
assertTrue(called);
assertOptimized(to_string);
