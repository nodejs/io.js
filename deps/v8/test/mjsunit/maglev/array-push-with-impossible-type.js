// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan

function f(a) {
  a.push(4.5);
  a.push({});  // Trying to push a non-double into a double array
}

%PrepareFunctionForOptimization(f);
const a1 = [1.2];
f(a1);
assertEquals(3, a1.length);
assertTrue(%HasPackedElements(a1));

%OptimizeMaglevOnNextCall(f);
const a2 = [1.2];
f(a2);

// Immediate deopt.
assertFalse(isMaglevved(f));

assertEquals(3, a2.length);
assertTrue(%HasPackedElements(a2));
