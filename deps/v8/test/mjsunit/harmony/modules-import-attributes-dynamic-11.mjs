// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

var life1;
var life2;
import('modules-skip-1.json', { with: { type: 'json' } }).then(
    namespace => life1 = namespace.default.life);

// Try loading the same module a second time.
import('modules-skip-1.json', { with: { type: 'json' } }).then(
    namespace => life2 = namespace.default.life);

%PerformMicrotaskCheckpoint();

assertEquals(42, life1);
assertEquals(42, life2);
