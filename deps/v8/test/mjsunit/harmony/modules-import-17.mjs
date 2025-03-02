// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

var ns;
import('modules-skip-13.mjs').then(x => ns = x);
%PerformMicrotaskCheckpoint();
assertEquals(42, ns.default);
assertEquals(ns, ns.self);
