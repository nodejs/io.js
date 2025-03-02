// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {foo} from "./modules-skip-8.mjs";
assertEquals(42, foo.default);
assertEquals(1, foo.get_a());
