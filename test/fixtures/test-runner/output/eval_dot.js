// Flags: --test-reporter=dot
'use strict';
eval(`
  const { test } = require('node:test');

  test('passes');
  test('fails', () => {
    throw new Error('fail');
  });
`);
