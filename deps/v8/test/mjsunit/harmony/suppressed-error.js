// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --js-staging

(function TestSuppressedErrorAllParameters() {
  const firstError = new Error('First error');
  const secondError = new Error('Second error');
  let suppressedError =
      new SuppressedError(secondError, firstError, 'Test error');
  assertEquals(secondError, suppressedError.error);
  assertEquals(firstError, suppressedError.suppressed);
  assertEquals('Test error', suppressedError.message);
})();

(function TestSuppressedErrorNewTarget() {
  class MySuppressedError extends SuppressedError {};
  const firstError = new Error('First error');
  const secondError = new Error('Second error');
  let suppressedError =
      new MySuppressedError(secondError, firstError, 'Test error');
  assertEquals(secondError, suppressedError.error);
  assertEquals(firstError, suppressedError.suppressed);
  assertEquals("Test error", suppressedError.message);
})();
