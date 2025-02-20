// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function make_obj(x) {
  return { 0 : x };
};

%PrepareFunctionForOptimization(make_obj);
assertEquals({0:42}, make_obj(42));
assertEquals({0:42}, make_obj(42));
%OptimizeFunctionOnNextCall(make_obj);
assertEquals({0:42}, make_obj(42));
assertOptimized(make_obj);
