'use strict';

require('../common');

const assert = require('node:assert');
const { Gunzip } = require('node:zlib');
const { test } = require('node:test');

test('zlib object write', async (t) => {
  const { promise, resolve, reject } = Promise.withResolvers();
  const gunzip = new Gunzip({ objectMode: true });
  gunzip.on('error', reject);
  gunzip.on('close', resolve);
  assert.throws(() => {
    gunzip.write({});
  }, {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_TYPE'
  });
  gunzip.close();
  await promise;
});
