'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const {
  createSecretKey,
  KeyObject,
  randomBytes,
  generateKeyPairSync,
} = require('crypto');

function assertCryptoKey(cryptoKey, keyObject, algorithm, extractable, usages) {
  assert.strictEqual(cryptoKey instanceof CryptoKey, true);
  assert.strictEqual(cryptoKey.type, keyObject.type);
  assert.strictEqual(cryptoKey.algorithm.name, algorithm);
  assert.strictEqual(cryptoKey.extractable, extractable);
  assert.deepStrictEqual(cryptoKey.usages, usages);
  assert.strictEqual(keyObject.equals(KeyObject.from(cryptoKey)), true);
}

{
  for (const length of [128, 192, 256]) {
    const aes = createSecretKey(randomBytes(length >> 3));
    for (const algorithm of ['AES-CTR', 'AES-CBC', 'AES-GCM', 'AES-KW']) {
      const usages = algorithm === 'AES-KW' ? ['wrapKey', 'unwrapKey'] : ['encrypt', 'decrypt'];
      for (const extractable of [true, false]) {
        const cryptoKey = aes.toCryptoKey(algorithm, extractable, usages);
        assertCryptoKey(cryptoKey, aes, algorithm, extractable, usages);
        assert.strictEqual(cryptoKey.algorithm.length, length);
      }
    }
  }
}

{
  const pbkdf2 = createSecretKey(randomBytes(16));
  const algorithm = 'PBKDF2';
  const usages = ['deriveBits'];
  assert.throws(() => pbkdf2.toCryptoKey(algorithm, true, usages), {
    name: 'SyntaxError',
    message: 'PBKDF2 keys are not extractable'
  });
  assert.throws(() => pbkdf2.toCryptoKey(algorithm, false, ['wrapKey']), {
    name: 'SyntaxError',
    message: 'Unsupported key usage for a PBKDF2 key'
  });
  const cryptoKey = pbkdf2.toCryptoKey(algorithm, false, usages);
  assertCryptoKey(cryptoKey, pbkdf2, algorithm, false, usages);
  assert.strictEqual(cryptoKey.algorithm.length, undefined);
}

{
  for (const length of [128, 192, 256]) {
    const hmac = createSecretKey(randomBytes(length >> 3));
    const algorithm = 'HMAC';
    const usages = ['sign', 'verify'];

    assert.throws(() => {
      createSecretKey(Buffer.alloc(0)).toCryptoKey({ name: algorithm, hash: 'SHA-256' }, true, usages);
    }, {
      name: 'DataError',
      message: 'Zero-length key is not supported',
    });

    assert.throws(() => {
      hmac.toCryptoKey({
        name: algorithm,
        hash: 'SHA-256',
      }, true, []);
    }, {
      name: 'SyntaxError',
      message: 'Usages cannot be empty when importing a secret key.'
    });

    for (const hash of ['SHA-1', 'SHA-256', 'SHA-384', 'SHA-512']) {
      for (const extractable of [true, false]) {
        assert.throws(() => {
          hmac.toCryptoKey({ name: algorithm, hash: 'SHA-256', length: 0 }, true, usages);
        }, {
          name: 'DataError',
          message: 'Zero-length key is not supported',
        });
        const cryptoKey = hmac.toCryptoKey({ name: algorithm, hash }, extractable, usages);
        assertCryptoKey(cryptoKey, hmac, algorithm, extractable, usages);
        assert.strictEqual(cryptoKey.algorithm.length, length);
      }
    }
  }
}

{
  for (const algorithm of ['Ed25519', 'Ed448', 'X25519', 'X448']) {
    const { publicKey, privateKey } = generateKeyPairSync(algorithm.toLowerCase());
    assert.throws(() => {
      publicKey.toCryptoKey(algorithm === 'Ed25519' ? 'X25519' : 'Ed25519', true, []);
    }, {
      name: 'DataError',
      message: 'Invalid key type'
    });
    for (const key of [publicKey, privateKey]) {
      let usages;
      if (algorithm.startsWith('E')) {
        usages = key.type === 'public' ? ['verify'] : ['sign'];
      } else {
        usages = key.type === 'public' ? [] : ['deriveBits'];
      }
      for (const extractable of [true, false]) {
        const cryptoKey = key.toCryptoKey(algorithm, extractable, usages);
        assertCryptoKey(cryptoKey, key, algorithm, extractable, usages);
      }
    }
  }
}

{
  const { publicKey, privateKey } = generateKeyPairSync('rsa', { modulusLength: 2048 });
  for (const key of [publicKey, privateKey]) {
    for (const algorithm of ['RSASSA-PKCS1-v1_5', 'RSA-PSS', 'RSA-OAEP']) {
      let usages;
      if (algorithm === 'RSA-OAEP') {
        usages = key.type === 'public' ? ['encrypt', 'wrapKey'] : ['decrypt', 'unwrapKey'];
      } else {
        usages = key.type === 'public' ? ['verify'] : ['sign'];
      }
      for (const extractable of [true, false]) {
        for (const hash of ['SHA-1', 'SHA-256', 'SHA-384', 'SHA-512']) {
          const cryptoKey = key.toCryptoKey({
            name: algorithm,
            hash
          }, extractable, usages);
          assertCryptoKey(cryptoKey, key, algorithm, extractable, usages);
          assert.strictEqual(cryptoKey.algorithm.hash.name, hash);
        }
      }
    }
  }
}

{
  for (const namedCurve of ['P-256', 'P-384', 'P-521']) {
    const { publicKey, privateKey } = generateKeyPairSync('ec', { namedCurve });
    assert.throws(() => {
      privateKey.toCryptoKey({
        name: 'ECDH',
        namedCurve,
      }, true, []);
    }, {
      name: 'SyntaxError',
      message: 'Usages cannot be empty when importing a private key.'
    });
    assert.throws(() => {
      publicKey.toCryptoKey({
        name: 'ECDH',
        namedCurve: namedCurve === 'P-256' ? 'P-384' : 'P-256'
      }, true, []);
    }, {
      name: 'DataError',
      message: 'Named curve mismatch'
    });
    for (const key of [publicKey, privateKey]) {
      for (const algorithm of ['ECDH', 'ECDSA']) {
        let usages;
        if (algorithm === 'ECDH') {
          usages = key.type === 'public' ? [] : ['deriveBits'];
        } else {
          usages = key.type === 'public' ? ['verify'] : ['sign'];
        }
        for (const extractable of [true, false]) {
          const cryptoKey = key.toCryptoKey({
            name: algorithm,
            namedCurve
          }, extractable, usages);
          assertCryptoKey(cryptoKey, key, algorithm, extractable, usages);
          assert.strictEqual(cryptoKey.algorithm.namedCurve, namedCurve);
        }
      }
    }
  }
}
