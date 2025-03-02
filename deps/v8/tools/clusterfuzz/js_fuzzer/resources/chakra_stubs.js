// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WasmSpec tests in the chakra testsuite use a lot of test(() => ...)
// patterns.
function test(fun) {
  try {
    fun();
  } catch(e) {}
}
