'use strict';

// Flags: --expose-gc

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const http2 = require('http2');

// Tests that write uses the correct encoding when writing
// using the helper function createWriteReq

const testString = 'a\u00A1\u0100\uD83D\uDE00';

const encodings = {
  'buffer': 'utf8',
  'ascii': 'ascii',
  'latin1': 'latin1',
  'binary': 'latin1',
  'utf8': 'utf8',
  'utf-8': 'utf8',
  'ucs2': 'ucs2',
  'ucs-2': 'ucs2',
  'utf16le': 'ucs2',
  'utf-16le': 'ucs2',
  'UTF8': 'utf8' // Should fall through to Buffer.from
};

const testsToRun = Object.keys(encodings).length;
let testsFinished = 0;

const server = http2.createServer(common.mustCall((req, res) => {
  const testEncoding = encodings[req.url.slice(1)];

  req.on('data', common.mustCall((chunk) => assert.ok(
    Buffer.from(testString, testEncoding).equals(chunk)
  )));

  req.on('end', () => res.end());
}, Object.keys(encodings).length));

server.listen(0, common.mustCall(function() {
  Object.keys(encodings).forEach((writeEncoding) => {
    const client = http2.connect(`http://localhost:${this.address().port}`);
    const req = client.request({
      ':path': `/${writeEncoding}`,
      ':method': 'POST'
    });

    assert.strictEqual(req._writableState.decodeStrings, false);
    req.write(
      writeEncoding !== 'buffer' ? testString : Buffer.from(testString),
      writeEncoding !== 'buffer' ? writeEncoding : undefined
    );
    req.resume();

    req.on('end', common.mustCall(function() {
      client.close();
      testsFinished++;

      if (testsFinished === testsToRun) {
        server.close(common.mustCall());
      }
    }));

    // Ref: https://github.com/nodejs/node/issues/17840
    const origDestroy = req.destroy;
    req.destroy = function(...args) {
      // Schedule a garbage collection event at the end of the current
      // MakeCallback() run.
      process.nextTick(globalThis.gc);
      return origDestroy.call(this, ...args);
    };

    req.end();
  });
}));
