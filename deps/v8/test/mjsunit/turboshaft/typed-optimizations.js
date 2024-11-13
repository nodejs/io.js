// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --turboshaft --allow-natives-syntax --turboshaft-typed-optimizations

function add1(x) {
  let a = x ? 3 : 7;      // a = {3, 7}
  let r = -1;             // r = {-1}
  if (a < 5)              // then: a = {3}
    r = a + 2;            // r = {5}
  else                    // else: a = {7}
    r = a - 2;            // r = {5}
  const result = r + 1;   // result = {6}
  // TODO(nicohartmann@): When we have a platform independent way to do that,
  // add a %CheckTurboshaftTypeOf to verify the type.
  return result;
}

function loop1(x) {
  let a = x ? 3 : 7;
  let result = 1;
  for(let i = 0; result < 1000; ++i) {
    result += a;
  }
  return result > 500;
}

function loop2(x) {
  let a = x ? 3 : 7;
  let result = 0;
  for(let i = 0; i < a; ++i) {
    result = i + i;
  }
  return result < 100;
}


let targets = [
  add1,
  loop1,
  loop2,
];
for(let f of targets) {
  %PrepareFunctionForOptimization(f);
  const expected_true = f(true);
  const expected_false = f(false);
  %OptimizeFunctionOnNextCall(f);
  assertEquals(expected_true, f(true));
  assertEquals(expected_false, f(false));
}
