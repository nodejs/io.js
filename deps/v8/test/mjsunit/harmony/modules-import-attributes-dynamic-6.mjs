// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

var life;
import('modules-skip-1.json', { with: { type: 'json', notARealAssertion: 'value' } }).then(
    namespace => life = namespace.default.life);

var life2;
import('modules-skip-1.json', { with: { 0: 'value', type: 'json' } }).then(
    namespace => life2 = namespace.default.life);

%PerformMicrotaskCheckpoint();

assertEquals(42, life);
assertEquals(42, life2);
