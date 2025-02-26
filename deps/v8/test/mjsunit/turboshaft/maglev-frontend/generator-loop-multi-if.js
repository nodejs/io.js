// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function* foo(x) {
  let s = 625;
  for (let i = 0; i < x; i++) {
    s -= i;
    if (i % 2 == 0) {
      yield s;
      s -= 21;
    } else if (i % 3 == 0) {
      yield x ^ 42;
    }
    s *= i;
  }
  s &= 42;
  yield s;
}

%PrepareFunctionForOptimization(foo);
let gen = foo(4);
assertEquals(625, gen.next().value);
assertEquals(-3, gen.next().value);
assertEquals(46, gen.next().value);
assertEquals(34, gen.next().value);
assertEquals(undefined, gen.next().value);

%OptimizeFunctionOnNextCall(foo);
gen = foo(4);
assertEquals(625, gen.next().value);
assertEquals(-3, gen.next().value);
assertEquals(46, gen.next().value);
assertEquals(34, gen.next().value);
assertEquals(undefined, gen.next().value);
