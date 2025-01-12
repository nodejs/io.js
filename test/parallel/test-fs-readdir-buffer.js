'use strict';

const common = require('../common');
const assert = require('node:assert');
const { describe, it } = require('node:test');

const fs = require('fs');

if (!common.isMacOS) {
  common.skip('this tests works only on MacOS');
}

describe('fs.readdir from buffer', () => {
  it('it should read from the buffer', (t, done) => {
    fs.readdir(
      Buffer.from('/dev'),
      { withFileTypes: true, encoding: 'buffer' },
      (e) => {
        assert.strictEqual(e, null);
        done();
      }
    );
  });
});
