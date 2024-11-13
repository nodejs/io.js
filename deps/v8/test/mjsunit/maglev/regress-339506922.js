// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

var caught = 0;
var N = 2000

function bar(x) {
  return x === parseInt();
}

function foo(x) {
  if (x === undefined || x === null) return false;
  return DoestExist.call();
}

%PrepareFunctionForOptimization(bar);
%PrepareFunctionForOptimization(foo);

for (let i = 0; i < N; i++) {
  try {
    foo(!bar());
  } catch (e) {
    caught++;
  }
}
assertEquals(caught, N);
