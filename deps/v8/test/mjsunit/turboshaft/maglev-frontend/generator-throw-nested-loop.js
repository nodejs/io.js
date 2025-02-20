// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function* foo(x) {
  for (let i = 0; i < x; i++) {
    try {
      for (let j = 0; j < x; j++) {
        if (j % 2 == 0) {
          // The .next resume after this yield is inside the inner loop, while
          // the .throw goes to the catch, which is in the outer loop.
          yield i * 10 + j;
        }
      }
    } catch (e) {
      i++;
    }
  }
  yield 42;
}

%PrepareFunctionForOptimization(foo);
let gen = foo(4);
assertEquals(0, gen.next().value);
assertEquals(2, gen.next().value);
assertEquals(20, gen.throw().value);
assertEquals(22, gen.next().value);
assertEquals(30, gen.next().value);
assertEquals(42, gen.throw().value);
assertEquals(undefined, gen.next().value);

%OptimizeFunctionOnNextCall(foo);
gen = foo(4);
assertEquals(0, gen.next().value);
assertEquals(2, gen.next().value);
assertEquals(20, gen.throw().value);
assertEquals(22, gen.next().value);
assertEquals(30, gen.next().value);
assertEquals(42, gen.throw().value);
assertEquals(undefined, gen.next().value);
