// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function C(a, b, c, d) {
  this.a = a;
  this.b = b;
  this.c = c;
  this.d = d;
}

function construct(arr) {
  return new C(...arr);
}

%PrepareFunctionForOptimization(construct);
let o1 = construct([17, 3.5, {}]);
%OptimizeFunctionOnNextCall(construct);
assertEquals(o1, construct([17, 3.5, {}]));
assertOptimized(construct);
