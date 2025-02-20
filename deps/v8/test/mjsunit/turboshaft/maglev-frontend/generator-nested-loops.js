// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function* foo(x) {
  for (let i = 0; i < x; i++) {
    for (let j = 0; j < x; j++) {
      for (let k = 0; k < x; k++) {
        if (j % 2 == 0) {
          yield i*32+j*8+k;
        }
      }
      yield j + 625;
    }
    yield i + 1459;
  }
  yield 42;
}

%PrepareFunctionForOptimization(foo);
let gen = foo(2);
assertEquals(0, gen.next().value);
assertEquals(1, gen.next().value);
assertEquals(625, gen.next().value);
assertEquals(626, gen.next().value);
assertEquals(1459, gen.next().value);
assertEquals(32, gen.next().value);
assertEquals(33, gen.next().value);
assertEquals(625, gen.next().value);
assertEquals(626, gen.next().value);
assertEquals(1460, gen.next().value);
assertEquals(42, gen.next().value);
assertEquals(undefined, gen.next().value);

%OptimizeFunctionOnNextCall(foo);
gen = foo(2);
assertEquals(0, gen.next().value);
assertEquals(1, gen.next().value);
assertEquals(625, gen.next().value);
assertEquals(626, gen.next().value);
assertEquals(1459, gen.next().value);
assertEquals(32, gen.next().value);
assertEquals(33, gen.next().value);
assertEquals(625, gen.next().value);
assertEquals(626, gen.next().value);
assertEquals(1460, gen.next().value);
assertEquals(42, gen.next().value);
assertEquals(undefined, gen.next().value);
