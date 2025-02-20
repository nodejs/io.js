// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function isBoolean(v) {
  return typeof v === "boolean";
}

%PrepareFunctionForOptimization(isBoolean);
assertTrue(isBoolean(true));
assertTrue(isBoolean(false));
assertFalse(isBoolean({}));

%OptimizeFunctionOnNextCall(isBoolean);
assertTrue(isBoolean(true));
assertTrue(isBoolean(false));
assertFalse(isBoolean({}));
