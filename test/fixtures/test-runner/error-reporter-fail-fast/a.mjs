const assert = require('node:assert');
const { test } = require('node:test');

test('fail', () => {
  assert.fail('a.mjs fail');
});
