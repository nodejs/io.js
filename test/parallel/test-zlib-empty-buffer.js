'use strict';

require('../common');

const { test } = require('node:test');
const zlib = require('node:zlib');
const { inspect, promisify } = require('node:util');
const assert = require('node:assert');

const emptyBuffer = Buffer.alloc(0);

test('zlib compression and decompression with various methods', async (t) => {
  const methods = [
    [zlib.deflateRawSync, zlib.inflateRawSync, 'raw sync'],
    [zlib.deflateSync, zlib.inflateSync, 'deflate sync'],
    [zlib.gzipSync, zlib.gunzipSync, 'gzip sync'],
    [zlib.brotliCompressSync, zlib.brotliDecompressSync, 'br sync'],
    [promisify(zlib.deflateRaw), promisify(zlib.inflateRaw), 'raw'],
    [promisify(zlib.deflate), promisify(zlib.inflate), 'deflate'],
    [promisify(zlib.gzip), promisify(zlib.gunzip), 'gzip'],
    [promisify(zlib.brotliCompress), promisify(zlib.brotliDecompress), 'br'],
  ];

  for (const [compress, decompress, method] of methods) {
    await t.test(`should handle ${method} compression and decompression`, async () => {
      const compressed = await compress(emptyBuffer);
      const decompressed = await decompress(compressed);

      assert.deepStrictEqual(
        emptyBuffer,
        decompressed,
        `Expected ${inspect(compressed)} to match ${inspect(decompressed)} for ${method}`
      );
    });
  }
});
