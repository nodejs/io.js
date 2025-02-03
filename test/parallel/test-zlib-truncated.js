'use strict';
// Tests zlib streams with truncated compressed input

require('../common');
const assert = require('assert');
const zlib = require('zlib');

const inputString = 'ΩΩLorem ipsum dolor sit amet, consectetur adipiscing eli' +
                    't. Morbi faucibus, purus at gravida dictum, libero arcu ' +
                    'convallis lacus, in commodo libero metus eu nisi. Nullam' +
                    ' commodo, neque nec porta placerat, nisi est fermentum a' +
                    'ugue, vitae gravida tellus sapien sit amet tellus. Aenea' +
                    'n non diam orci. Proin quis elit turpis. Suspendisse non' +
                    ' diam ipsum. Suspendisse nec ullamcorper odio. Vestibulu' +
                    'm arcu mi, sodales non suscipit id, ultrices ut massa. S' +
                    'ed ac sem sit amet arcu malesuada fermentum. Nunc sed. ';

const errMessage = /unexpected end of file/;

[
  { comp: 'gzip', decomp: 'gunzip', decompSync: 'gunzipSync' },
  { comp: 'gzip', decomp: 'unzip', decompSync: 'unzipSync' },
  { comp: 'deflate', decomp: 'inflate', decompSync: 'inflateSync' },
  { comp: 'deflateRaw', decomp: 'inflateRaw', decompSync: 'inflateRawSync' },
].forEach(function(methods) {
  zlib[methods.comp](inputString, function(err, compressed) {
    assert.ifError(err);
    const truncated = compressed.slice(0, compressed.length / 2);
    const toUTF8 = (buffer) => buffer.toString('utf-8');

    // sync sanity
    const decompressed = zlib[methods.decompSync](compressed);
    assert.strictEqual(toUTF8(decompressed), inputString);

    // async sanity
    zlib[methods.decomp](compressed, function(err, result) {
      assert.ifError(err);
      assert.strictEqual(toUTF8(result), inputString);
    });

    // Sync truncated input test
    assert.throws(function() {
      zlib[methods.decompSync](truncated);
    }, errMessage);

    // Async truncated input test
    zlib[methods.decomp](truncated, function(err, result) {
      assert.match(err.message, errMessage);
    });

    const syncFlushOpt = { finishFlush: zlib.constants.Z_SYNC_FLUSH };

    // Sync truncated input test, finishFlush = Z_SYNC_FLUSH
    const result = toUTF8(zlib[methods.decompSync](truncated, syncFlushOpt));
    assert.strictEqual(result, inputString.slice(0, result.length));

    // Async truncated input test, finishFlush = Z_SYNC_FLUSH
    zlib[methods.decomp](truncated, syncFlushOpt, function(err, decompressed) {
      assert.ifError(err);
      const result = toUTF8(decompressed);
      assert.strictEqual(result, inputString.slice(0, result.length));
    });
  });
});
