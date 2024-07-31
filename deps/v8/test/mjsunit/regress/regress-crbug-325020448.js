// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

var p1 = {}
var p2 = {}
function foo() {}
var o1 = new foo();

// Pre-seed 3 maps where con is constant
o1.con = 1;
o1.__proto__ = p1;
o1.__proto__ = p2;

// The o.con store only clears constness of the middle map. Both
// proto transitions above and below do not propagate the constness
// change. Thus both loads at the bottom and top seem to be reading
// from the same constant field.
function t1(o) {
  var a = o.con;
  o.__proto__ = p1;
  o.con = 2;
  o.__proto__ = p2;
  return a + o.con;
}
function t2() {
  var o = new foo();
  o.con = 1;
  return t1(o);
}
%PrepareFunctionForOptimization(t1);
assertTrue(t2() == 3);
%OptimizeFunctionOnNextCall(t1);
assertTrue(t2() == 3);
