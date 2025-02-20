// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function* g(x) {
  yield x+4;
}
%NeverOptimizeFunction(g);

function* f(x) {
  for (let i = 0; i < x; i++) {
    if (i % 2 == 0) {
      yield* g(i);
    } else {
      yield i * 13;
    }
  }
}

%PrepareFunctionForOptimization(f);
let gen = f(4);
assertEquals(4, gen.next().value);
assertEquals(13, gen.next().value);
assertEquals(6, gen.next().value);
assertEquals(39, gen.next().value);
assertEquals(undefined, gen.next().value);

%OptimizeFunctionOnNextCall(f);
gen = f(4);
assertEquals(4, gen.next().value);
assertEquals(13, gen.next().value);
assertEquals(6, gen.next().value);
assertEquals(39, gen.next().value);
assertEquals(undefined, gen.next().value);
