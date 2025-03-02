// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --expose-async-hooks

(async function () {
    await gc({ execution: 'async' });
    d8.terminate();
    const foo = new FinalizationRegistry();
})();
