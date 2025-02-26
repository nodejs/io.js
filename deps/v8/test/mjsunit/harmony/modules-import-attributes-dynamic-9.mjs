// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

var life;
import('modules-skip-1.mjs', { with: undefined }).then(
    namespace => life = namespace.life());

%PerformMicrotaskCheckpoint();

assertEquals(42, life);
