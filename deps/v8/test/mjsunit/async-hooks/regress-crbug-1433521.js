// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-async-hooks

let arr = [];
let hook = async_hooks.createHook({
  init() {
    d8.terminate();
    arr.push();
  }
});
hook.enable();

async function async_f() {}
async_f().then();
async_hooks.createHook({init() {}});
