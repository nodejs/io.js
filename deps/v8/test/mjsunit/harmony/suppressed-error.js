// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --js-explicit-resource-management

(function TestSuppressedErrorAllParameters() {
    const firstError = new Error('First error');
    const secondrror = new Error('Second error');
    let suppressedError = new SuppressedError(secondrror, firstError, "Test error");
    assertEquals(secondrror, suppressedError.error);
    assertEquals(firstError, suppressedError.suppressed);
    assertEquals("Test error", suppressedError.message);
})();
