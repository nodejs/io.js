// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function f0() {
  function f2(v1) {
    v1();
    %DeoptimizeFunction(f0);
    %ThrowStackOverflow();
  }
  const v0 = new Promise(f2);
}
function f1() {
  const v3 = [].values().__proto__;
  v3.return = f0;
  try {
    new WeakSet([1]);
  } catch (v4) {}
}
%PrepareFunctionForOptimization(f0);
f1();
%OptimizeFunctionOnNextCall(f0);
f1();
