'use strict';
const common = require('../common');
if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const assert = require('assert');
const crypto = require('crypto');
const fs = require('fs');

const { hasOpenSSL } = require('../common/crypto');
const fixtures = require('../common/fixtures');

let cryptoType;
let digest;

// Test hashing
const a1 = crypto.createHash('sha1').update('Test123').digest('hex');
const a2 = crypto.createHash('sha256').update('Test123').digest('base64');
const a3 = crypto.createHash('sha512').update('Test123').digest(); // buffer
const a4 = crypto.createHash('sha1').update('Test123').digest('buffer');

// stream interface
let a5 = crypto.createHash('sha512');
a5.end('Test123');
a5 = a5.read();

let a6 = crypto.createHash('sha512');
a6.write('Te');
a6.write('st');
a6.write('123');
a6.end();
a6 = a6.read();

let a7 = crypto.createHash('sha512');
a7.end();
a7 = a7.read();

let a8 = crypto.createHash('sha512');
a8.write('');
a8.end();
a8 = a8.read();

if (!crypto.getFips()) {
  cryptoType = 'md5';
  digest = 'latin1';
  const a0 = crypto.createHash(cryptoType).update('Test123').digest(digest);
  assert.strictEqual(
    a0,
    'h\u00ea\u00cb\u0097\u00d8o\fF!\u00fa+\u000e\u0017\u00ca\u00bd\u008c',
    `${cryptoType} with ${digest} digest failed to evaluate to expected hash`
  );
}
cryptoType = 'md5';
digest = 'hex';
assert.strictEqual(
  a1,
  '8308651804facb7b9af8ffc53a33a22d6a1c8ac2',
  `${cryptoType} with ${digest} digest failed to evaluate to expected hash`);
cryptoType = 'sha256';
digest = 'base64';
assert.strictEqual(
  a2,
  '2bX1jws4GYKTlxhloUB09Z66PoJZW+y+hq5R8dnx9l4=',
  `${cryptoType} with ${digest} digest failed to evaluate to expected hash`);
cryptoType = 'sha512';
digest = 'latin1';
assert.deepStrictEqual(
  a3,
  Buffer.from(
    '\u00c1(4\u00f1\u0003\u001fd\u0097!O\'\u00d4C/&Qz\u00d4' +
    '\u0094\u0015l\u00b8\u008dQ+\u00db\u001d\u00c4\u00b5}\u00b2' +
    '\u00d6\u0092\u00a3\u00df\u00a2i\u00a1\u009b\n\n*\u000f' +
    '\u00d7\u00d6\u00a2\u00a8\u0085\u00e3<\u0083\u009c\u0093' +
    '\u00c2\u0006\u00da0\u00a1\u00879(G\u00ed\'',
    'latin1'),
  `${cryptoType} with ${digest} digest failed to evaluate to expected hash`);
cryptoType = 'sha1';
digest = 'hex';
assert.deepStrictEqual(
  a4,
  Buffer.from('8308651804facb7b9af8ffc53a33a22d6a1c8ac2', 'hex'),
  `${cryptoType} with ${digest} digest failed to evaluate to expected hash`
);

// Stream interface should produce the same result.
assert.deepStrictEqual(a5, a3);
assert.deepStrictEqual(a6, a3);
assert.notStrictEqual(a7, undefined);
assert.notStrictEqual(a8, undefined);

// Test multiple updates to same hash
const h1 = crypto.createHash('sha1').update('Test123').digest('hex');
const h2 = crypto.createHash('sha1').update('Test').update('123').digest('hex');
assert.strictEqual(h1, h2);

// Test hashing for binary files
const fn = fixtures.path('sample.png');
const sha1Hash = crypto.createHash('sha1');
const fileStream = fs.createReadStream(fn);
fileStream.on('data', function(data) {
  sha1Hash.update(data);
});
fileStream.on('close', common.mustCall(function() {
  // Test SHA1 of sample.png
  assert.strictEqual(sha1Hash.digest('hex'),
                     '22723e553129a336ad96e10f6aecdf0f45e4149e');
}));

// Issue https://github.com/nodejs/node-v0.x-archive/issues/2227: unknown digest
// method should throw an error.
assert.throws(function() {
  crypto.createHash('xyzzy');
}, /Digest method not supported/);

// Issue https://github.com/nodejs/node/issues/9819: throwing encoding used to
// segfault.
assert.throws(
  () => crypto.createHash('sha256').digest({
    toString: () => { throw new Error('boom'); },
  }),
  {
    name: 'Error',
    message: 'boom'
  });

// Issue https://github.com/nodejs/node/issues/25487: error message for invalid
// arg type to update method should include all possible types
assert.throws(
  () => crypto.createHash('sha256').update(),
  {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
  });

// Default UTF-8 encoding
const hutf8 = crypto.createHash('sha512').update('УТФ-8 text').digest('hex');
assert.strictEqual(
  hutf8,
  '4b21bbd1a68e690a730ddcb5a8bc94ead9879ffe82580767ad7ec6fa8ba2dea6' +
        '43a821af66afa9a45b6a78c712fecf0e56dc7f43aef4bcfc8eb5b4d8dca6ea5b');

assert.notStrictEqual(
  hutf8,
  crypto.createHash('sha512').update('УТФ-8 text', 'latin1').digest('hex'));

const h3 = crypto.createHash('sha256');
h3.digest();

assert.throws(
  () => h3.digest(),
  {
    code: 'ERR_CRYPTO_HASH_FINALIZED',
    name: 'Error'
  });

