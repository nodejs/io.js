// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function rest_len(a, ...rest) {
  return rest.length;
}

%PrepareFunctionForOptimization(rest_len);
assertEquals(4, rest_len("a", 1, {}, 15.25, []));
%OptimizeFunctionOnNextCall(rest_len);
assertEquals(4, rest_len("a", 1, {}, 15.25, []));
assertOptimized(rest_len);
