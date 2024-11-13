// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev

function strictEquals(a, b) {
  if (a === b) {
    return 1;
  }
  return 0;
}

%PrepareFunctionForOptimization(strictEquals);

// Add the NumberOrBoolean feedback.
strictEquals(0, true);
strictEquals(true, 1);

%OptimizeMaglevOnNextCall(strictEquals);
strictEquals(0, true);

assertEquals(0, strictEquals(true, 0));
assertEquals(0, strictEquals(true, 1));
assertEquals(0, strictEquals(true, 3.14));

assertEquals(0, strictEquals(false, 0));
assertEquals(0, strictEquals(false, 1));
assertEquals(0, strictEquals(false, 2.72));

assertEquals(0, strictEquals(0, true));
assertEquals(0, strictEquals(1, true));
assertEquals(0, strictEquals(1.41, true));

assertEquals(0, strictEquals(0, false));
assertEquals(0, strictEquals(1, false));
assertEquals(0, strictEquals(1.62, false));

assertEquals(1, strictEquals(true, true));
assertEquals(1, strictEquals(false, false));
assertEquals(1, strictEquals(3, 3));

assertEquals(0, strictEquals(false, true));
assertEquals(0, strictEquals(true, false));
assertEquals(0, strictEquals(3, 4));
