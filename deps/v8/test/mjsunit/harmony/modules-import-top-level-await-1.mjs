// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

let promise_resolved = false;
let m = import('modules-skip-1.mjs');
m.then(
  () => { promise_resolved = true; },
  () => { %AbortJS('Promise rejected'); });
await m;

assertEquals(promise_resolved, true);
