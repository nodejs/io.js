// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

g = function g(expected, found) {
  if (typeof a !== typeof found) return false;
  objectClass
};

function f1() {
  return f2.arguments[0];
}
function f2() {
  return f1();
}

function f3() {
  var v1 = {
    x: 2
  };
  var v2 = f2(v1);
  delete v2["x"];

  try {
    g(2, v1.x);
  } catch (e) {}

  v1.x = 3;
  try {
    v2.x = 42;
  } catch (e) {}
  return v1.x;
}

%PrepareFunctionForOptimization(f2);
%PrepareFunctionForOptimization(f3);
assertEquals(f3(), 42);
assertEquals(f3(), 42);
%OptimizeFunctionOnNextCall(f3);
assertEquals(f3(), 42);
