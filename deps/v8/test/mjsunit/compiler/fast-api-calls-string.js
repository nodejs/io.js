// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file excercises one byte string support for fast API calls.

// Flags: --turbo-fast-api-calls --expose-fast-api --allow-natives-syntax --turbofan
// --always-turbofan is disabled because we rely on particular feedback for
// optimizing to the fastest path.
// Flags: --no-always-turbofan
// The test relies on optimizing/deoptimizing at predictable moments, so
// it's not suitable for deoptimization fuzzing.
// Flags: --deopt-every-n-times=0
// Flags: --fast-api-allow-float-in-sim

assertThrows(() => d8.test.FastCAPI());
const fast_c_api = new d8.test.FastCAPI();

function assertSlowCall(input) {
  assertEquals(new Uint8Array(input.length), copy_string(input));
}

function assertFastCall(input) {
  const bytes = Uint8Array.from(input, c => c.charCodeAt(0));
  assertEquals(bytes, copy_string(input));
}

function copy_string(input) {
  const buffer = new Uint8Array(input.length);
  fast_c_api.copy_string(input, buffer);
  return buffer;
}

%PrepareFunctionForOptimization(copy_string);
assertSlowCall('Hello');
%OptimizeFunctionOnNextCall(copy_string);

fast_c_api.reset_counts();
assertFastCall('Hello');
assertFastCall('');
assertFastCall(['Hello', 'World'].join(''));
assertOptimized(copy_string);
assertEquals(3, fast_c_api.fast_call_count());
assertEquals(0, fast_c_api.slow_call_count());

// Fall back for twobyte strings.
fast_c_api.reset_counts();
assertSlowCall('Hello\u{10000}');
assertSlowCall('नमस्ते');
assertSlowCall(['नमस्ते', 'World'].join(''));
assertOptimized(copy_string);
assertEquals(0, fast_c_api.fast_call_count());
assertEquals(3, fast_c_api.slow_call_count());

// Fall back for cons strings.
function getTwoByteString() {
  return '\u1234t';
}
function getCons() {
  return 'hello' + getTwoByteString()
}

fast_c_api.reset_counts();
assertSlowCall(getCons());
assertOptimized(copy_string);
assertEquals(0, fast_c_api.fast_call_count());
assertEquals(1, fast_c_api.slow_call_count());

// Fall back for sliced strings.
fast_c_api.reset_counts();
function getSliced() {
  return getCons().slice(1);
}
assertSlowCall(getSliced());
assertOptimized(copy_string);
assertEquals(0, fast_c_api.fast_call_count());
assertEquals(1, fast_c_api.slow_call_count());

// Fall back for SMI and non-string inputs.
fast_c_api.reset_counts();
assertSlowCall(1);
assertSlowCall({});
assertSlowCall(new Uint8Array(1));
assertEquals(0, fast_c_api.fast_call_count());
assertEquals(3, fast_c_api.slow_call_count());
