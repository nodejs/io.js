// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --super-ic --turbofan
// Flags: --no-always-turbofan --deopt-every-n-times=0

(function TestPropertyIsInTheHomeObjectsProto() {
  // Test where the property is a constant found on home object's proto. This
  // will generate a minimorphic property load.
  class A {}
  A.prototype.bar = "wrong value: A.prototype.bar";

  class B extends A {};
  B.prototype.bar = "correct value";

  class C extends B {
    foo() { return super.bar; }
  }
  C.prototype.bar = "wrong value: D.prototype.bar";
  %PrepareFunctionForOptimization(C.prototype.foo);

  let o = new C();
  o.bar = "wrong value: o.bar";

  // Fill in the feedback.
  let r = o.foo();
  assertEquals("correct value", r);
  %OptimizeFunctionOnNextCall(C.prototype.foo);

  // Test the optimized function.
  r = o.foo();
  assertEquals("correct value", r);

  // Assert that the function was not deoptimized.
  assertOptimized(C.prototype.foo);

  // Change the property value.
  B.prototype.bar = "new value";
  r = o.foo();
  assertEquals("new value", r);
})();

(function TestPropertyIsGetterInTheHomeObjectsProto() {
  // Test where the property is a constant found on home object's proto. This
  // will generate a minimorphic property load.
  class A {}
  A.prototype.bar = "wrong value: A.prototype.bar";

  class B extends A {
    get bar() { return this.this_value; }
  }
  class C extends B {
    foo() { return super.bar; }
  }
  C.prototype.bar = "wrong value: D.prototype.bar";
  %PrepareFunctionForOptimization(C.prototype.foo);

  let o = new C();
  o.bar = "wrong value: o.bar";
  o.this_value = "correct value";

  // Fill in the feedback.
  let r = o.foo();
  assertEquals("correct value", r);
  %OptimizeFunctionOnNextCall(C.prototype.foo);

  // Test the optimized function.
  r = o.foo();
  assertEquals("correct value", r);

  // Assert that the function was not deoptimized.
  assertOptimized(C.prototype.foo);

  // Change the property value.
  o.this_value = "new value";
  r = o.foo();
  assertEquals("new value", r);
})();

(function TestPropertyIsConstantInThePrototypeChain() {
  // Test where the property is a constant found on the prototype chain of the
  // lookup start object.
  class A {}
  A.prototype.bar = "wrong value: A.prototype.bar";

  class B extends A {};
  B.prototype.bar = "correct value";

  class C extends B {};

  class D extends C {
    foo() { return super.bar; }
  }
  D.prototype.bar = "wrong value: D.prototype.bar";
  %PrepareFunctionForOptimization(D.prototype.foo);

  let o = new D();
  o.bar = "wrong value: o.bar";

  // Fill in the feedback.
  let r = o.foo();
  assertEquals("correct value", r);

  %OptimizeFunctionOnNextCall(D.prototype.foo);

  // Test the optimized function.
  r = o.foo();
  assertEquals("correct value", r);

  // Assert that the function was not deoptimized.
  assertOptimized(D.prototype.foo);

  // Change the property value.
  B.prototype.bar = "new value";

  // Assert that the function was deoptimized (dependency to the constant
  // value).
  // TODO(v8:11457) We don't support inlining JSLoadNamedFromSuper for
  // dictionary mode prototypes, yet. Therefore, if
  // v8_dict_property_const_tracking is enabled, the optimized code only
  // contains a call to the IC handler and doesn't get deopted.
  assertEquals(%IsDictPropertyConstTrackingEnabled(),
               isOptimized(D.prototype.foo));

  r = o.foo();
  assertEquals("new value", r);
})();

(function TestPropertyIsNonConstantData() {
  // Test for a case where the property is a non-constant data property found
  // in the lookup start object.
  class A {}
  A.prototype.bar = "wrong value: A.prototype.bar";

  class B extends A {};
  B.prototype.bar = "initial value";

  class C extends B {
    foo() { return super.bar; }
  }
  C.prototype.bar = "wrong value: C.prototype.bar";
  %PrepareFunctionForOptimization(C.prototype.foo);

  let o = new C();
  o.bar = "wrong value: o.bar";

  // Make the property look like a non-constant.
  B.prototype.bar = "correct value";

  // Fill in the feedback.
  let r = o.foo();
  assertEquals("correct value", r);
  %OptimizeFunctionOnNextCall(C.prototype.foo);

  // Test the optimized function.
  r = o.foo();
  assertEquals("correct value", r);

  // Assert that the function was not deoptimized.
  assertOptimized(C.prototype.foo);

  // Change the property value.
  B.prototype.bar = "new value";
  r = o.foo();
  assertEquals("new value", r);

  // Assert that the function was still not deoptimized (the value was not a
  // constant to begin with).
  assertOptimized(C.prototype.foo);
})();

