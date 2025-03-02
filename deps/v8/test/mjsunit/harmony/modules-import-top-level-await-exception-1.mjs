// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --ignore-unhandled-promises

let ran = false;
let m = import('modules-skip-2.mjs');
await m.then(
  () => {
    assertUnreachable();
  },
  (e) => {
    assertEquals(e.message, '42 is not the answer');
    ran = true;
  });

assertEquals(ran, true);
