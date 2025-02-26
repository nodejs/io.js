// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {a, b} from "./modules-skip-9.mjs";
assertSame(a, b);
assertEquals(42, a.default);
assertEquals(1, a.a);
