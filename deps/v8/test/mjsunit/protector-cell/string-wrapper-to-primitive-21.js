// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev

assertTrue(%StringWrapperToPrimitiveProtector());

function createStringWrapper(newTarget) {
  return Reflect.construct(String, ["content"], newTarget);
}

%PrepareFunctionForOptimization(createStringWrapper);
createStringWrapper(String);
%OptimizeMaglevOnNextCall(createStringWrapper);

class X {
  [Symbol.toPrimitive]() {
    return 'new';
  }
}

assertTrue(%StringWrapperToPrimitiveProtector());

let x = createStringWrapper(X);

// The protector is also invalidated when we call Reflect.construct from Maglev code.
assertFalse(%StringWrapperToPrimitiveProtector());

assertEquals('got new', 'got ' + x);
