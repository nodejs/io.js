'use strict';
// Flags: --expose-internals

require('../common');
const fixtures = require('../common/fixtures');
const assert = require('node:assert');
const { test } = require('node:test');
const { dataURLProcessor } = require('internal/data_url');

// https://github.com/web-platform-tests/wpt/blob/7c79d998ff42e52de90290cb847d1b515b3b58f7/fetch/data-urls/processing.any.js
test('parsing data URLs', async () => {
  const tests = require(fixtures.path('wpt/fetch/data-urls/resources/data-urls.json'));

  for (let i = 0; i < tests.length; i++) {
    const input = tests[i][0];
    const expectedMimeType = tests[i][1];
    const expectedBody = expectedMimeType !== null ? new Uint8Array(tests[i][2]) : null;

    if (!URL.canParse(input)) {
      assert.strictEqual(expectedMimeType, null);
    } else if (expectedMimeType === null) {
      assert.strictEqual(dataURLProcessor(URL.parse(input)), 'failure');
    } else {
      const { mimeType, body } = dataURLProcessor(new URL(input));

      assert.deepStrictEqual(expectedBody, body);
      assert.deepStrictEqual(expectedMimeType, mimeType.toString());
    }
  }
});
