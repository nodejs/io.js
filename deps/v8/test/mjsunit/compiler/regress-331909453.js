// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-fast-api

const fast_c_api = new d8.test.FastCAPI();

function f() {
  return fast_c_api.add_all_sequence(false, {});
}

%PrepareFunctionForOptimization(f);
assertThrows(() => f(), Error,
             "This method expects an array as a first argument.");

%OptimizeFunctionOnNextCall(f);
assertThrows(() => f(), Error,
             "This method expects an array as a first argument.");
