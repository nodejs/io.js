'use strict';

const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const { subtle } = globalThis.crypto;

const kTests = [
  {
    name: 'X25519',
    size: 32,
    pkcs8: '302e020100300506032b656e04220420c8838e76d057dfb7d8c95a69e138160ad' +
           'd6373fd71a4d276bb56e3a81b64ff61',
    spki: '302a300506032b656e0321001cf2b1e6022ec537371ed7f53e54fa1154d83e98eb' +
          '64ea51fae5b3307cfe9706',
    result: '2768409dfab99ec23b8c89b93ff5880295f76176088f89e43dfebe7ea1950008'
  },
  {
    name: 'X448',
    size: 56,
    pkcs8: '3046020100300506032b656f043a043858c7d29a3eb519b29d00cfb191bb64fc6' +
           'd8a42d8f17176272b89f2272d1819295c6525c0829671b052ef0727530f188e31' +
           'd0cc53bf26929e',
    spki: '3042300506032b656f033900b604a1d1a5cd1d9426d561ef630a9eb16cbe69d5b9' +
          'ca615edc53633efb52ea31e6e6a0a1dbacc6e76cbce6482d7e4ba3d55d9e802765' +
          'ce6f',
    result: 'f0f6c5f17f94f4291eab7178866d37ec8906dd6c514143dc85be7cf28deff39b' +
            '726e0f6dcf810eb594dca97b4882bd44c43ea7dc67f49a4e',
  },
];

async function prepareKeys() {
  const keys = {};
  await Promise.all(
    kTests.map(async ({ name, size, pkcs8, spki, result }) => {
      const [
        privateKey,
        publicKey,
      ] = await Promise.all([
        subtle.importKey(
          'pkcs8',
          Buffer.from(pkcs8, 'hex'),
          { name },
          true,
          ['deriveKey', 'deriveBits']),
        subtle.importKey(
          'spki',
          Buffer.from(spki, 'hex'),
          { name },
          true,
          []),
      ]);
      keys[name] = {
        privateKey,
        publicKey,
        size,
        result,
      };
    }));
  return keys;
}

(async function() {
  const keys = await prepareKeys();

  await Promise.all(
    Object.keys(keys).map(async (name) => {
      const { size, result, privateKey, publicKey } = keys[name];

      {
        // Good parameters
        const bits = await subtle.deriveBits({
          name,
          public: publicKey
        }, privateKey, 8 * size);

        assert(bits instanceof ArrayBuffer);
        assert.strictEqual(Buffer.from(bits).toString('hex'), result);
      }

      {
        // Case insensitivity
        const bits = await subtle.deriveBits({
          name: name.toLowerCase(),
          public: publicKey
        }, privateKey, 8 * size);

        assert.strictEqual(Buffer.from(bits).toString('hex'), result);
      }

      {
        // Null length
        const bits = await subtle.deriveBits({
          name,
          public: publicKey
        }, privateKey, null);

        assert.strictEqual(Buffer.from(bits).toString('hex'), result);
      }

      {
        // Default length
        const bits = await subtle.deriveBits({
          name,
          public: publicKey
        }, privateKey);

        assert.strictEqual(Buffer.from(bits).toString('hex'), result);
      }

      {
        // Short Result
        const bits = await subtle.deriveBits({
          name,
          public: publicKey
        }, privateKey, 8 * size - 32);

        assert.strictEqual(
          Buffer.from(bits).toString('hex'),
          result.slice(0, -8));
      }

      {
        // Too long result
        await assert.rejects(subtle.deriveBits({
          name,
          public: publicKey
        }, privateKey, 8 * size + 8), {
          message: /derived bit length is too small/
        });
      }

      {
        // Non-multiple of 8
        const bits = await subtle.deriveBits({
          name,
          public: publicKey
        }, privateKey, 8 * size - 11);

        const expected = Buffer.from(result.slice(0, -2), 'hex');
        expected[size - 2] = expected[size - 2] & 0b11111000;
        assert.deepStrictEqual(
          Buffer.from(bits),
          expected);
      }
    }));

  // Error tests
  {
    // Missing public property
    await assert.rejects(
      subtle.deriveBits(
        { name: 'X448' },
        keys.X448.privateKey,
        8 * keys.X448.size),
      { code: 'ERR_MISSING_OPTION' });
  }

  {
    // The public property is not a CryptoKey
    await assert.rejects(
      subtle.deriveBits(
        {
          name: 'X448',
          public: { message: 'Not a CryptoKey' }
        },
        keys.X448.privateKey,
        8 * keys.X448.size),
      { code: 'ERR_INVALID_ARG_TYPE' });
  }

  {
    // Mismatched types
    await assert.rejects(
      subtle.deriveBits(
        {
          name: 'X448',
          public: keys.X25519.publicKey
        },
        keys.X448.privateKey,
        8 * keys.X448.size),
      { message: 'The public and private keys must be of the same type' });
  }

  {
    // Base key is not a private key
    await assert.rejects(subtle.deriveBits({
      name: 'X448',
      public: keys.X448.publicKey
    }, keys.X448.publicKey, null), {
      name: 'InvalidAccessError'
    });
  }

  {
    // Base key is not a private key
    await assert.rejects(subtle.deriveBits({
      name: 'X448',
      public: keys.X448.privateKey
    }, keys.X448.publicKey, null), {
      name: 'InvalidAccessError'
    });
  }

  {
    // Public is a secret key
    const keyData = globalThis.crypto.getRandomValues(new Uint8Array(32));
    const key = await subtle.importKey(
      'raw',
      keyData,
      { name: 'AES-CBC', length: 256 },
      false, ['encrypt']);

    await assert.rejects(subtle.deriveBits({
      name: 'X448',
      public: key
    }, keys.X448.publicKey, null), {
      name: 'InvalidAccessError'
    });
  }
})().then(common.mustCall());
