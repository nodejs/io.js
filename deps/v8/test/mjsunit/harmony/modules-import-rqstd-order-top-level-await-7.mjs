// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


assertEquals(globalThis.test262, [
  '1_udir_a', '1_udir_b', '2',
]);

import 'modules-skip-1-rqstd-order-unreached-top-level-await.mjs';
import 'modules-skip-2-rqstd-order.mjs';
