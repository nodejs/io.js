// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function load_global() {
  return o;
}

this.__defineGetter__("o", function() { return 42; });

%PrepareFunctionForOptimization(load_global);
assertEquals(o, load_global());
%OptimizeFunctionOnNextCall(load_global);
assertEquals(o, load_global());
assertOptimized(load_global);
