// Copyright (C) 2024 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: Test if Symbol.dispose is called correctly.
includes: [asyncHelpers.js, compareArray.js]
flags: [async]
features: [explicit-resource-management]
---*/

// sync dispose method ----------------
asyncTest(async function() {
  let syncMethodValues = [];

  {
    await using x = {
      value: 1,
      [Symbol.dispose]() {
        syncMethodValues.push(42);
      }
    };
    await using y = {
      value: 1,
      [Symbol.dispose]() {
        syncMethodValues.push(43);
      }
    };
    syncMethodValues.push(44);
  }

  assert.compareArray(syncMethodValues, [44, 43, 42]);
});
