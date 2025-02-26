// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbofan --noalways-turbofan

class C {};
const c = new C;
const getPrototypeOf = Object.getPrototypeOf;

function bar(f) {
  return f();
}
function foo() {
  return bar(getPrototypeOf.bind(undefined, c));
}

%PrepareFunctionForOptimization(foo);
%PrepareFunctionForOptimization(bar);
bar(function() {});
foo();
%OptimizeFunctionOnNextCall(foo);
foo();
assertOptimized(foo);

c.prop = 42;
// Assert that the call reducer optimized the call to Object.getPrototypeOf
// by asserting that foo gets deopted when c's previous map becomes
// unstable.
assertUnoptimized(foo);
foo();
