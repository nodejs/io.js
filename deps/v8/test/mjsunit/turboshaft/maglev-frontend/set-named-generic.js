// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function set_named_generic() {
  function f() {}
  f.prototype = undefined;
  return f;
}

%PrepareFunctionForOptimization(set_named_generic);
let before = set_named_generic();
assertEquals(undefined, before.prototype);
%OptimizeFunctionOnNextCall(set_named_generic);
let after = set_named_generic();
assertEquals(undefined, after.prototype);
assertOptimized(set_named_generic);
