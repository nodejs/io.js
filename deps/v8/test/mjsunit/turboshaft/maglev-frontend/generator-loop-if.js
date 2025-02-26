// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function* foo(x) {
  for (let i = 0; i < x; i++) {
    if (i % 2 == 0) {
      yield i;
    }
  }
  yield 42;
}

%PrepareFunctionForOptimization(foo);
let gen = foo(4);
assertEquals(0, gen.next().value);
assertEquals(2, gen.next().value);
assertEquals(42, gen.next().value);
assertEquals(undefined, gen.next().value);

%OptimizeFunctionOnNextCall(foo);
gen = foo(4);
assertEquals(0, gen.next().value);
assertEquals(2, gen.next().value);
assertEquals(42, gen.next().value);
assertEquals(undefined, gen.next().value);
