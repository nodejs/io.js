// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function foo(a) {
  for (var [] of a.entries()) {
  }
}
%PrepareFunctionForOptimization(foo);
foo([1]);
%OptimizeFunctionOnNextCall(foo);
foo([1]);

function bar() {
  new Map(['bar'].entries());
}

%PrepareFunctionForOptimization(bar);
bar();
%OptimizeFunctionOnNextCall(bar);
bar();
