// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-always-turbofan --turbofan

const r = RegExp('bla');

function foo() {
  r.test('string');
}

%PrepareFunctionForOptimization(foo);
foo();
foo();
%OptimizeFunctionOnNextCall(foo);
foo();
assertOptimized(foo);

r.__proto__.exec = function() {
  return null;
}
Object.freeze(r.__proto__);

assertUnoptimized(foo);
foo();
