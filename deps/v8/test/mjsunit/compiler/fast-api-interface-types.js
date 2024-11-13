// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file interface types used with fast API calls.

// Flags: --turbo-fast-api-calls --expose-fast-api --allow-natives-syntax --turbofan
// Flags: --no-always-turbofan
// Flags: --deopt-every-n-times=0
// Flags: --fast-api-allow-float-in-sim

const fast_c_api = new d8.test.FastCAPI();
// We create another API object to avoid migrating the map of fast_c_api
// when using it as a prototype.
const another_fast_c_api = new d8.test.FastCAPI();

function is_fast_c_api_object(obj) {
  return fast_c_api.is_fast_c_api_object(obj);
}

%PrepareFunctionForOptimization(is_fast_c_api_object);
assertTrue(is_fast_c_api_object(another_fast_c_api));
%OptimizeFunctionOnNextCall(is_fast_c_api_object);

// Test that fast_c_api is an API object wrapping a C++ one.
fast_c_api.reset_counts();
assertTrue(is_fast_c_api_object(another_fast_c_api));
assertOptimized(is_fast_c_api_object);
assertEquals(1, fast_c_api.fast_call_count());
assertEquals(0, fast_c_api.slow_call_count());

// Test that a regular JS object returns false.
fast_c_api.reset_counts();
assertFalse(is_fast_c_api_object({}));
assertOptimized(is_fast_c_api_object);
assertEquals(1, fast_c_api.fast_call_count());
assertEquals(0, fast_c_api.slow_call_count());

// Test that a subclassed object returns false.
const child = Object.create(another_fast_c_api);
fast_c_api.reset_counts();
assertFalse(is_fast_c_api_object(child));
assertOptimized(is_fast_c_api_object);
assertEquals(1, fast_c_api.fast_call_count());
assertEquals(0, fast_c_api.slow_call_count());

// Test that passing an API object of an unrelated (leaf) type
// causes the function to return false, because an object of
// FastCAPI type is expected as an argument.
const leaf_interface_obj = new d8.test.LeafInterfaceType();
fast_c_api.reset_counts();
assertFalse(is_fast_c_api_object(leaf_interface_obj));
assertOptimized(is_fast_c_api_object);
assertEquals(1, fast_c_api.fast_call_count());
assertEquals(0, fast_c_api.slow_call_count());
