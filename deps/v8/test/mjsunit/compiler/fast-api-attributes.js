// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file excercises basic fast API calls and enables fuzzing of this
// functionality.

// Flags: --turbo-fast-api-calls --expose-fast-api --allow-natives-syntax --turbofan
// --always-turbofan is disabled because we rely on particular feedback for
// optimizing to the fastest path.
// Flags: --no-always-turbofan
// The test relies on optimizing/deoptimizing at predictable moments, so
// it's not suitable for deoptimization fuzzing.
// Flags: --deopt-every-n-times=0
// Flags: --fast-api-allow-float-in-sim

const fast_c_api = new d8.test.FastCAPI();

function get() {
    return fast_c_api.fast_attribute;
}

function set(value) {
    fast_c_api.fast_attribute = value;
}

%PrepareFunctionForOptimization(set);
set(12);
%OptimizeFunctionOnNextCall(set);
%PrepareFunctionForOptimization(get);
assertEquals(12, get());
%OptimizeFunctionOnNextCall(get);


fast_c_api.reset_counts();
set(21);
assertOptimized(set);
assertEquals(1, fast_c_api.fast_call_count());
assertEquals(0, fast_c_api.slow_call_count());

fast_c_api.reset_counts();
assertEquals(21, get());
assertOptimized(get);
assertEquals(1, fast_c_api.fast_call_count());
assertEquals(0, fast_c_api.slow_call_count());
