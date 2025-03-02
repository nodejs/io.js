// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


assertEquals(globalThis.test262, [
  '1_dir_a', '2_dir_a', '3_dir_a', '4_dir_a',
  '1', '2', '3', '4',
  '1_dir_b', '2_dir_b', '3_dir_b', '4_dir_b',
  '1_ind', '2_ind', '3_ind', '4_ind',
]);

import 'modules-skip-1-rqstd-order-indirect-top-level-await.mjs';
import 'modules-skip-2-rqstd-order-indirect-top-level-await.mjs';
import 'modules-skip-3-rqstd-order-indirect-top-level-await.mjs';
import 'modules-skip-4-rqstd-order-indirect-top-level-await.mjs';
