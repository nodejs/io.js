// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

let undetectable = %GetUndetectable();
function foo(a) {
  return !!a.x;
}
var o = { x : undetectable };
%PrepareFunctionForOptimization(foo);
assertFalse(foo(o));
%OptimizeFunctionOnNextCall(foo);
assertFalse(foo(o));
