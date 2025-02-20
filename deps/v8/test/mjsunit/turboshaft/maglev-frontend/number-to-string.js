// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function number_to_string(n) {
  let v = n + 5;
  return String(v);
}

%PrepareFunctionForOptimization(number_to_string);
assertEquals(String(458), number_to_string(453));
%OptimizeFunctionOnNextCall(number_to_string);
assertEquals(String(458), number_to_string(453));
assertOptimized(number_to_string);