assert.throws(
  () => h3.update('foo'),
  {
    code: 'ERR_CRYPTO_HASH_FINALIZED',
    name: 'Error'
  });

assert.strictEqual(
  crypto.createHash('sha256').update('test').digest('ucs2'),
  crypto.createHash('sha256').update('test').digest().toString('ucs2'));

assert.throws(
  () => crypto.createHash(),
  {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: 'The "algorithm" argument must be of type string. ' +
             'Received undefined'
  }
);

{
  const Hash = crypto.Hash;
  const instance = crypto.Hash('sha256');
  assert(instance instanceof Hash, 'Hash is expected to return a new instance' +
                                   ' when called without `new`');
}

// Test XOF hash functions and the outputLength option.
{
  // Default outputLengths. Since OpenSSL 3.4 an outputLength is mandatory
  if (!hasOpenSSL(3, 4)) {
    assert.strictEqual(crypto.createHash('shake128').digest('hex'),
                       '7f9c2ba4e88f827d616045507605853e');
    assert.strictEqual(crypto.createHash('shake128', null).digest('hex'),
                       '7f9c2ba4e88f827d616045507605853e');
    assert.strictEqual(crypto.createHash('shake256').digest('hex'),
                       '46b9dd2b0ba88d13233b3feb743eeb24' +
                       '3fcd52ea62b81b82b50c27646ed5762f');
    assert.strictEqual(crypto.createHash('shake256', { outputLength: 0 })
                             .copy()  // Default outputLength.
                             .digest('hex'),
                       '46b9dd2b0ba88d13233b3feb743eeb24' +
                       '3fcd52ea62b81b82b50c27646ed5762f');
  }

  // Short outputLengths.
  assert.strictEqual(crypto.createHash('shake128', { outputLength: 0 })
                           .digest('hex'),
                     '');
  assert.strictEqual(crypto.createHash('shake128', { outputLength: 5 })
                           .copy({ outputLength: 0 })
                           .digest('hex'),
                     '');
  assert.strictEqual(crypto.createHash('shake128', { outputLength: 5 })
                           .digest('hex'),
                     '7f9c2ba4e8');
  assert.strictEqual(crypto.createHash('shake128', { outputLength: 0 })
                           .copy({ outputLength: 5 })
                           .digest('hex'),
                     '7f9c2ba4e8');
  assert.strictEqual(crypto.createHash('shake128', { outputLength: 15 })
                           .digest('hex'),
                     '7f9c2ba4e88f827d61604550760585');
  assert.strictEqual(crypto.createHash('shake256', { outputLength: 16 })
                           .digest('hex'),
                     '46b9dd2b0ba88d13233b3feb743eeb24');

  // Large outputLengths.
  assert.strictEqual(crypto.createHash('shake128', { outputLength: 128 })
                           .digest('hex'),
                     '7f9c2ba4e88f827d616045507605853e' +
                     'd73b8093f6efbc88eb1a6eacfa66ef26' +
                     '3cb1eea988004b93103cfb0aeefd2a68' +
                     '6e01fa4a58e8a3639ca8a1e3f9ae57e2' +
                     '35b8cc873c23dc62b8d260169afa2f75' +
                     'ab916a58d974918835d25e6a435085b2' +
                     'badfd6dfaac359a5efbb7bcc4b59d538' +
                     'df9a04302e10c8bc1cbf1a0b3a5120ea');
  const superLongHash = crypto.createHash('shake256', {
    outputLength: 1024 * 1024
  }).update('The message is shorter than the hash!')
    .digest('hex');
  assert.strictEqual(superLongHash.length, 2 * 1024 * 1024);
  assert.ok(superLongHash.endsWith('193414035ddba77bf7bba97981e656ec'));
  assert.ok(superLongHash.startsWith('a2a28dbc49cfd6e5d6ceea3d03e77748'));

  // Non-XOF hash functions should accept valid outputLength options as well.
  assert.strictEqual(crypto.createHash('sha224', { outputLength: 28 })
                           .digest('hex'),
                     'd14a028c2a3a2bc9476102bb288234c4' +
                     '15a2b01f828ea62ac5b3e42f');

  // Passing invalid sizes should throw during creation.
  assert.throws(() => {
    crypto.createHash('sha256', { outputLength: 28 });
  }, {
    code: 'ERR_OSSL_EVP_NOT_XOF_OR_INVALID_LENGTH'
  });

  for (const outputLength of [null, {}, 'foo', false]) {
    assert.throws(() => crypto.createHash('sha256', { outputLength }),
                  { code: 'ERR_INVALID_ARG_TYPE' });
  }

  for (const outputLength of [-1, .5, Infinity, 2 ** 90]) {
    assert.throws(() => crypto.createHash('sha256', { outputLength }),
                  { code: 'ERR_OUT_OF_RANGE' });
  }
}

{
  const h = crypto.createHash('sha512');
  h.digest();
  assert.throws(() => h.copy(), { code: 'ERR_CRYPTO_HASH_FINALIZED' });
  assert.throws(() => h.digest(), { code: 'ERR_CRYPTO_HASH_FINALIZED' });
}

{
  const a = crypto.createHash('sha512').update('abc');
  const b = a.copy();
  const c = b.copy().update('def');
  const d = crypto.createHash('sha512').update('abcdef');
  assert.strictEqual(a.digest('hex'), b.digest('hex'));
  assert.strictEqual(c.digest('hex'), d.digest('hex'));
}

{
  crypto.Hash('sha256');
  common.expectWarning({
    DeprecationWarning: [
      ['crypto.Hash constructor is deprecated.',
       'DEP0179'],
    ]
  });
}
