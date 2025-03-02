// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


import "modules-skip-async-cycle-2.mjs";
import "modules-skip-async-cycle-leaf.mjs";

if (globalThis.test_order === undefined) {
  globalThis.test_order = [];
}
globalThis.test_order.push('1');

