// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --invoke-weak-callbacks

let mutex = new Atomics.Mutex();
let cond = new Atomics.Condition();
Atomics.Mutex.lockAsync(mutex, async function () {
  await Atomics.Condition.waitAsync(cond, mutex);
});
