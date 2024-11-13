// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct

(function TestAsyncLockProxyAsCallback() {
  const callable = new Proxy(eval, {});
  const mutex = new Atomics.Mutex;
  const locAsyncPromise = Atomics.Mutex.lockAsync(mutex, callable);
  let thenExecuted = false;
  locAsyncPromise.then(() => {thenExecuted = true}, () => {});

  const asyncLoop = () => {
    if (!thenExecuted) {
      setTimeout(asyncLoop, 0);
      return;
    }
  };
  asyncLoop();
})();
