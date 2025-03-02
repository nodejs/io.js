// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbofan --no-always-turbofan
// Flags: --typed-array-length-loading

Object.defineProperty(Uint8Array.prototype, 'length',
                      {get: () => { return 3; }});

function foo(size) {
  let a = new Uint8Array(size);
  return a.length;
}
%PrepareFunctionForOptimization(foo);

assertEquals(3, foo(100));

%OptimizeFunctionOnNextCall(foo);
const val = foo(100);
assertEquals(3, val);
assertOptimized(foo);