(function TestPropertyIsGetter() {
  class A {}
  A.prototype.bar = "wrong value: A.prototype.bar";

  class B extends A {
    get bar() {
      return this.test_value;
    }
  };

  class C extends B {}

  class D extends C {
    foo() {
      const b = super.bar;
      return b;
    }
  }
  %PrepareFunctionForOptimization(D.prototype.foo);
  D.prototype.bar = "wrong value: D.prototype.bar";

  let o = new D();
  o.bar = "wrong value: o.bar";
  o.test_value = "correct value";

  // Fill in the feedback.
  let r = o.foo();
  assertEquals("correct value", r);

  %OptimizeFunctionOnNextCall(D.prototype.foo);

  // Test the optimized function.
  r = o.foo();
  assertEquals("correct value", r);

  // Assert that the function was not deoptimized.
  assertOptimized(D.prototype.foo);
})();

(function TestPropertyInsertedInTheMiddle() {
  // Test for a case where the property is a constant found in the lookup start
  // object.
  class A {}
  A.prototype.bar = "correct value";

  class B extends A {};

  class C extends B {
    foo() { return super.bar; }
  }
  C.prototype.bar = "wrong value: C.prototype.bar";
  %PrepareFunctionForOptimization(C.prototype.foo);

  let o = new C();
  o.bar = "wrong value: o.bar";

  // Fill in the feedback.
  let r = o.foo();
  assertEquals("correct value", r);
  %OptimizeFunctionOnNextCall(C.prototype.foo);

  // Test the optimized function.
  r = o.foo();
  assertEquals("correct value", r);

  // Assert that the function was not deoptimized.
  assertOptimized(C.prototype.foo);

  // Insert the property into the prototype chain between the lookup start
  // object and the old holder.
  B.prototype.bar = "new value";

  // Depending on how long the compilation takes it can happen that we add a
  // prototype chain dependency and already changing the prototype deoptimizes
  // the code. This can then trigger eager re-optimization, which means after
  // invoking foo() we cannot know if it is optimized or not.
  let was_optimized = isOptimized(C.prototype.foo);

  r = o.foo();
  assertEquals("new value", r);

  // Assert that the function was deoptimized (holder changed).
  // TODO(v8:11457) We don't support inlining JSLoadNamedFromSuper for
  // dictionary mode prototypes, yet. Therefore, if
  // v8_dict_property_const_tracking is enabled, the optimized code only
  // contains a call to the IC handler and doesn't get deopted.
  if (was_optimized) {
    assertEquals(%IsDictPropertyConstTrackingEnabled(),
                 isOptimized(C.prototype.foo));
  }
})();

(function TestUnexpectedHomeObjectPrototypeDeoptimizes() {
  class A {}
  A.prototype.bar = "wrong value: A.prototype.bar";

  class B extends A {}
  B.prototype.bar = "correct value";

  class C extends B {}

  class D extends C {
    foo() { return super.bar; }
  }
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

  // Change the home object's prototype.
  D.prototype.__proto__ = {"bar": "new value"};

  // Depending on how long the compilation takes it can happen that we add a
  // prototype chain dependency and already changing the prototype deoptimizes
  // the code. This can then trigger eager re-optimization, which means after
  // invoking foo() we cannot know if it is optimized or not.
  let was_optimized = isOptimized(D.prototype.foo);

  r = o.foo();

  // Assert that the function was deoptimized.
  // TODO(v8:11457) We don't support inlining JSLoadNamedFromSuper for
  // dictionary mode prototypes, yet. Therefore, if
  // v8_dict_property_const_tracking is enabled, the optimized code only
  // contains a call to the IC handler and doesn't get deopted.
  if (was_optimized) {
    assertEquals(%IsDictPropertyConstTrackingEnabled(),
                 isOptimized(D.prototype.foo));
  }

  assertEquals("new value", r);
})();

(function TestUnexpectedReceiverDoesNotDeoptimize() {
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
  let r = o.foo();
  assertEquals("correct value", r);

  %OptimizeFunctionOnNextCall(C.prototype.foo);
  o.foo();
  assertOptimized(C.prototype.foo);

  // Test the optimized function with an unexpected receiver.
  r = C.prototype.foo.call({'lol': 5});
  assertEquals("correct value", r);

  // Assert that the function was not deoptimized.
  assertOptimized(C.prototype.foo);
})();

(function TestPolymorphic() {
  class A {}
  A.prototype.bar = "wrong value: A.prototype.bar";

  class B extends A {}
  B.prototype.bar = "correct value";

  class C extends B {}

  class D extends C {
    foo() { return super.bar; }
  }
  %PrepareFunctionForOptimization(D.prototype.foo);
  D.prototype.bar = "wrong value: D.prototype.bar";

  const o = new D();

  // Create objects which will act as the "home object's prototype" later.
  const prototypes = [{"a": 0}, {"b": 0}];
  for (p of prototypes) {
    p.__proto__ = B.prototype;
  }

  // Fill in the feedback (polymorphic).
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
})();

