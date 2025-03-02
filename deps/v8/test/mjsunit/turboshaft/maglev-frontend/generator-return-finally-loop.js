// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

let glob = 0;

function* foo(x) {
  for (let i = 0; i < x; i++) {
    try {
      yield i * 10;
      yield i + 2;
    } catch(e) {
    } finally {
      glob = i;
    }
    i++;
  }
}

%PrepareFunctionForOptimization(foo);
let gen = foo(8);
assertEquals(0, gen.next().value);
assertEquals(2, gen.next().value);
assertEquals(20, gen.next().value);
assertEquals(4, gen.next().value);
assertEquals(19, gen.return(19).value);
assertEquals(undefined, gen.next().value);
assertEquals(2, glob);

%OptimizeFunctionOnNextCall(foo);
gen = foo(8);
assertEquals(0, gen.next().value);
assertEquals(2, gen.next().value);
assertEquals(20, gen.next().value);
assertEquals(4, gen.next().value);
assertEquals(19, gen.return(19).value);
assertEquals(undefined, gen.next().value);
assertEquals(2, glob);
