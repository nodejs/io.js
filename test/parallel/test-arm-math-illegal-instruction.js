'use strict';
require('../common');
const { test } = require('node:test');

// This test ensures Math functions don't fail with an "illegal instruction"
// error on ARM devices (primarily on the Raspberry Pi 1)
// See https://github.com/nodejs/node/issues/1376
// and https://code.google.com/p/v8/issues/detail?id=4019

// Iterate over all Math functions
test('Iterate over all Math functions', () => {
  Object.getOwnPropertyNames(Math).forEach((functionName) => {
    if (!/[A-Z]/.test(functionName)) {
      // The function names don't have capital letters.
      Math[functionName](-0.5);
    }
  });
});
