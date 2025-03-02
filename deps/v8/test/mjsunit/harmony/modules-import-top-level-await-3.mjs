// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


let m1 = import('modules-skip-1.mjs');
let m1_namespace = await m1;

let m2 = import('modules-skip-3.mjs');
let m2_namespace = await m2;

assertEquals(42, m1_namespace.life());
assertEquals('42', m2_namespace.stringlife);
