// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function delete_prop_strict(o) {
  'use strict';
  delete o.x;
}

let o = { x : 25, y : 42 };

%PrepareFunctionForOptimization(delete_prop_strict);
delete_prop_strict(o);
assertEquals({y:42}, o);

o.x = 25;
%OptimizeFunctionOnNextCall(delete_prop_strict);
delete_prop_strict(o);
assertEquals({y:42}, o);
assertOptimized(delete_prop_strict);
