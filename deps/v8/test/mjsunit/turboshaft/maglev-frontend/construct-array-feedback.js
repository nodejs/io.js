// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function foo(t) {
  return new t();
}

%PrepareFunctionForOptimization(foo);
a = foo(Array);
a[0] = 3.5;
a = foo(Array);
a[0] = 3.5;
b = foo(Array);
assertTrue(%HasDoubleElements(b));

%OptimizeFunctionOnNextCall(foo);
b = foo(Array);
assertTrue(%HasDoubleElements(b));
assertOptimized(foo);
