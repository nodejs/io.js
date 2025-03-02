// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

let result1;
let result2;

let badAssertProxy1 = new Proxy({}, { ownKeys() { throw "bad ownKeys!"; } });
import('./modules-skip-1.mjs', { with: badAssertProxy1 }).then(
    () => assertUnreachable('Should have failed due to throwing ownKeys'),
    error => result1 = error);

let badAssertProxy2 = new Proxy(
    {foo: "bar"},
    { getOwnPropertyDescriptor() { throw "bad getOwnPropertyDescriptor!"; } });
import('./modules-skip-1.mjs', { with: badAssertProxy2 }).then(
    () => assertUnreachable(
        'Should have failed due to throwing getOwnPropertyDescriptor'),
    error => result2 = error);

%PerformMicrotaskCheckpoint();

assertEquals('bad ownKeys!', result1);
assertEquals('bad getOwnPropertyDescriptor!', result2);
