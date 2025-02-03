// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function foo(n) {
  return -n;
}

%PrepareFunctionForOptimization(foo);
assertSame(-5, foo(5));
assertSame(-5, foo(5));
%OptimizeFunctionOnNextCall(foo);
assertSame(-0, foo(0));
