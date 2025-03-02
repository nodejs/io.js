// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

function symbol_eq(x, y) { return x === y; }

const a = Symbol("a");
const b = Symbol("b");

%PrepareFunctionForOptimization(symbol_eq);
assertFalse(symbol_eq(a, b));
assertTrue(symbol_eq(a, a));
%OptimizeFunctionOnNextCall(symbol_eq);
assertFalse(symbol_eq(a, b));
assertTrue(symbol_eq(a, a));
assertOptimized(symbol_eq);

assertFalse(symbol_eq(a, "a"));
assertUnoptimized(symbol_eq);
