// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function load_global_inside_typeof() {
  return typeof o;
}

this.__defineGetter__("o", function() { return 42; });

%PrepareFunctionForOptimization(load_global_inside_typeof);
assertEquals("number", load_global_inside_typeof());
%OptimizeFunctionOnNextCall(load_global_inside_typeof);
assertEquals("number", load_global_inside_typeof());
assertOptimized(load_global_inside_typeof);
