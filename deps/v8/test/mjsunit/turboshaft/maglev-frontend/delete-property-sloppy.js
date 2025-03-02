// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function delete_prop_sloppy(o) {
  delete o.y;
}

o = { x : 25, y : 42 };
%PrepareFunctionForOptimization(delete_prop_sloppy);
delete_prop_sloppy(o);
assertEquals({x:25}, o);

o.y = 42;
%OptimizeFunctionOnNextCall(delete_prop_sloppy);
delete_prop_sloppy(o);
assertEquals({x:25}, o);
assertOptimized(delete_prop_sloppy);
