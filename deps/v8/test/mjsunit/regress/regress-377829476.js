// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --regexp-interpret-all

function f0() {
  return 'aaaaaaaaaaaaaa';
}
function f1() {
  return 'bbbbbbbbbbbbbb';
}
function f2() {
  return f0() + f1();
}
function f3(arg) {
  return arg + f2();
}
%PrepareFunctionForOptimization(f3);
f3("");
f3("");
%OptimizeFunctionOnNextCall(f3);
var foo = f3("");
foo.replace(/[\u1234]/g, "");
foo.replace(/[\u1234]/g, "");