(function TestPolymorphicWithGetter() {
  class A {}
  A.prototype.bar = "wrong value: A.prototype.bar";

  class B extends A {
    get bar() {
      return this.test_value;
    }
  }

  class C extends B {}

  class D extends C {
    foo() { return super.bar; }
  }
  %PrepareFunctionForOptimization(D.prototype.foo);
  D.prototype.bar = "wrong value: D.prototype.bar";

  const o = new D();
  o.test_value = "correct value";

  // Create objects which will act as the "home object's prototype" later.
  const prototypes = [{"a": 0}, {"b": 0}];
  for (p of prototypes) {
    p.__proto__ = B.prototype;
  }

  // Fill in the feedback.
  for (p of prototypes) {
    D.prototype.__proto__ = p;
    const r = o.foo();
    assertEquals("correct value", r);
  }

  %OptimizeFunctionOnNextCall(D.prototype.foo);

  // Test the optimized function - don't change the home object's proto any
  // more.
  const r = o.foo();
  assertEquals("correct value", r);

  // Assert that the function was not deoptimized.
  assertOptimized(D.prototype.foo);
})();

(function TestPolymorphicMixinDoesNotDeopt() {
  function createClasses() {
    class A {}
    A.prototype.bar = "correct value";
    class B extends A {
      foo() { return super.bar; }
    }
    return B;
  }

  const b1 = createClasses();
  %PrepareFunctionForOptimization(b1.prototype.foo);
  const b2 = createClasses();
  %PrepareFunctionForOptimization(b2.prototype.foo);

  class c1 extends b1 {};
  class c2 extends b2 {};

  const objects = [new c1(), new c2()];

  // Fill in the feedback.
  for (o of objects) {
    const r = o.foo();
    assertEquals("correct value", r);
  }
  %OptimizeFunctionOnNextCall(b1.prototype.foo);
  %OptimizeFunctionOnNextCall(b2.prototype.foo);

  // Test the optimized function.
  for (o of objects) {
    const r = o.foo();
    assertEquals("correct value", r);
  }
  assertOptimized(b1.prototype.foo);
  assertOptimized(b2.prototype.foo);
})();

(function TestHomeObjectProtoIsGlobalThis() {
  class A {}

  class B extends A {
    foo() { return super.bar; }
  }
  B.prototype.__proto__ = globalThis;
  globalThis.bar = "correct value";
  %PrepareFunctionForOptimization(B.prototype.foo);

  let o = new B();

  // Fill in the feedback.
  let r = o.foo();
  assertEquals("correct value", r);

  %OptimizeFunctionOnNextCall(B.prototype.foo);

  // Test the optimized function.
  r = o.foo();
  assertEquals("correct value", r);

  // Assert that the function was not deoptimized.
  assertOptimized(B.prototype.foo);

  globalThis.bar = "new value";

  r = o.foo();
  assertEquals("new value", r);
})();

(function TestMegamorphic() {
  class A {}
  A.prototype.bar = "wrong value: A.prototype.bar";

  class B extends A {}
  B.prototype.bar = "correct value";

  class C extends B {}

  class D extends C {
    foo() { return super.bar; }
  }
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
})();

(function TestMegamorphicWithGetter() {
  class A {}
  A.prototype.bar = "wrong value: A.prototype.bar";

  class B extends A {
    get bar() {
      return this.test_value;
    }
  };

  class C extends B {}

  class D extends C {
    foo() { return super.bar;}
  }
  %PrepareFunctionForOptimization(D.prototype.foo);
  D.prototype.bar = "wrong value: D.prototype.bar";

  const o = new D();
  o.test_value = "correct value";

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
  const r = o.foo();
  assertEquals("correct value", r);
})();

(function TestHomeObjectProtoIsGlobalThisGetterProperty() {
  class A {}

  class B extends A {
    foo() { return super.bar; }
  }
  B.prototype.__proto__ = globalThis;
  Object.defineProperty(globalThis, "bar", {get: function() { return this.this_value; }});
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
})();

(function TestHomeObjectProtoIsFunctionAndPropertyIsPrototype() {
  // There are special optimizations for accessing Function.prototype. Test
  // that super property access which ends up accessing it works.
  class A {}

  class B extends A {
    foo() { return super.prototype; }
  }
  function f() {}
  B.prototype.__proto__ = f;
  %PrepareFunctionForOptimization(B.prototype.foo);

  let o = new B();

  // Fill in the feedback.
  let r = o.foo();
  assertEquals(f.prototype, r);

  %OptimizeFunctionOnNextCall(B.prototype.foo);

  // Test the optimized function.
  r = o.foo();
  assertEquals(f.prototype, r);

  // Assert that the function was not deoptimized.
  assertOptimized(B.prototype.foo);
})();
