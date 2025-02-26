// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbolev --turbofan --no-always-turbofan
// Flags: --typed-array-length-loading

function foo(buffer) {
  let a = new Uint8Array(buffer);
  return a.length;
}

let ab = new ArrayBuffer(100, {maxByteLength: 200});
%PrepareFunctionForOptimization(foo);
assertEquals(100, foo(ab));

%OptimizeFunctionOnNextCall(foo);
assertEquals(100, foo(ab));
assertOptimized(foo);

ab.resize(200);

assertEquals(200, foo(ab));
assertOptimized(foo);
