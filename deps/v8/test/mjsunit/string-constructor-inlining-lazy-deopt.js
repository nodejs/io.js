// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function foo(a) {
  return new String(a);
}

const obj = {
  [Symbol.toPrimitive]() {
    %DeoptimizeFunction(foo);
    return "I'm a very special object";
  },
};

%PrepareFunctionForOptimization(foo);
assertEquals(new String("I'm a very special object"), foo(obj));

%OptimizeFunctionOnNextCall(foo);
assertEquals(new String("I'm a very special object"), foo(obj));
