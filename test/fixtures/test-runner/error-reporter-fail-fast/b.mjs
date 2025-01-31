const assert = require('node:assert');
const { test } = require('node:test');

test('fail', () => {
  assert.fail('b.mjs fail');
});
