// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function g(o) { return o.u; }

function create_shallow_obj(x) {
  var o = {u: x};
  return g(o);
}

%PrepareFunctionForOptimization(create_shallow_obj);
assertEquals(42, create_shallow_obj(42));
assertEquals(3.56, create_shallow_obj(3.56));
%OptimizeFunctionOnNextCall(create_shallow_obj);
assertEquals(42, create_shallow_obj(42));
assertEquals(3.56, create_shallow_obj(3.56));
