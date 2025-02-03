// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --js-decorators

(function TestAutoAccessorsReparsing() {
  const y = null;
  // A spread args error in the initializer forces the class scope
  // (and the auto-accessor) to be reparsed.
  class C {
    accessor x = (() => {})(1, ...y);
  }
  // Assert an error is thrown, meaning that the reparsing was performed
  // correctly.
  assertThrows(() => {
    let c = new C();
  });
})();
