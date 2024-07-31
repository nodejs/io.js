'use strict';

const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const { Buffer } = require('buffer');
const { subtle } = globalThis.crypto;
const { createHash } = require('crypto');

const kTests = [
  ['SHA-1', 'sha1', 160],
  ['SHA-256', 'sha256', 256],
  ['SHA-384', 'sha384', 384],
  ['SHA-512', 'sha512', 512],
];

// Empty hash just works, not checking result
subtle.digest('SHA-512', Buffer.alloc(0))
  .then(common.mustCall());

// TODO(@jasnell): Need to move this particular test to pummel
// // Careful, this is an expensive operation because of both the memory
// // allocation and the cost of performing the hash on such a large
// // input.
// subtle.digest('SHA-512', new ArrayBuffer(2 ** 31 - 1))
//   .then(common.mustCall());

// TODO(@jasnell): Change max to 2 ** 31 - 1
// assert.rejects(subtle.digest('SHA-512', new ArrayBuffer(kMaxLength + 1)), {
//   code: 'ERR_OUT_OF_RANGE'
// });

const kData = (new TextEncoder()).encode('hello');
(async function() {
  await Promise.all(kTests.map(async (test) => {
    // Get the digest using the legacy crypto API
    const checkValue =
      createHash(test[1]).update(kData).digest().toString('hex');

    // Get the digest using the SubtleCrypto API
    const values = Promise.all([
      subtle.digest({ name: test[0] }, kData),
      subtle.digest({ name: test[0], length: test[2] }, kData),
      subtle.digest(test[0], kData),
      subtle.digest(test[0], kData.buffer),
      subtle.digest(test[0], new DataView(kData.buffer)),
      subtle.digest(test[0], Buffer.from(kData)),
    ]);

    // subtle.digest copies the input data, so changing it
    // while we're waiting on the Promises should never
    // cause the test to fail.
    kData[0] = 0x1;
    kData[2] = 0x2;
    kData[4] = 0x3;

    // Compare that the legacy crypto API and SubtleCrypto API
    // produce the same results
    (await values).forEach((v) => {
      assert(v instanceof ArrayBuffer);
      assert.strictEqual(checkValue, Buffer.from(v).toString('hex'));
    });
  }));
})().then(common.mustCall());

Promise.all([1, null, undefined].map((i) =>
  assert.rejects(subtle.digest(i, Buffer.alloc(0)), {
    message: /Unrecognized algorithm name/,
    name: 'NotSupportedError',
  })
)).then(common.mustCall());

assert.rejects(subtle.digest('', Buffer.alloc(0)), {
  message: /Unrecognized algorithm name/,
  name: 'NotSupportedError',
}).then(common.mustCall());

Promise.all([1, [], {}, null, undefined].map((i) =>
  assert.rejects(subtle.digest('SHA-256', i), {
    code: 'ERR_INVALID_ARG_TYPE'
  })
)).then(common.mustCall());

const kSourceData = {
  empty: '',
  short: '156eea7cc14c56cb94db030a4a9d95ff',
  medium: 'b6c8f9df648cd088b70f38e74197b18cb81e1e435' +
          '0d50bccb8fb5a7379c87bb2e3d6ed5461ed1e9f36' +
          'f340a3962a446b815b794b4bd43a4403502077b22' +
          '56cc807837f3aacd118eb4b9c2baeb897068625ab' +
          'aca193',
  long: null
};

kSourceData.long = kSourceData.medium.repeat(1024);

const kDigestedData = {
  'sha-1': {
    empty: 'da39a3ee5e6b4b0d3255bfef95601890afd80709',
    short: 'c91318cdf2396a015e3f4e6a86a0ba65b8635944',
    medium: 'e541060870eb16bf33b68e51f513526893986729',
    long: '3098b50037ecd02ebd657653b2bfa01eee27a2ea'
  },
  'sha-256': {
    empty: 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855',
    short: 'a2831186984792c7d32d59c89740687f19addc1b959e71a1cc538a3b7ed843f2',
    medium: '535367877ef014d7fc717e5cb7843e59b61aee62c7029cec7ec6c12fd924e0e4',
    long: '14cdea9dc75f5a6274d9fc1e64009912f1dcd306b48fe8e9cf122de671571781'
  },
  'sha-384': {
    empty: '38b060a751ac96384cd9327eb1b1e36a21fdb71114b' +
           'e07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2' +
           'f14898b95b',
    short: '6bf5ea6524d1cddc43f7cf3b56ee059227404a2f538' +
           'f022a3db7447a782c06c1ed05e8ab4f5edc17f37114' +
           '40dfe97731',
    medium: 'cbc2c588fe5b25f916da28b4e47a484ae6fc1fe490' +
            '2dd5c9939a6bfd034ab3b48b39087436011f6a9987' +
            '9d279540e977',
    long: '49f4fdb3981968f97d57370f85345067cd5296a97dd1' +
          'a18e06911e756e9608492529870e1ad130998d57cbfb' +
          'b7c1d09e'
  },
  'sha-512': {
    empty: 'cf83e1357eefb8bdf1542850d66d8007d620e4050b5' +
           '715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318' +
           'd2877eec2f63b931bd47417a81a538327af927da3e',
    short: '375248be5ff34be74cab4ff1c3bc8dc68bd5f8dff40' +
           '23e98f87b865cff2c724292df189443a64ff4134a65' +
           'cd4635b9d4f5dc0d3fb67528002a63acf26c9da575',
    medium: 'b9109f839e8ea43c890f293ce11dc6e2798d1e2431' +
            'f1e4b919e3b20c4f36303ba39c916db306c45a3b65' +
            '761ff5be85328eeaf42c3830f1d95e7a41165b7d2d36',
    long: '4b02caf650276030ea5617e597c5d53fd9daa68b78bfe' +
          '60b22aab8d36a4c2a3affdb71234f49276737c575ddf7' +
          '4d14054cbd6fdb98fd0ddcbcb46f91ad76b6ee'
  }
};

async function testDigest(size, name) {
  const digest = await subtle.digest(
    name,
    Buffer.from(kSourceData[size], 'hex'));

  assert.strictEqual(
    Buffer.from(digest).toString('hex'),
    kDigestedData[name.toLowerCase()][size]);
}

(async function() {
  const variations = [];
  Object.keys(kSourceData).forEach((size) => {
    Object.keys(kDigestedData).forEach((alg) => {
      const upCase = alg.toUpperCase();
      const downCase = alg.toLowerCase();
      const mixedCase = upCase.slice(0, 1) + downCase.slice(1);

      variations.push(testDigest(size, upCase));
      variations.push(testDigest(size, downCase));
      variations.push(testDigest(size, mixedCase));
    });
  });

  await Promise.all(variations);
})().then(common.mustCall());

(async () => {
  await assert.rejects(subtle.digest('RSA-OAEP', Buffer.alloc(1)), {
    name: 'NotSupportedError',
  });
})().then(common.mustCall());
