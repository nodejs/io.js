// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

// There is currently only one node in Maglev that can lazy deopt with multiple
// return values: a builtin call to FindNonDefaultConstructorOrConstruct.
// This test makes sure that such lazy deopt works.

let deopt = false;
let o = new Proxy(function() {}, {
  construct(target,args) {
    if (deopt) %DeoptimizeFunction(X);
    return new target(...args)}
});
class X extends o {
  constructor() { super(); }
}

%PrepareFunctionForOptimization(X);
let val = new X;
assertEquals(val, new X);
%OptimizeFunctionOnNextCall(X);
assertEquals(val, new X);
assertOptimized(X);

deopt = true;
assertEquals(val, new X);
assertUnoptimized(X);
