// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --maglev-inlining

function inlined(x) {
  if (x < 10) {
    x;
  } else {
    x;
  }
  // At a merge point, we should not think that we are merging the
  // caller function Phi node.
}

function foo(y) {
  y < 10;
  let a = 1;
  let b = 2;
  for (let i = 0; i < y; i++) {
    inlined(i); // Phi (representing i) can leak to the inlined function.
  }
}

%PrepareFunctionForOptimization(inlined);
%PrepareFunctionForOptimization(foo);
foo();
foo();
%OptimizeFunctionOnNextCall(foo);
foo();
