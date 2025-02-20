// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

'use strict';

function inner() {
  return arguments;
}

function outer() {
  let phi = 1.17;
  for (let i = 0; i < 42; i++) {
    phi = phi + 33.672;
  }
  return inner(phi);
}

%PrepareFunctionForOptimization(inner);
%PrepareFunctionForOptimization(outer);
outer();
outer();

%OptimizeMaglevOnNextCall(outer);
outer();
