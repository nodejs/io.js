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
magnitude(zero);

// The first call immediately deopts
assertUnoptimized(magnitude);

// But we learn and don't deopt any more.
%OptimizeFunctionOnNextCall(magnitude);
magnitude(zero);
assertOptimized(magnitude);

// Also passing other old objects won't deopt.
magnitude(anotherOldObject);
assertOptimized(magnitude);
