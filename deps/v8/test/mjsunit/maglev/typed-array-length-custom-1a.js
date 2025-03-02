// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --typed-array-length-loading

Object.defineProperty(Uint8Array.prototype, 'length',
                      {get: () => { return 3; }});

function foo(size) {
  let a = new Uint8Array(size);
  return a.length;
}
%PrepareFunctionForOptimization(foo);

assertEquals(3, foo(100));

%OptimizeMaglevOnNextCall(foo);
const val = foo(100);
assertEquals(3, val);
assertTrue(isMaglevved(foo));
