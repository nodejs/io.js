// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --no-lazy-feedback-allocation

let setter_calls = 0;

// Single canonical setter function, so that ClassWithSetter.foo has monomorphic
// setter JSFunction.
function setter_function() {
  print("Oh no, someone called the setter!");
  setter_calls++;
}

class ClassWithFooSetter {
  constructor() {
    // Define a "foo" setter on `this` using Object.defineProperty, so that it's
    // on the receiver and not the prototype.
    Object.defineProperty(this, "foo", {
      configurable: true,
      set: setter_function,
    });
  }
}

class ClassWithFooField extends ClassWithFooSetter {
  // Public fields are written using "DefineOwn", so in particular this write
  // shouldn't trigger the "foo" setter present on the receiver after the
  // ClassWithFooSetter construction.
  foo = 42;
}

// Run ClassWithFooField once, to populate the feedback vector...
let o1 = new ClassWithFooField();
// ... and again, to use the IC handler.
let o2 = new ClassWithFooField();

assertEquals(Object.getOwnPropertyDescriptor(o1, "foo"), {
  value: 42,
  writable: true,
  enumerable: true,
  configurable: true,
});
assertEquals(Object.getOwnPropertyDescriptor(o2, "foo"), {
  value: 42,
  writable: true,
  enumerable: true,
  configurable: true,
});
// The foo setter should not have been called.
assertEquals(setter_calls, 0);
