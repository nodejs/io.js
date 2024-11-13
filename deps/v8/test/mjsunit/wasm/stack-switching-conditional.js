// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// We pick a small stack size to run the stack overflow test quickly, but big
// enough to run all the tests.
//
// Flags: --allow-natives-syntax
// Flags: --expose-gc --wasm-stack-switching-stack-size=100


// Enable the JSPI flag conditionally (as in a Chrome Origin Trial),
// then run existing stack-switching tests.
d8.test.enableJSPI();
d8.file.execute("test/mjsunit/wasm/stack-switching.js");

// Test that nothing blows up if we call this multiple times.
d8.test.installConditionalFeatures();
