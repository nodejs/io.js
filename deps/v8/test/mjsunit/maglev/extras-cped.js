// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

const {
  getExtrasBindingObject,
  getContinuationPreservedEmbedderDataViaAPIForTesting,
} = d8;
const {
  getContinuationPreservedEmbedderData,
  setContinuationPreservedEmbedderData,
} = getExtrasBindingObject();

function testOpt(v) {
  setContinuationPreservedEmbedderData(v);
  return getContinuationPreservedEmbedderData();
}

const runTestOpt = (v) => {
  const data = testOpt(v);
  assertEquals(data, v);
  assertEquals(getContinuationPreservedEmbedderDataViaAPIForTesting(), v);
};

%PrepareFunctionForOptimization(testOpt);

runTestOpt(5);
runTestOpt(5.5);
runTestOpt({});

%OptimizeMaglevOnNextCall(testOpt);

runTestOpt(5);
runTestOpt(5.5);
runTestOpt({});

assertTrue(isMaglevved(testOpt));
