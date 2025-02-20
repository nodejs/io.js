// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

var result;
import('modules-skip-1.json', { with: { type: 'notARealType' } }).then(
    () => assertUnreachable('Should have failed due to bad module type'),
    error => result = error.message);

%PerformMicrotaskCheckpoint();

assertEquals('Invalid module type was asserted', result);
