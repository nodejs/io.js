// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan

class Vector {
  constructor(x) {
    this.x = x;
  }
}

function magnitude(v) {
  return v.x;
}
%PrepareFunctionForOptimization(magnitude);

const oldObject = new Vector(0);  // Map1
const anotherOldObject = new Vector(0); // Map1

const newObject = new Vector(0.6); // Map2

// Make Map2 a migration target.
oldObject.x = 0.3;

// Make `magnitude` polymorphic with Map2 (a migration target) and an unrelated
// map.
magnitude(newObject);
const unrelated = {a: 0, b: 0, c: 0, x: 0};
magnitude(unrelated);

%OptimizeFunctionOnNextCall(magnitude);
magnitude(newObject);

assertOptimized(magnitude);

// It should now migrate old objects.
magnitude(anotherOldObject);

// TODO(turbofan): Fix https://issues.chromium.org/u/1/issues/383318227
// assertOptimized(magnitude);
