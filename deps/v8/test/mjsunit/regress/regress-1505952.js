// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function bar(a, b) {
  if (a instanceof Array) {
    for (var i = 0; i < a.length; i++) {
      if (!bar(a[i], b[i])) return false;
    }
  }
}

function foo() {
  bar();
  bar();
}

%PrepareFunctionForOptimization(bar);
%PrepareFunctionForOptimization(foo);
foo();
bar([[, 1.5], {}], [[, 1.5], {}]);
foo();
%OptimizeFunctionOnNextCall(foo);
foo();
