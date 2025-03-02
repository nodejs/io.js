// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function foo(x) {
  // Turbofan's LoadElimination only tracks up to 32 fields on each object. We
  // thus create an object with more fields than that, so that `o.v39` below is
  // not load-eliminated by Turbofan.
  let o = {
    v0 : 0.5,
    v1 : 0.5,
    v2 : 0.5,
    v3 : 0.5,
    v4 : 0.5,
    v5 : 0.5,
    v6 : 0.5,
    v7 : 0.5,
    v8 : 0.5,
    v9 : 0.5,
    v10 : 0.5,
    v11 : 0.5,
    v12 : 0.5,
    v13 : 0.5,
    v14 : 0.5,
    v15 : 0.5,
    v16 : 0.5,
    v17 : 0.5,
    v18 : 0.5,
    v19 : 0.5,
    v20 : 0.5,
    v21 : 0.5,
    v22 : 0.5,
    v23 : 0.5,
    v24 : 0.5,
    v25 : 0.5,
    v26 : 0.5,
    v27 : 0.5,
    v28 : 0.5,
    v29 : 0.5,
    v30 : 0.5,
    v31 : 0.5,
    v32 : 0.5,
    v33 : 0.5,
    v34 : 0.5,
    v35 : 0.5,
    v36 : 0.5,
    v37 : 0.5,
    v38 : 0.5,
    v39 : 0.5 };

  // Turboshaft will now load-eliminate o.v39 (because it doesn't have a limit
  // on the number of fields tracked), and then recognize the Math.pow(x, 0.5)
  // call and optimize it to sqrt(x).
  return 1 / Math.pow(x, o.v39);
}

%PrepareFunctionForOptimization(foo);
assertEquals(Infinity, foo(-0));
assertEquals(Infinity, foo(-0));
%OptimizeFunctionOnNextCall(foo);
assertEquals(Infinity, foo(-0));
