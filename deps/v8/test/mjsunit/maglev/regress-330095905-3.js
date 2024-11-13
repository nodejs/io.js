// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --maglev-escape-analysis

function foo(o) {
  o.g.h;
  return o;
}

function bar() {
  foo({ f:42, g:{h:3} });
}

%PrepareFunctionForOptimization(foo);
%PrepareFunctionForOptimization(bar);
bar();
bar();
%OptimizeMaglevOnNextCall(bar);
bar();
