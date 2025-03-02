// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function foo() {
  return slot;
}

%PrepareFunctionForOptimization(foo);
%OptimizeFunctionOnNextCall(foo);
try { foo(); } catch {}

let slot = 1;
function bar(arg) {
  slot = eval("~(" + arg + " - 0.01)");
}
bar(0x80000000 + 0.12345);
assertEquals(2147483647, foo());
