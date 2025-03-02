// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbo-fast-api-calls --expose-fast-api --allow-natives-syntax --turbofan

const fast_c_api = new d8.test.FastCAPI();
assertThrows(() => fast_c_api.enforce_range_compare_i32(
  -9007199254740990, new Boolean(), {}));
