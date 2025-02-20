// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev --no-always-turbofan

class Vector {
  constructor(x) {
    this.x = x;
  }
}

function magnitude(v) {
  return v.x;
}

const zero = new Vector(0);  // Map1
const anotherOldObject = new Vector(0); // Map1

%PrepareFunctionForOptimization(magnitude);
magnitude(zero);

// Make the feedback polymorphic with an unrelated map.
const unrelated = {a: 0, b: 0, c: 0, x: 0};
magnitude(unrelated);

const nonzero = new Vector(0.6); // Map2

// Map1 is now deprecated but Map2 is not a migration target.

%OptimizeMaglevOnNextCall(magnitude);
magnitude(nonzero);

// The first call doesn't deopt if called with a new object.
assertTrue(isMaglevved(magnitude));
