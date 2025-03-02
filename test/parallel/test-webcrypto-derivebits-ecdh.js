'use strict';

const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const { subtle } = globalThis.crypto;

const kTests = [
  {
    namedCurve: 'P-521',
    size: 66,
    pkcs8: '3081ee020100301006072a8648ce3d020106052b810400230481d63081d302010' +
           '1044201a67ed321915a64aa359b7d648ddc2618fa8e8d1867e8f71830b10d25ed' +
           '2891faf12f3c7e75421a2ea264f9a915320d274fe1470742b984e96b98912081f' +
           'acd478da18189038186000400209d483f28666881c6641f3a126f400f51e46511' +
           '70fe678c75e85712e2868adc850824997bebf0bc82b43028a6d2ec1777ca45279' +
           'f7206a3ea8b5cd2073f493e45000cb54c3a5acaa268c56710428878d98b8afbf6' +
           '8a612153632846d807e92672698f1b9c611de7d38e34cd6c73889092c56e52d68' +
           '0f1dfd092b87ac8ef9ff3c8fb48',
    spki: '30819b301006072a8648ce3d020106052b81040023038186000400ee69f94715d7' +
          '01e9e2011333d4f4f96cba7d91f88b112baf75cf09cc1f8aca97618da9389822d2' +
          '9b6fe9996a61203ef752b771e8958fc4677bb3778565ab60d6ed00deab6761895b' +
          '935e3ad325fb8549e56f13786aa73f88a2ecfe40933473d8aef240c4dfd7d506f2' +
          '2cdd0e55558f3fbf05ebf7efef7a72d78f46469b8448f26e2712',
    result: '009c2bce57be80adab3b07385b8e5990eb7d6fdebdb01bf35371a4f6075e9d28' +
            '8ac12a6dfe03aa5743bc81709d49a822940219b64b768acd520fa1368ea0af8d' +
            '475d',
  },
  {
    namedCurve: 'P-384',
    size: 48,
    pkcs8: '3081b6020100301006072a8648ce3d020106052b8104002204819e30819b02010' +
           '10430f871a5666589c14a5747263ef85b319cc023db6e35676c3d781eef8b055f' +
           'cfbe86fa0d06d056b5195fb1323af8de25b3a16403620004f11965df7dd4594d0' +
           '419c5086482a3b826b9797f9be0bd0d109c9e1e9989c1b9a92b8f269f98e17ad1' +
           '84ba73c1f79762af45af8141602642da271a6bb0ffeb0cb4478fcf707e661aa6d' +
           '6cdf51549c88c3f130be9e8201f6f6a09f4185aaf95c4',
    spki: '3076301006072a8648ce3d020106052b810400220362000491822dc2af59c18f5b' +
          '67f80df61a2603c2a8f0b3c0af822d63c279701a824560404401dde9a56ee52757' +
          'ea8bc748d4c82b5337b48d7b65583a3d572438880036bac6730f42ca5278966bd5' +
          'f21e86e21d30c5a6d0463ec513dd509ffcdcaf1ff5',
    result: 'e0bd6bce0aef8ca48838a6e2fcc57e67b9c5e8860c5f0be9dabec53e454e18a0' +
            'a174c48888a26488115b2dc9f1dfa52d',
  },
];

