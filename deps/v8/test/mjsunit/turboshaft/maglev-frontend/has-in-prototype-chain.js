// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function A() {}
class B extends A {}
class C {}

function is_a(o) {
  return o instanceof A;
}

%PrepareFunctionForOptimization(is_a);
assertEquals(true, is_a(new B()));
assertEquals(true, is_a(new A()));
assertEquals(false, is_a(new C()));
%OptimizeFunctionOnNextCall(is_a);
assertEquals(true, is_a(new B()));
assertEquals(true, is_a(new A()));
assertEquals(false, is_a(new C()));
assertOptimized(is_a);

// Changing manually the prototype of an object.
let b = new B();
assertEquals(true, is_a(b));
Object.setPrototypeOf(b, {});
assertEquals(false, is_a(b));
assertOptimized(is_a);
