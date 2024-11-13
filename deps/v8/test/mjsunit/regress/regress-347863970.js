// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

const iterationCount = 10;

function test() {
  let trueValues = 0;
  let falseValues = 0;

  for (let i = 0; i < iterationCount; i++) {
    var c2 = Math.floor(Math.random() * 10) + 100;
    if (!!!(!!((c2 === 32) | (c2 === 34)) | (c2 === 92))) {
      trueValues++;
    } else {
      falseValues++;
    }
  }
  return [trueValues, falseValues];
}

%PrepareFunctionForOptimization(test);
assertEquals([iterationCount, 0], test());
%OptimizeFunctionOnNextCall(test);
assertEquals([iterationCount, 0], test());
