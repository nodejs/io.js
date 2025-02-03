// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

// Generate a function {f} containing a large array literal of doubles.
eval("function create_arr_lit() { return [" + String("0.1,").repeat(65535) + "] }");

%PrepareFunctionForOptimization(create_arr_lit);

// Running the function once will initialize the boilerplate.
assertEquals(65535, create_arr_lit().length);

// Running the function again will perform cloning.
assertEquals(65535, create_arr_lit().length);

let expected = create_arr_lit();

// Running the function as optimized code next.
%OptimizeFunctionOnNextCall(create_arr_lit);
assertEquals(expected, create_arr_lit());
assertOptimized(create_arr_lit);
