// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function foo(arr) {
  var it = arr.values();
  it.next();
  arr.push(42);
  return it.next().done;
}
%PrepareFunctionForOptimization(foo);
assertTrue(foo([]));
assertTrue(foo([]));
%OptimizeFunctionOnNextCall(foo);
assertTrue(foo([]));