async function prepareKeys() {
  const keys = {};
  await Promise.all(
    kTests.map(async ({ namedCurve, size, pkcs8, spki, result }) => {
      const [
        privateKey,
        publicKey,
      ] = await Promise.all([
        subtle.importKey(
          'pkcs8',
          Buffer.from(pkcs8, 'hex'),
          {
            name: 'ECDH',
            namedCurve
          },
          true,
          ['deriveKey', 'deriveBits']),
        subtle.importKey(
          'spki',
          Buffer.from(spki, 'hex'),
          {
            name: 'ECDH',
            namedCurve
          },
          true,
          []),
      ]);
      keys[namedCurve] = {
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
    Object.keys(keys).map(async (namedCurve) => {
      const { size, result, privateKey, publicKey } = keys[namedCurve];

      {
        // Good parameters
        const bits = await subtle.deriveBits({
          name: 'ECDH',
          public: publicKey
        }, privateKey, 8 * size);

        assert(bits instanceof ArrayBuffer);
        assert.strictEqual(Buffer.from(bits).toString('hex'), result);
      }

      {
        // Case insensitivity
        const bits = await subtle.deriveBits({
          name: 'eCdH',
          public: publicKey
        }, privateKey, 8 * size);

        assert.strictEqual(Buffer.from(bits).toString('hex'), result);
      }

      {
        // Null length
        const bits = await subtle.deriveBits({
          name: 'ECDH',
          public: publicKey
        }, privateKey, null);

        assert.strictEqual(Buffer.from(bits).toString('hex'), result);
      }

      {
        // Default length
        const bits = await subtle.deriveBits({
          name: 'ECDH',
          public: publicKey
        }, privateKey);

        assert.strictEqual(Buffer.from(bits).toString('hex'), result);
      }

      {
        // Short Result
        const bits = await subtle.deriveBits({
          name: 'ECDH',
          public: publicKey
        }, privateKey, 8 * size - 32);

        assert.strictEqual(
          Buffer.from(bits).toString('hex'),
          result.slice(0, -8));
      }

      {
        // Too long result
        await assert.rejects(subtle.deriveBits({
          name: 'ECDH',
          public: publicKey
        }, privateKey, 8 * size + 8), {
          message: /derived bit length is too small/
        });
      }

      {
        // Non-multiple of 8
        const bits = await subtle.deriveBits({
          name: 'ECDH',
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
        { name: 'ECDH' },
        keys['P-384'].privateKey,
        8 * keys['P-384'].size),
      { code: 'ERR_MISSING_OPTION' });
  }

  {
    // The public property is not a CryptoKey
    await assert.rejects(
      subtle.deriveBits(
        {
          name: 'ECDH',
          public: { message: 'Not a CryptoKey' }
        },
        keys['P-384'].privateKey,
        8 * keys['P-384'].size),
      { code: 'ERR_INVALID_ARG_TYPE' });
  }

  {
    // Mismatched named curves
    await assert.rejects(
      subtle.deriveBits(
        {
          name: 'ECDH',
          public: keys['P-384'].publicKey
        },
        keys['P-521'].privateKey,
        8 * keys['P-384'].size),
      { message: /Named curve mismatch/ });
  }

  {
    // Incorrect public key algorithm
    const { publicKey } = await subtle.generateKey(
      {
        name: 'ECDSA',
        namedCurve: 'P-521'
      }, false, ['sign', 'verify']);

    await assert.rejects(subtle.deriveBits({
      name: 'ECDH',
      public: publicKey
    }, keys['P-521'].privateKey, null), {
      message: 'algorithm.public must be an ECDH key'
    });
  }

  {
    // Private key does not have correct usages
    const privateKey = await subtle.importKey(
      'pkcs8',
      Buffer.from(kTests[0].pkcs8, 'hex'),
      {
        name: 'ECDH',
        namedCurve: 'P-521'
      }, false, ['deriveKey']);

    await assert.rejects(subtle.deriveBits({
      name: 'ECDH',
      public: keys['P-521'].publicKey,
    }, privateKey, null), {
      message: /baseKey does not have deriveBits usage/
    });
  }

  {
    // Base key is not a private key
    await assert.rejects(subtle.deriveBits({
      name: 'ECDH',
      public: keys['P-521'].publicKey
    }, keys['P-521'].publicKey, null), {
      name: 'InvalidAccessError'
    });
  }

  {
    // Public is not a public key
    await assert.rejects(subtle.deriveBits({
      name: 'ECDH',
      public: keys['P-521'].privateKey
    }, keys['P-521'].privateKey, null), {
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
      name: 'ECDH',
      public: key
    }, keys['P-521'].publicKey, null), {
      name: 'InvalidAccessError'
    });
  }
})().then(common.mustCall());
