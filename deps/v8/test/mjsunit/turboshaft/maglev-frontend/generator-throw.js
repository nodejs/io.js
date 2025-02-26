// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function* foo() {
  try {
    yield 42;
  } catch (e) {
    return 14;
  }
}

%PrepareFunctionForOptimization(foo);
let gen = foo();
assertEquals(42, gen.next().value);
assertEquals(14, gen.throw().value);
assertEquals(25, gen.return(25).value);
assertEquals(undefined, gen.next().value);

%OptimizeFunctionOnNextCall(foo);
gen = foo();
assertEquals(42, gen.next().value);
assertEquals(14, gen.throw().value);
assertEquals(25, gen.return(25).value);
assertEquals(undefined, gen.next().value);
