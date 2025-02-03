// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-use-ic
// Flags: --maglev-escape-analysis

function foo() {
  // --no-use-ic guarantees a soft deopt here, so the allocation goes
  // to deopt-info.
  return [NaN,, NaN];
}

%PrepareFunctionForOptimization(foo);
foo();
%OptimizeMaglevOnNextCall(foo);

// When accessing the hole, we get an undefined.
assertEquals(foo()[1], undefined);
// We deopt.
assertFalse(isOptimized(foo));
