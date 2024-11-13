// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev

function equals(a, b) {
  if (a == b) {
    return 1;
  }
  return 0;
}

%PrepareFunctionForOptimization(equals);

// Add the NumberOrBoolean feedback.
equals(0, true);
equals(true, 1);

%OptimizeMaglevOnNextCall(equals);
equals(0, true);

assertEquals(0, equals(true, 0));
assertEquals(1, equals(true, 1));
assertEquals(0, equals(true, 3.14));

assertEquals(1, equals(false, 0));
assertEquals(0, equals(false, 1));
assertEquals(0, equals(false, 2.72));

assertEquals(0, equals(0, true));
assertEquals(1, equals(1, true));
assertEquals(0, equals(1.41, true));

assertEquals(1, equals(0, false));
assertEquals(0, equals(1, false));
assertEquals(0, equals(1.62, false));

assertEquals(1, equals(true, true));
assertEquals(1, equals(false, false));
assertEquals(1, equals(3, 3));

assertEquals(0, equals(false, true));
assertEquals(0, equals(true, false));
assertEquals(0, equals(3, 4));

assertTrue(isMaglevved(equals));

// Passing a non-boolean oddball deopts.
assertEquals(0, equals(undefined, true));

assertFalse(isMaglevved(equals));
