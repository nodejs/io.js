// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function store_field(o, v) {
  o.x = 17; // Tagged field, no write barrier
  o.y = v; // Tagged field, with write barrier
  o.z = 12.29; // Double field
  return o;
}

let o = { x : 42, y : 10, z : 14.58 };

%PrepareFunctionForOptimization(store_field);
assertEquals({ x : 17, y : undefined, z : 12.29 }, store_field(o));
o = { x : 42, y : 10, z : 14.58 }; // Resetting {o}
%OptimizeFunctionOnNextCall(store_field);
assertEquals({ x : 17, y : undefined, z : 12.29 }, store_field(o));
assertOptimized(store_field);
