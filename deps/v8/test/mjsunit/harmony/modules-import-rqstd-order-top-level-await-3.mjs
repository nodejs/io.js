// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


assertEquals(globalThis.test262, [
  '1', '2_dir_a', '3', '4_dir_a', '2', '4', '2_dir_b', '4_dir_b']);

import 'modules-skip-1-rqstd-order.mjs';
import 'modules-skip-2-rqstd-order-top-level-await.mjs';
import 'modules-skip-3-rqstd-order.mjs';
import 'modules-skip-4-rqstd-order-top-level-await.mjs';
