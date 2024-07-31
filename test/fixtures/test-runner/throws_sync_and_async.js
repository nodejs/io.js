'use strict';
const { test } = require('node:test');

test('fails and schedules more work', () => {
  setTimeout(() => {
    throw new Error('this should not have a chance to be thrown');
  }, 1000);

  throw new Error('fails');
});
