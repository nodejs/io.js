// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function f(b) { if (b) throw "help" }
function g(e,b) {
  for (const x of e) {
    try {} finally {}
    f(b);
  }
}

function h() {
  try {
    g([[1,2],[3,4]], true);
  } catch (e) {}
}

%NeverOptimizeFunction(f);
%PrepareFunctionForOptimization(g);
h();
h();
h();
h();
h();
%OptimizeMaglevOnNextCall(g);
h();
