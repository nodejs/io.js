// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function test(val) {
  function func() {
    class Class extends func {
      static {
        super.m();
      }
      // An instance initializer between two static initializers is
      // needed to trigger the regression.
      [this] = val;
      static 1;
    }
  }
  func();
}
assertThrows(test);
