// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --turbofan --turbolev

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

const nonzero = new Vector(0.6); // Map2

// Map1 is now deprecated but Map2 is not a migration target.

%OptimizeFunctionOnNextCall(magnitude);
magnitude(nonzero);

// The first doesn't deopt if we pass a new object right away.
assertOptimized(magnitude);
