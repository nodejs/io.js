// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --super-ic --turbofan
// Flags: --no-always-turbofan --deopt-every-n-times=0

// This file contains tests which are disabled for TurboProp. TurboProp deopts
// differently than TurboFan, so the assertions about when a function is
// deoptimized won't hold. In addition, TurboProp doesn't inline, so all
// tests that rely on inlining need to be in this file.

(function TestPropertyIsConstant() {
  // Test for a case where the property is a constant found in the lookup start
  // object.
  class A {}
  A.prototype.bar = "wrong value: A.prototype.bar";

  class B extends A {};
  B.prototype.bar = "correct value";

  class C extends B {
    foo() { return super.bar; }
  }
  C.prototype.bar = "wrong value: C.prototype.bar";
  %PrepareFunctionForOptimization(C.prototype.foo);

  let o = new C();
  o.bar = "wrong value: o.bar";

  // Fill in the feedback.
  r = o.foo();
  assertEquals("correct value", r);

  %OptimizeFunctionOnNextCall(C.prototype.foo);

  // Test the optimized function.
  r = o.foo();
  assertEquals("correct value", r);

  // Assert that the function was not deoptimized.
  assertOptimized(C.prototype.foo);

  // Change the property value.
  B.prototype.bar = "new value";

  // Assert that the function was deoptimized (dependency to the constant
  // value).
  // TODO(v8:11457) We don't support inlining JSLoadNamedFromSuper for
  // dictionary mode prototypes, yet. Therefore, if
  // v8_dict_property_const_tracking is enabled, the optimized code only
  // contains a call to the IC handler and doesn't get deopted.
  assertEquals(%IsDictPropertyConstTrackingEnabled(),
               isOptimized(C.prototype.foo));

  r = o.foo();
  assertEquals("new value", r);
})();

(function TestSuperpropertyAccessInlined() {
  class A {}
  A.prototype.bar = "wrong value: A.prototype.bar";

  class B extends A {}
  B.prototype.bar = "correct value";

  class C extends B {}

  let inline_this_is_being_interpreted;
  class D extends C {
    inline_this() {
      inline_this_is_being_interpreted = %IsBeingInterpreted();
      return super.bar;
    }
    foo() { return this.inline_this(); }
  }
  %PrepareFunctionForOptimization(D.prototype.inline_this);
  %PrepareFunctionForOptimization(D.prototype.foo);
  D.prototype.bar = "wrong value: D.prototype.bar";

  const o = new D();

  // Fill in the feedback.
  let r = o.foo();
  assertEquals("correct value", r);

  %OptimizeFunctionOnNextCall(D.prototype.foo);

  // Test the optimized function.
  r = o.foo();
  assertEquals("correct value", r);

  // Assert that the function was not deoptimized.
  assertOptimized(D.prototype.foo);

  // Assert that inline_this really got inlined.
  assertFalse(inline_this_is_being_interpreted);
})();

(function TestMegamorphicInlined() {
  class A {}
  A.prototype.bar = "wrong value: A.prototype.bar";

  class B extends A {}
  B.prototype.bar = "correct value";

  class C extends B {}

  let inline_this_is_being_interpreted;
  class D extends C {
    inline_this() {
      inline_this_is_being_interpreted = %IsBeingInterpreted();
      return super.bar;
    }
    foo() { return this.inline_this(); }
  }
  %PrepareFunctionForOptimization(D.prototype.inline_this);
  %PrepareFunctionForOptimization(D.prototype.foo);
  D.prototype.bar = "wrong value: D.prototype.bar";

  const o = new D();

  // Create objects which will act as the "home object's prototype" later.
  const prototypes = [{"a": 0}, {"b": 0}, {"c": 0}, {"d": 0}, {"e": 0},
  {"f": 0}, {"g": 0}, {"e": 0}];
  for (p of prototypes) {
    p.__proto__ = B.prototype;
  }

  // Fill in the feedback (megamorphic).
  for (p of prototypes) {
    D.prototype.__proto__ = p;
    const r = o.foo();
    assertEquals("correct value", r);
  }

  %OptimizeFunctionOnNextCall(D.prototype.foo);

  // Test the optimized function - don't change the home object's proto any
  // more.
  let r = o.foo();
  assertEquals("correct value", r);

  // Assert that the function was not deoptimized.
  assertOptimized(D.prototype.foo);

  // Assert that inline_this really got inlined.
  assertFalse(inline_this_is_being_interpreted);
})();

(function TestHomeObjectProtoIsGlobalThisGetterPropertyInlined() {
  class A {}

  let inline_this_is_being_interpreted;
  class B extends A {
    inline_this() {
      inline_this_is_being_interpreted = %IsBeingInterpreted();
      return super.bar;
    }
    foo() { return this.inline_this(); }
  }
  B.prototype.__proto__ = globalThis;
  Object.defineProperty(globalThis, "bar",
                        {get: function() { return this.this_value; }});
  %PrepareFunctionForOptimization(B.prototype.inline_this);
  %PrepareFunctionForOptimization(B.prototype.foo);

  let o = new B();
  o.this_value = "correct value";

  // Fill in the feedback.
  let r = o.foo();
  assertEquals("correct value", r);

  %OptimizeFunctionOnNextCall(B.prototype.foo);

  // Test the optimized function.
  r = o.foo();
  assertEquals("correct value", r);

  // Assert that the function was not deoptimized.
  assertOptimized(B.prototype.foo);

  // Assert that inline_this really got inlined.
  assertFalse(inline_this_is_being_interpreted);
})();
