// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


let m = import('modules-skip-1.mjs');
let m_namespace = await m;

assertEquals(42, m_namespace.life());
