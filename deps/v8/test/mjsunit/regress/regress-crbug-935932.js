// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function test(func, expect) {
    %PrepareFunctionForOptimization(func);
    assertTrue(func() == expect);
    %OptimizeFunctionOnNextCall(func);
    assertTrue(func() == expect);
}

// Check loading a constant off the global.
var v0 = 10;
function check_v0() { return "v0" in this; }
test(check_v0, true);

// make it non-constant.
v0 = 0;
test(check_v0, true);

// test a missing value.
function check_v1() { return "v1" in this; }
test(check_v1, false);
this.v1 = 3;
test(check_v1, true);
delete this.v1;
test(check_v1, false);

// test undefined.
var v2;
function check_v2() { return "v2" in this; }
test(check_v2, true);

// test a constant object.
var v3 = {};
function check_v3() { return "v3" in this; }
test(check_v3, true);
// make the object non-constant.
v3 = [];
test(check_v3, true);

// test non-configurable
Object.defineProperty(this, "v4", { value: {}, configurable: false});
function check_v4() { return "v4" in this; }
test(check_v4, true);

// Test loading from arrays with different prototypes.
(function() {
  function testIn(index, array) {
    return index in array;
  }
  %PrepareFunctionForOptimization(testIn);

  let a = [];
  a.__proto__ = [0,1,2];
  a[1] = 3;

  // First load will set IC to Load handle with allow hole to undefined conversion false.
  assertTrue(testIn(0, a));
  // Second load will hit ICMiss when hole is loaded. Seeing the same map twice, the IC will be set megamorphic.
  assertTrue(testIn(0, a));
  %OptimizeFunctionOnNextCall(testIn);
  // Test JIT to ensure proper handling.
  assertTrue(testIn(0, a));

  %ClearFunctionFeedback(testIn);
  %DeoptimizeFunction(testIn);
  %PrepareFunctionForOptimization(testIn);

  // First load will set IC to Load handle with allow hole to undefined conversion false.
  assertTrue(testIn(0, a));
  %OptimizeFunctionOnNextCall(testIn);
  // Test JIT to ensure proper handling if hole is loaded.
  assertTrue(testIn(0, a));

  // Repeat the same testing for access out-of-bounds of the array, but in bounds of its prototype.
  %ClearFunctionFeedback(testIn);
  %DeoptimizeFunction(testIn);
  %PrepareFunctionForOptimization(testIn);

  assertTrue(testIn(2, a));
  assertTrue(testIn(2, a));
  %OptimizeFunctionOnNextCall(testIn);
  assertTrue(testIn(2, a));

  %ClearFunctionFeedback(testIn);
  %DeoptimizeFunction(testIn);
  %PrepareFunctionForOptimization(testIn);

  assertTrue(testIn(2, a));
  %OptimizeFunctionOnNextCall(testIn);
  assertTrue(testIn(2, a));
})();
