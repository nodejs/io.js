// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

const ab = new ArrayBuffer(100, {maxByteLength: 200});
var dv = new DataView(ab, 0, 8); // "var" is important

function foo() {
  return dv.getInt8(0);
}

%PrepareFunctionForOptimization(foo);
foo();

%OptimizeFunctionOnNextCall(foo);
foo();

ab.resize(0);

try {
 foo();
} catch {
}
