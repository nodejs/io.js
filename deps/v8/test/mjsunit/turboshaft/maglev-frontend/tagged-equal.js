// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

function f(x, y) {
  let is_eq = x === y;
  let r = is_eq + 2;
  if (x === y) {
    return r + 42;
  } else {
    return r + 25;
  }
}

let o = {};

%PrepareFunctionForOptimization(f);
assertEquals(27, f(o, []));
assertEquals(45, f(o, o));
%OptimizeMaglevOnNextCall(f);
assertEquals(27, f(o, []));
assertEquals(45, f(o, o));
%OptimizeFunctionOnNextCall(f);
assertEquals(27, f(o, []));
assertEquals(45, f(o, o));
assertOptimized(f);

// Making sure that strings trigger a deopt (since === for strings is regular
// string equality, not reference equality).
assertEquals(45, f("abcdefghijklmno", "abcde" + "fghijklmno"));
assertUnoptimized(f);
