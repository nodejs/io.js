// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev

const v0 = [];
function f1(a2) {
    a2[1073741825] = a2;
}
%PrepareFunctionForOptimization(f1);
f1(f1);
f1(v0);
%OptimizeMaglevOnNextCall(f1);
var ok = false;
try {
 f1();
} catch (e) {
  ok = true;
}
assertTrue(ok);
