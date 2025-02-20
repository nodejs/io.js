// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbofan --no-always-turbofan
// Flags: --no-turbolev
// Flags: --typed-array-length-loading

var constantTypedArray = new Uint8Array(100);

function foo() {
  return constantTypedArray.length;
}
%PrepareFunctionForOptimization(foo);

foo();

%OptimizeFunctionOnNextCall(foo);
const a = foo();
assertEquals(100, a);
assertOptimized(foo);
