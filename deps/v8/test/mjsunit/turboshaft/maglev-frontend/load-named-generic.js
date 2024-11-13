// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

let v1 = {};

function load_named_generic() {
  let v2 = v1.Intl;
  try {
    return v2.supportedValuesOf();
  } catch(e) {
    // Stringifying the exception for easier comparison.
    return "123" + e + v2;
  }
}

%PrepareFunctionForOptimization(load_named_generic);
let before = load_named_generic();
%OptimizeFunctionOnNextCall(load_named_generic);
let after = load_named_generic();
assertEquals(before, after);
assertOptimized(load_named_generic);
