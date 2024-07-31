// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --allow-natives-syntax

const v0 = {};
function f0() {}
const v1 = new f0();
function f1(v4, v5) {
  v4.d = v0;
  function f3() {}
  const v6 = new f3();
  let v7 = 0;
  for (let v8 = ++v7; v8 <= v7; ++v8) {}
  return v5;
}
f1(v1);
gc();
f1(v1);
function f2() {
  const v9 = Array();
  v9.findLast(Array);
  v9.reduce(f1, 0);
}
%PrepareFunctionForOptimization(f2);
f2();
%OptimizeFunctionOnNextCall(f2);
f2();
