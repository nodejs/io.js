'use strict';

const common = require('../common');

const test = require('node:test');
const assert = require('node:assert');

test('TextDecoder correctly decodes windows-1252 encoded data', { skip: !common.hasIntl }, () => {
  const latin1Bytes = new Uint8Array([0xc1, 0xe9, 0xf3]);

  const expectedString = 'Áéó';

  const decoder = new TextDecoder('windows-1252');
  const decodedString = decoder.decode(latin1Bytes);

  assert.strictEqual(decodedString, expectedString);
});
