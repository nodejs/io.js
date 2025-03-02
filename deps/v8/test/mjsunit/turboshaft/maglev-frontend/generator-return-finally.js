// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

let glob = 0;

function* foo() {
  yield 11;
  try {
    yield 42;
    yield 15;
  } catch(e) {
  } finally {
    glob++;
  }
}

%PrepareFunctionForOptimization(foo);
let gen = foo();
assertEquals(11, gen.next().value);
assertEquals(42, gen.next().value);
assertEquals(19, gen.return(19).value);
assertEquals(undefined, gen.next().value);
assertEquals(1, glob);

%OptimizeFunctionOnNextCall(foo);
gen = foo();
assertEquals(11, gen.next().value);
assertEquals(42, gen.next().value);
assertEquals(19, gen.return(19).value);
assertEquals(undefined, gen.next().value);
assertEquals(2, glob);
