// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function boom() {
  // Trigger stack overflow.
  try {
    f2();
  } catch(e4) {
  }
  // Continue with exception.
  const options = {
    "initial": 1,
  };
  // Re-enter JS from Memory ctor to trigger crash.
  Object.defineProperty(options, "shared", { "get": () => {} });
  if (typeof WebAssembly === "object") {
    new WebAssembly.Memory(options);
  }
}
performance.measureMemory();
Object.defineProperty(Object.prototype, "then", {"get": boom });
