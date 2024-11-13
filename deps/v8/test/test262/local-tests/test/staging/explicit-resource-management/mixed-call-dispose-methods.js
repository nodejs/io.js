// Copyright (C) 2024 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: Test if disposed methods are called correctly with mixed resources
includes: [asyncHelpers.js, compareArray.js]
flags: [async]
features: [explicit-resource-management]
---*/

// Mix of sync and async resources ----------
asyncTest(async function() {
  let mixValues = [];

  {
    await using x = {
      value: 1,
      [Symbol.asyncDispose]() {
        mixValues.push(42);
      }
    };
    using y = {
      value: 1,
      [Symbol.dispose]() {
        mixValues.push(43);
      }
    };
    mixValues.push(44);
  }

  assert.compareArray(mixValues, [44, 43, 42]);
});
