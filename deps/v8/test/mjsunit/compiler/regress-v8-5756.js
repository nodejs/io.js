// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

z = {};
t = new Uint8Array(3);
var m = 0;
var x = 10;

function k() {
  ++m;
  if (m % 10 != 9) {
    if (m > 20) // This if is to just force it to deoptimize.
      x = '1';  // this deoptimizes during the second invocation of k.
                // This causes two deopts, one eager at x = 1 and the
                // other lazy at t[2] = z.
     t[2] = z; // since we are assigning to Uint8Array, ToNumber
              // is called which calls this function again.
  }
}

function f1() {
  %PrepareFunctionForOptimization(k);
  z.toString = k;
  z.toString();
  z.toString();
  %OptimizeFunctionOnNextCall(k);
  z.toString();
}
f1();
