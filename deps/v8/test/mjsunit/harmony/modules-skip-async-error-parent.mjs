// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import './modules-skip-async-error-parent-static1.mjs';
import './modules-skip-async-error-parent-static2.mjs';

await {};

throw new Error('aaa');
