'use strict';
const { test } = require('node:test');

test('test that aborts', (t, done) => {
  t.after(() => {
    // This should still run.
    console.log('AFTER');
  });

  setImmediate(() => {
    // This creates an uncaughtException, which aborts the test.
    throw new Error('boom');
  });
});
