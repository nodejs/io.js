// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-staging

for (let v0 = 0; v0 < 5; v0++) {
    async function f1() {
      await using v13 = {
        [Symbol.asyncDispose]() {
            throw RangeError();
        },
    };
        await using v12 = {
          [Symbol.asyncDispose]() {
          },
      };
    }
    assertThrowsAsync(f1().then(), RangeError);
  }
