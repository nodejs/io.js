// Copyright (C) 2024 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: Test if disposed methods are called correctly in for statements.
includes: [asyncHelpers.js, compareArray.js]
flags: [async]
features: [explicit-resource-management]
---*/

// ForStatement --------------
asyncTest(async function() {
  let forStatementValues = [];

  for (let i = 0; i < 3; i++) {
    await using x = {
      value: i,
      [Symbol.asyncDispose]() {
        forStatementValues.push(this.value);
      }
    };
  }
  forStatementValues.push(3);

  assert.compareArray(forStatementValues, [0, 1, 2, 3]);
});
