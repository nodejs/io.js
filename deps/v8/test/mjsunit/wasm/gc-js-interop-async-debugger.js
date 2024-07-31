// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan

// The implementation of Promises currently takes a different path (a C++
// runtime function instead of a Torque builtin) when the debugger is
// enabled, so exercise that path in this variant of the test.
d8.debugger.enable();
d8.file.execute('test/mjsunit/wasm/gc-js-interop-async.js');
