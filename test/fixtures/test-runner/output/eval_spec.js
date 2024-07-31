// Flags: --test-reporter=spec
'use strict';
eval(`
  const { test } = require('node:test');

  test('passes');
  test('fails', () => {
    throw new Error('fail');
  });
`);
