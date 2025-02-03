// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function check(i) {
  function inlined(a) {
    a[2];
  }
  %PrepareFunctionForOptimization(inlined);
  inlined([1,,""]);
  inlined([]);
  return i < 2;
}

%PrepareFunctionForOptimization(check);

for (let i = 0; check(i); i++) {
  %OptimizeFunctionOnNextCall(check);
}
