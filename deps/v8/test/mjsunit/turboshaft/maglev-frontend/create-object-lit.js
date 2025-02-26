// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function create_obj_lit(v) {
  return { 1024 : v } ;
}

%PrepareFunctionForOptimization(create_obj_lit);
assertEquals({ 1024 : 42 }, create_obj_lit(42));
assertEquals({ 1024 : 42 }, create_obj_lit(42));
%OptimizeFunctionOnNextCall(create_obj_lit);
assertEquals({ 1024 : 42 }, create_obj_lit(42));
assertOptimized(create_obj_lit);
