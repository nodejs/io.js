// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --harmony --no-always-turbofan --maglev

function assertMaglevved(f) {
  assertTrue(isMaglevved(f));
}

function f(x) {
  return x[2];
}

let buff = new ArrayBuffer(1024);
let arr = new Int32Array(buff);
arr[2] = 42;

%PrepareFunctionForOptimization(f);
assertEquals(42, f(arr));

%OptimizeMaglevOnNextCall(f);
assertEquals(42, f(arr));

assertMaglevved(f);
// Detaching {buff} will cause {f} to deoptimize thanks to the protector.
buff.transfer();
assertUnoptimized(f);

assertEquals(undefined, f(arr));

let buff2 = new ArrayBuffer(1024);
let arr2 = new Int32Array(buff2);
arr2[2] = 42;

// Re-optimizing {f} (which a fresh feedback), now that the protector for
// detached array buffer doesn't hold anymore.
%DeoptimizeFunction(f);
%ClearFunctionFeedback(f);
%PrepareFunctionForOptimization(f);
assertEquals(42, f(arr2));
%OptimizeMaglevOnNextCall(f);
assertEquals(42, f(arr2));
assertMaglevved(f);

// The protector doesn't hold anymore, so detaching buff2 shouldn't deopt {f}.
buff2.transfer();
assertMaglevved(f);

// The runtime check will call a deopt since {buff2} has been detached.
assertEquals(undefined, f(arr2));
assertUnoptimized(f);
