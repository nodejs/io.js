// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function* foo(x) {
  yield 5;
  yield 7;
  if (x) {
    yield 9;
  }
}

%PrepareFunctionForOptimization(foo);
let gen = foo(true);
assertEquals(5, gen.next().value);
assertEquals(7, gen.next().value);
assertEquals(9, gen.next().value);
assertEquals(undefined, gen.next().value);

gen = foo(false);
assertEquals(5, gen.next().value);
assertEquals(7, gen.next().value);
assertEquals(undefined, gen.next().value);
assertEquals(undefined, gen.next().value);

%OptimizeFunctionOnNextCall(foo);
gen = foo(true);
assertEquals(5, gen.next().value);
assertEquals(7, gen.next().value);
assertEquals(9, gen.next().value);
assertEquals(undefined, gen.next().value);

gen = foo(false);
assertEquals(5, gen.next().value);
assertEquals(7, gen.next().value);
assertEquals(undefined, gen.next().value);
assertEquals(undefined, gen.next().value);
