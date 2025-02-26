// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function store_glob() {
  glob_a = 42;
}

%PrepareFunctionForOptimization(store_glob);
store_glob();
assertEquals(glob_a, 42);
glob_a = 25;
%OptimizeFunctionOnNextCall(store_glob);
store_glob();
assertEquals(glob_a, 42);
assertOptimized(store_glob);
