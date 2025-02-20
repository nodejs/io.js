// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


import "modules-skip-async-cycle-start.mjs"

assertEquals(globalThis.test_order, [
  '2', 'async before', 'async after', '1',
  '3', 'start',
]);
