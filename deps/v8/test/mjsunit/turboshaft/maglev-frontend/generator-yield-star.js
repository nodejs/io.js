// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function* g(x) {
  yield x+4;
}
%NeverOptimizeFunction(g);

function* f(x) {
  yield* g(x);
  yield* g(5);
}

%PrepareFunctionForOptimization(f);
let gen = f(4);
assertEquals(8, gen.next().value);
assertEquals(9, gen.next().value);
assertEquals(undefined, gen.next().value);

%OptimizeFunctionOnNextCall(f);
gen = f(4);
assertEquals(8, gen.next().value);
assertEquals(9, gen.next().value);
assertEquals(undefined, gen.next().value);
