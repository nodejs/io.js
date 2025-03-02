// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --wasm-staging

(async function () {
  let __v_3 = function () {
  };
  let __v_4 = new FinalizationRegistry(__v_3);
  (function () {
    let __v_7 = {};
    __v_4.register(__v_7);
  })();
  await gc();
})();
