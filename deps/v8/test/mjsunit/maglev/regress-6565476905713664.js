// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

function f1() {
  return(Infinity != true);
}
%PrepareFunctionForOptimization(f1);
assertTrue(f1());
%OptimizeFunctionOnNextCall(f1);
assertTrue(f1());

function f2() {
  return(true != Infinity);
}
%PrepareFunctionForOptimization(f2);
assertTrue(f2());
%OptimizeFunctionOnNextCall(f2);
assertTrue(f2());

function two() { return 1.1; }
function f3(t) {
  if ((t == true) == two()) { return true; }
  return false;
}
%PrepareFunctionForOptimization(two);
%PrepareFunctionForOptimization(f3);
assertFalse(f3(true));
%OptimizeFunctionOnNextCall(f3);
assertFalse(f3(true));

function mz() { return -0.0; }
function f4(t) {
  if ((t == false) == mz()) { return true; }
  return false;
}
%PrepareFunctionForOptimization(mz);
%PrepareFunctionForOptimization(f4);
assertTrue(f4(true));
%OptimizeFunctionOnNextCall(f4);
assertTrue(f4(true));
