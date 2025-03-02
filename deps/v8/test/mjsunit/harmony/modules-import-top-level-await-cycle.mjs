// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

import * as m1 from 'modules-skip-1-top-level-await-cycle.mjs'
import * as m2 from 'modules-skip-2-top-level-await-cycle.mjs'
import * as m3 from 'modules-skip-3-top-level-await-cycle.mjs'

assertSame(m1.m1.m.m.life, m1.m2.m.m.life);
assertSame(m1.m1.m.m.life, m2.m.m.life);
assertSame(m1.m1.m.m.life, m3.m.m.life);

let m4 = await import('modules-skip-1.mjs');
assertSame(m1.m1.m.m.life, m4.life);
