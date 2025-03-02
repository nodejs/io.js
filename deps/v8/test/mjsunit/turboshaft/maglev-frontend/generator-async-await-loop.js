// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

async function foo(x) {
  let res = 0;
  for (let i = 0; i < x; i++) {
    if (i % 2 == 0) {
      res += await i;
    }
  }
  return res;
}

%PrepareFunctionForOptimization(foo);
foo(4).then(function(result) {
  assertEquals(2, result);

  %OptimizeFunctionOnNextCall(foo);
  foo(4).then((result) => assertEquals(2, result));
});
