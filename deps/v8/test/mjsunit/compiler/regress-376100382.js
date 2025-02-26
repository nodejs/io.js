// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

Object.prototype["10"] = "unreachable";
function foo() {
  return new Int32Array().__proto__["10"];
}

%PrepareFunctionForOptimization(foo);
assertEquals("unreachable", foo());
assertEquals("unreachable", foo());

%OptimizeFunctionOnNextCall(foo);
assertEquals("unreachable", foo());
