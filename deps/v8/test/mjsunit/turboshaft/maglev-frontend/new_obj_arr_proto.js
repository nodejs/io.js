// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function make_obj() {
  const a = {};
  return {__proto__: [], p1: a};
}

%PrepareFunctionForOptimization(make_obj);
for (var i = 0; i < 10; i++) make_obj();
let expected = make_obj();
%OptimizeFunctionOnNextCall(make_obj);
assertEquals(expected, make_obj());
assertOptimized(make_obj);
