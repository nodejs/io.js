// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-lazy-feedback-allocation

class A {
  constructor(n, is_enum) {
    if (n) {
      Object.defineProperty(this, "p"+n,
          {value: 0, enumerable: false, configurable: true, writable: true});
    }
    Object.defineProperty(this, "boom",
        {value: 42, enumerable: is_enum, configurable: true, writable: true});
    Object.defineProperty(this, "0",
        {value: 55, enumerable: is_enum, configurable: true, writable: true});
  }
}

class B extends A {
  boom = 0;
  [0]  = 0;
  foo  = 1;

  constructor(...args) {
    super(...args);
  }
}

const expected = ["0", "boom", "foo"];

(function TestMonomorphic() {
  for (let i = 1; i <= 12; i++) {
    const b = new B(0, false);
    assertEquals(expected, Object.keys(b));
  }
})();

(function TestPolymorphic() {
  for (let i = 1; i <= 12; i++) {
    const b = new B(0, true);
    assertEquals(expected, Object.keys(b));
  }
})();

(function TestMegamorphic() {
  for (let i = 1; i <= 12; i++) {
    const b = new B(i, false);
    assertEquals(expected, Object.keys(b));
  }
  for (let i = 1; i <= 12; i++) {
    const b = new B(i, true);
    assertEquals(expected, Object.keys(b));
  }
})();
