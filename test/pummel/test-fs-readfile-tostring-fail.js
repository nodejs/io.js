'use strict';

const common = require('../common');

if (!common.enoughTestMem)
  common.skip('intensive toString tests due to memory confinements');

const assert = require('assert');
const fs = require('fs');
const cp = require('child_process');
const kStringMaxLength = require('buffer').constants.MAX_STRING_LENGTH;
const size = Math.floor(kStringMaxLength / 200);

if (common.isAIX && (Number(cp.execSync('ulimit -f')) * 512) < kStringMaxLength)
  common.skip('intensive toString tests due to file size confinements');

const tmpdir = require('../common/tmpdir');
tmpdir.refresh();

if (!tmpdir.hasEnoughSpace(kStringMaxLength + size)) {
  common.skip(`Not enough space in ${tmpdir.path}`);
}

const file = tmpdir.resolve('toobig.txt');
const stream = fs.createWriteStream(file, {
  flags: 'a',
});

stream.on('error', (err) => { throw err; });

const a = Buffer.alloc(size, 'a');
let expectedSize = 0;

for (let i = 0; i < 201; i++) {
  stream.write(a, (err) => { assert.ifError(err); });
  expectedSize += a.length;
}

stream.end();
stream.on('finish', common.mustCall(function() {
  assert.strictEqual(stream.bytesWritten, expectedSize,
                     `${stream.bytesWritten} bytes written (expected ${expectedSize} bytes).`);
  fs.readFile(file, 'utf8', common.mustCall(function(err, buf) {
    assert.ok(err instanceof Error);
    if (err.message !== 'Array buffer allocation failed') {
      const stringLengthHex = kStringMaxLength.toString(16);
      common.expectsError({
        message: 'Cannot create a string longer than ' +
                 `0x${stringLengthHex} characters`,
        code: 'ERR_STRING_TOO_LONG',
        name: 'Error',
      })(err);
    }
    assert.strictEqual(buf, undefined);
  }));
}));

function destroy() {
  try {
    fs.unlinkSync(file);
  } catch {
    // it may not exist
  }
}

process.on('exit', destroy);

process.on('SIGINT', function() {
  destroy();
  process.exit();
});

// To make sure we don't leave a very large file
// on test machines in the event this test fails.
process.on('uncaughtException', function(err) {
  destroy();
  throw err;
});
