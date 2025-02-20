// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


assertEquals(globalThis.test262, [
  '1', '2_dir_a', '3_dir_a', '4',
  '2', '3', '2_dir_b', '3_dir_b',
  '2_ind',
]);

import 'modules-skip-1-rqstd-order.mjs';
import 'modules-skip-2-rqstd-order-indirect-top-level-await.mjs';
import 'modules-skip-3-rqstd-order-top-level-await.mjs';
import 'modules-skip-4-rqstd-order.mjs';
