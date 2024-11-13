// Copyright (C) 2024 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: Test if disposed methods are called correctly in switch cases.
includes: [asyncHelpers.js, compareArray.js]
flags: [async]
features: [explicit-resource-management]
---*/

// CaseBlock --------------
asyncTest(async function() {
  let caseBlockValues = [];

  let label = 1;
  switch (label) {
    case 1:
      await using x = {
        value: 1,
        [Symbol.asyncDispose]() {
          caseBlockValues.push(42);
        }
      };
  }
  caseBlockValues.push(43);

  assert.compareArray(caseBlockValues, [42, 43]);
});
