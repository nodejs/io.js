// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-source-phase-imports

assertPromiseResult(async function () {
  var error;
  try {
    await import.source('not-found.wasm');
    assertUnreachable();
  } catch (e) {
    error = e;
  }
  assertInstanceof(error, Error);
  assertStringContains(error.message, 'Error reading module');
}());
