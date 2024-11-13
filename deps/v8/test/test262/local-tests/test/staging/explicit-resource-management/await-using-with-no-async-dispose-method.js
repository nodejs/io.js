// Copyright (C) 2024 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: Test if exception is throwon when dispose method is missing.
includes: [asyncHelpers.js]
flags: [async]
features: [explicit-resource-management]
---*/

// Lack of dispose method --------
asyncTest(async function() {
  let values = [];

  async function TestUsingWithoutDisposeMethod() {
    {
      await using x = {value: 1};
      values.push(43);
    }
  };
  await assert.throwsAsync(
      TypeError, () => TestUsingWithoutDisposeMethod(), 'No dispose method');
});
