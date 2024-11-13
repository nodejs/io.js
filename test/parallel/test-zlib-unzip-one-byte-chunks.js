'use strict';

require('../common');

const assert = require('node:assert');
const zlib = require('node:zlib');
const { test } = require('node:test');

test('zlib should unzip one byte chunks', async () => {
  const { promise, resolve } = Promise.withResolvers();
  const data = Buffer.concat([
    zlib.gzipSync('abc'),
    zlib.gzipSync('def'),
  ]);

  const resultBuffers = [];

  const unzip = zlib.createUnzip()
    .on('error', (err) => {
      assert.ifError(err);
    })
    .on('data', (data) => resultBuffers.push(data))
    .on('finish', () => {
      const unzipped = Buffer.concat(resultBuffers).toString();
      assert.strictEqual(unzipped, 'abcdef',
                         `'${unzipped}' should match 'abcdef' after zipping ` +
        'and unzipping');
      resolve();
    });

  for (let i = 0; i < data.length; i++) {
    // Write each single byte individually.
    unzip.write(Buffer.from([data[i]]));
  }

  unzip.end();
  await promise;
});
