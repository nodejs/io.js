// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

class C {
  m() {
    return super.nodeType;
  }
}

var v1 = new C();
%PrepareFunctionForOptimization(v1.m);
v1.m(); // Undefined (inexistent feedback for nodeType access).

C.prototype.__proto__ = new d8.dom.Div();
var v2 = new C();
try {
  v2.m(); // API feedback for nodeType access with different
          // receiver and start object.
} catch {}

%OptimizeMaglevOnNextCall(v1.m);

try {
  v2.m();
} catch {}
