// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function* foo(x) {
  let s = 0;
  for (let i = 0; i < x + 2; i++) {
    s += 2;
    try {
      yield i + 17;
      s += 1;
      // The following `yield` looks like it's out of the loop when looking at
      // the regular .next() path, because it's predecessor is outside of the
      // loop (we always jump on it through a resume), and because it's a
      // `yield`, it has no successors.
      // However, if we resume with .throw() after the yield,
      yield i * 14;
      s += 3;
    } catch (e) {
      i++;
    }
    s += 4;
    if (i < 2) {
      yield i - 1;
    }
    s += 5;
  }
  yield s;
}

%PrepareFunctionForOptimization(foo);
let gen = foo(2);
assertEquals(17, gen.next().value);
assertEquals(0, gen.throw().value);
assertEquals(19, gen.next().value);
assertEquals(28, gen.next().value);
assertEquals(23, gen.throw().value);
assertEquals(undefined, gen.next().value);

%OptimizeFunctionOnNextCall(foo);
gen = foo(2);
assertEquals(17, gen.next().value);
assertEquals(0, gen.throw().value);
assertEquals(19, gen.next().value);
assertEquals(28, gen.next().value);
assertEquals(23, gen.throw().value);
assertEquals(undefined, gen.next().value);


// B1 --> header = <none>
// B2 --> header = <none>
// B3 --> header = <none>
// B4 --> header = <none>
// B5 --> header = <none>
// B6 --> header = <none>
// B7 --> header = <none>
// B8 --> header = <none>
// B9 --> header = <none>
// B10 --> header = <none>
// B11 --> header = <none>
// B12 --> header = <none>
// B13 --> header = <none>
// B14 --> header = <none>
// B15 --> header = <none>
// B16 --> header = <none>
// B17 --> header = <none>
// B18 --> header = <none>
// B19 --> header = B9
// B20 --> header = <none>
// B21 --> header = B9
// B22 --> header = <none>
// B23 --> header = <none>
// B24 --> header = B9
// B25 --> header = B9
// B26 --> header = B9
// B27 --> header = <none>
// B28 --> header = <none>
// B29 --> header = <none>
// B30 --> header = <none>
// B31 --> header = <none>
