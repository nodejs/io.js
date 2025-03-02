// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

class C {
  foo() {
    return super.nodeType;
  }
}
var c0 = new C();

%PrepareFunctionForOptimization(C.prototype.foo);

assertEquals(c0.foo(), undefined);

C.prototype.__proto__ = new d8.dom.Div();
var c1 = new C();
assertThrows(()=>c1.foo());

%OptimizeMaglevOnNextCall(C.prototype.foo);

assertThrows(()=>c1.foo());
