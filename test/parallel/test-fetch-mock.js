'use strict';
require('../common');
const { mock, test } = require('node:test');
const assert = require('node:assert');

test('should correctly stub globalThis.fetch', async () => {
  const customFetch = async (url) => {
    return {
      text: async () => 'foo',
    };
  };

  mock.method(globalThis, 'fetch', customFetch);

  const response = await globalThis.fetch('some-url');
  const text = await response.text();

  assert.strictEqual(text, 'foo');
  mock.restoreAll();
});
