// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

class C extends Array { };

function new_derived_obj() {
  new C();
}

%PrepareFunctionForOptimization(new_derived_obj);
let c = new_derived_obj();
%OptimizeFunctionOnNextCall(new_derived_obj);
assertEquals(c, new_derived_obj());
assertOptimized(new_derived_obj);
