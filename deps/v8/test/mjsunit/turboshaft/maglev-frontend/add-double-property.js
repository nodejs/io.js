// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function add_double_property(o) {
  o.x = 1.1;
  return o;
}

%PrepareFunctionForOptimization(add_double_property);
assertEquals({x:1.1}, add_double_property({}));
assertEquals({x:1.1}, add_double_property({}));
%OptimizeFunctionOnNextCall(add_double_property);
assertEquals({x:1.1}, add_double_property({}));
assertOptimized(add_double_property);
