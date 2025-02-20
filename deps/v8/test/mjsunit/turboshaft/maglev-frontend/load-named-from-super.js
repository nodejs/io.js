// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

class B {
  foo() { return super.bar; }
}

B.prototype.__proto__ = globalThis;
globalThis.bar = "correct value";

let o = new B();

%PrepareFunctionForOptimization(B.prototype.foo);
assertEquals("correct value", o.foo());

%OptimizeFunctionOnNextCall(B.prototype.foo);
assertEquals("correct value", o.foo());
assertOptimized(B.prototype.foo);

globalThis.bar = "new value";
assertEquals("new value", o.foo());
