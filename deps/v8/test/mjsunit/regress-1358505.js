// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

(function Test_OOB() {
  function f() {
    try {
      const buffer = new ArrayBuffer(42, {'maxByteLength': 42});
      const view = new DataView(buffer, 0, 42);
      // Resize the buffer to smaller than the view.
      buffer.resize(20);
      // Any access in the view should throw.
      view.setInt8(11, 0xab);
      return 'did not prevent out-of-bounds access';
    } catch (e) {
      return 'ok';
    }
  }

  %PrepareFunctionForOptimization(f);
  assertEquals('ok', f());
  assertEquals('ok', f());
  %OptimizeFunctionOnNextCall(f);
  assertEquals('ok', f());
  assertEquals('ok', f());
}());

(function Test_OOB_WithOffset() {
  function f() {
    try {
      const buffer = new ArrayBuffer(42, {'maxByteLength': 42});
      const view = new DataView(buffer, 30, 42);
      // Resize the buffer to smaller than the view.
      buffer.resize(40);
      // Any access in the view should throw.
      view.setInt8(11, 0xab);
      return 'did not prevent out-of-bounds access';
    } catch (e) {
      return 'ok';
    }
  }

  %PrepareFunctionForOptimization(f);
  assertEquals('ok', f());
  assertEquals('ok', f());
  %OptimizeFunctionOnNextCall(f);
  assertEquals('ok', f());
  assertEquals('ok', f());
}());
