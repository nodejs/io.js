'use strict';
const common = require('../common');
const fs = require('fs');

if (!common.isMacOS) {
  common.skip('this tests works only on MacOS');
}

const assert = require('assert');

fs.readdir(
  Buffer.from('/dev'),
  { withFileTypes: true, encoding: 'buffer' },
  common.mustCall((e, d) => {
    assert.strictEqual(e, null);
  })
);
