'use strict';

const common = require('../common');
if (!common.hasCrypto) {
  common.skip('missing crypto');
}

if (common.isWindows) {
  common.skip('Not supported on Windows');
}

if (common.isASan) {
  common.skip('ASan does not play well with secure heap allocations');
}

const assert = require('assert');
const { fork } = require('child_process');
const fixtures = require('../common/fixtures');
const { hasOpenSSL3 } = require('../common/crypto');
const {
  secureHeapUsed,
  createDiffieHellman,
  getFips,
} = require('crypto');

if (process.argv[2] === 'child') {

  const a = secureHeapUsed();

  assert(a);
  assert.strictEqual(typeof a, 'object');
  assert.strictEqual(a.total, 65536);
  assert.strictEqual(a.min, 4);
  assert.strictEqual(a.used, 0);

  {
    const size = getFips() || hasOpenSSL3 ? 1024 : 256;
    const dh1 = createDiffieHellman(size);
    const p1 = dh1.getPrime('buffer');
    const dh2 = createDiffieHellman(p1, 'buffer');
    const key1 = dh1.generateKeys();
    const key2 = dh2.generateKeys('hex');
    dh1.computeSecret(key2, 'hex', 'base64');
    dh2.computeSecret(key1, 'latin1', 'buffer');

    const b = secureHeapUsed();
    assert(b);
    assert.strictEqual(typeof b, 'object');
    assert.strictEqual(b.total, 65536);
    assert.strictEqual(b.min, 4);
    // The amount used can vary on a number of factors
    assert(b.used > 0);
    assert(b.utilization > 0.0);
  }

  return;
}

const child = fork(
  process.argv[1],
  ['child'],
  { execArgv: ['--secure-heap=65536', '--secure-heap-min=4'] });

child.on('exit', common.mustCall((code) => {
  assert.strictEqual(code, 0);
}));

{
  const child = fork(fixtures.path('a.js'), {
    execArgv: ['--secure-heap=3', '--secure-heap-min=3'],
    stdio: 'pipe'
  });
  let res = '';
  child.on('exit', common.mustCall((code) => {
    assert.notStrictEqual(code, 0);
    assert.match(res, /--secure-heap must be a power of 2/);
    assert.match(res, /--secure-heap-min must be a power of 2/);
  }));
  child.stderr.setEncoding('utf8');
  child.stderr.on('data', (chunk) => res += chunk);
}
