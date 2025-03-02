// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

import * as m from 'modules-skip-6-top-level-await.mjs';

assertEquals(m.m1.life(), m.m2.life());
