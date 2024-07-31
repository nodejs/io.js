// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function f() {
  function resolve(result) {
    result();
    throw new Error();
  }
  function reject(error) {
  }
  const v8 = new Promise(resolve, reject);
}
function g() {
  const p = [].values().__proto__;
  p.return = f;
  try {
    new WeakSet([1]);
  } catch(e) {
  }
}
%PrepareFunctionForOptimization(f);
g();
%OptimizeFunctionOnNextCall(f);
g();
