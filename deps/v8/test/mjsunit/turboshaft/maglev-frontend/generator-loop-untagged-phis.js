// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

// Note that loop phis of resumable loops cannot currently be untagged, because
// they always have a `GeneratorRestoreRegister` input, which prevents
// untagging. Still, this test is meant more as a "future" test: if we can ever
// untag loop phis in resumable loops, then this test will help make sure that
// it works correctly.

function* foo(n) {
  let f64 = 3.45;
  let i32 = 17;
  for (let i = 0; i < n; i++) {
    f64 += 4.45;
    i32 += 4;
    if (i % 2 == 0) {
      yield i;
    }
  }
  yield f64;
  yield i32;
}

%PrepareFunctionForOptimization(foo);
let gen = foo(4);
assertEquals(0, gen.next().value);
assertEquals(2, gen.next().value);
assertEquals(21.25, gen.next().value);
assertEquals(33, gen.next().value);
assertEquals(undefined, gen.next().value);

%OptimizeFunctionOnNextCall(foo);
gen = foo(4);
assertEquals(0, gen.next().value);
assertEquals(2, gen.next().value);
assertEquals(21.25, gen.next().value);
assertEquals(33, gen.next().value);
assertEquals(undefined, gen.next().value);
