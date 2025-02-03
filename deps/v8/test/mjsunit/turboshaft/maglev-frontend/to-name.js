// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function to_name() {
  var o = {
    [Symbol.toPrimitive]() {}
  };
  return o;
}

%PrepareFunctionForOptimization(to_name);
let o = to_name();
assertEquals(o, to_name());
%OptimizeFunctionOnNextCall(to_name);
assertEquals(o, to_name());
assertOptimized(to_name);
