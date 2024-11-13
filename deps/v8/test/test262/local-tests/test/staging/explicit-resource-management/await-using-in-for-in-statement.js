// Copyright (C) 2024 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: Test if disposed methods are called correctly in for-in statement.
includes: [asyncHelpers.js, compareArray.js]
flags: [async]
features: [explicit-resource-management]
---*/

// ForInStatement --------------
asyncTest(async function() {
  let forInStatementValues = [];

  for (let i in [0, 1]) {
    await using x = {
      value: i,
      [Symbol.asyncDispose]() {
        forInStatementValues.push(this.value);
      }
    };
  }
  forInStatementValues.push('2');

  assert.compareArray(forInStatementValues, ['0', '1', '2']);
});
