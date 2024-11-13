// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function g(b) {
  if (b) {
    throw_because_function_not_defined();
  }
}
%NeverOptimizeFunction(g);

function A() {}
class B extends A {}
class C {}

function is_a(o, b) {
  let v = b;
  try {
    v = 13;
    g(b);
    v = 12;
    return o instanceof A;
  } catch (e) {
    return v;
  }
}

%PrepareFunctionForOptimization(is_a);
assertEquals(true, is_a(new B(), false));
assertEquals(13, is_a(new B(), true));
assertEquals(true, is_a(new A(), false));
assertEquals(13, is_a(new A(), true));
assertEquals(false, is_a(new C(), false));
assertEquals(13, is_a(new C(), true));
%OptimizeFunctionOnNextCall(is_a);
assertEquals(true, is_a(new B(), false));
assertEquals(13, is_a(new B(), true));
assertEquals(true, is_a(new A(), false));
assertEquals(13, is_a(new A(), true));
assertEquals(false, is_a(new C(), false));
assertEquals(13, is_a(new C(), true));
assertOptimized(is_a);

// Changing manually the prototype of an object.
let b = new B();
assertEquals(true, is_a(b, false));
assertEquals(13, is_a(b, true));
Object.setPrototypeOf(b, {});
assertEquals(false, is_a(b, false));
assertEquals(13, is_a(b, true));
assertOptimized(is_a);
