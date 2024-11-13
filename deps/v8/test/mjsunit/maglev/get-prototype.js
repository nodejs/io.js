// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev

function test(obj) {
  var funs = [function(x) {
    // Force a map check on x.x
    x.size;
    // Will be constantfolded
    return Object.getPrototypeOf(x);
  }, function(x) {
    x.size;
    try {
      return Reflect.getPrototypeOf(x);
    } catch(e) {
      return x.__proto__;
    }
  }, function(x) {
    x.size;
    return x.__proto__;
  }];
  funs.forEach((f) => {
    %PrepareFunctionForOptimization(f);
    f(obj);
    %OptimizeMaglevOnNextCall(f);
    assertTrue(f(obj) == Object.getPrototypeOf(obj));
    assertTrue(f(obj) == obj.__proto__);
    assertFalse(f(obj) == 1);
  });
}

test({});
test({x:1});
test(1);
test(1.1);
test([]);
test("");
test(true);

var f = function(x) {
  console.log(x);
  return Object.getPrototypeOf(x);
};
%PrepareFunctionForOptimization(f);
try{f();} catch(e) {}
%OptimizeMaglevOnNextCall(f);
let thr = false;
try{f();} catch(e) {thr = true}
assertTrue(thr);
