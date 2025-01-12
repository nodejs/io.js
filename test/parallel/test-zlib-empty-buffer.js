'use strict';

require('../common');

const zlib = require('node:zlib');
const { inspect, promisify } = require('node:util');
const assert = require('node:assert');
const { test } = require('node:test');

test('empty buffer', async (t) => {
  const emptyBuffer = Buffer.alloc(0);

  for (const [ compress, decompress, method ] of [
    [ zlib.deflateRawSync, zlib.inflateRawSync, 'raw sync' ],
    [ zlib.deflateSync, zlib.inflateSync, 'deflate sync' ],
    [ zlib.gzipSync, zlib.gunzipSync, 'gzip sync' ],
    [ zlib.brotliCompressSync, zlib.brotliDecompressSync, 'br sync' ],
    [ promisify(zlib.deflateRaw), promisify(zlib.inflateRaw), 'raw' ],
    [ promisify(zlib.deflate), promisify(zlib.inflate), 'deflate' ],
    [ promisify(zlib.gzip), promisify(zlib.gunzip), 'gzip' ],
    [ promisify(zlib.brotliCompress), promisify(zlib.brotliDecompress), 'br' ],
  ]) {
    const compressed = await compress(emptyBuffer);
    const decompressed = await decompress(compressed);
    assert.deepStrictEqual(
      emptyBuffer, decompressed,
      `Expected ${inspect(compressed)} to match ${inspect(decompressed)} ` +
      `to match for ${method}`);
  }
});
