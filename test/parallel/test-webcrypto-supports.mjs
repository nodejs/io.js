import * as common from '../common/index.mjs';

if (!common.hasCrypto)
  common.skip('missing crypto');

import * as assert from 'node:assert';
const { SubtleCrypto } = globalThis;
const { subtle } = globalThis.crypto;

const RSA_KEY_GEN = {
  modulusLength: 2048,
  publicExponent: new Uint8Array([1, 0, 1])
};

const [ECDH, X448, X25519] = await Promise.all([
  subtle.generateKey({ name: 'ECDH', namedCurve: 'P-256' }, false, ['deriveBits']),
  subtle.generateKey('X448', false, ['deriveBits']),
  subtle.generateKey('X25519', false, ['deriveBits']),
]);

const vectors = {
  'encrypt': [
    // TODO:
  ],
  'sign': [
    [false, 'Invalid'],
    [false, 'SHA-1'],

    [true, 'Ed25519'],

    [true, 'Ed448'],
    [true, { name: 'Ed448', context: Buffer.alloc(0) }],
    [false, { name: 'Ed448', context: Buffer.alloc(1) }],

    [true, 'RSASSA-PKCS1-v1_5'],

    [true, { name: 'RSA-PSS', saltLength: 32 }],
    [false, 'RSA-PSS'],

    [true, { name: 'ECDSA', hash: 'SHA-256' }],
    [false, { name: 'ECDSA', hash: 'Invalid' }],
    [false, { name: 'ECDSA', hash: 'Ed25519' }],
    [false, 'ECDSA'],

    [true, 'HMAC'],
  ],
  'digest': [
    [true, 'SHA-1'],
    [true, 'SHA-256'],
    [true, 'SHA-384'],
    [true, 'SHA-512'],
    [false, 'Invalid'],
    [false, 'Ed25519'],
  ],
  'generateKey': [
    [false, 'SHA-1'],
    [false, 'Invalid'],
    [false, 'HKDF'],
    [false, 'PBKDF2'],
    [true, 'X25519'],
    [true, 'X448'],
    [true, 'Ed25519'],
    [true, 'Ed448'],
    [true, { name: 'HMAC', hash: 'SHA-256' }],
    [true, { name: 'HMAC', hash: 'SHA-256', length: 256 }],
    [false, { name: 'HMAC', hash: 'SHA-256', length: 25 }],
    [true, { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256', ...RSA_KEY_GEN }],
    [true, { name: 'RSA-PSS', hash: 'SHA-256', ...RSA_KEY_GEN }],
    [true, { name: 'RSA-OAEP', hash: 'SHA-256', ...RSA_KEY_GEN }],
    [true, { name: 'ECDSA', namedCurve: 'P-256' }],
    [false, { name: 'ECDSA', namedCurve: 'X25519' }],
    [true, { name: 'AES-CTR', length: 128 }],
    [false, { name: 'AES-CTR', length: 25 }],
    [true, { name: 'AES-CBC', length: 128 }],
    [false, { name: 'AES-CBC', length: 25 }],
    [true, { name: 'AES-GCM', length: 128 }],
    [false, { name: 'AES-GCM', length: 25 }],
    [true, { name: 'AES-KW', length: 128 }],
    [false, { name: 'AES-KW', length: 25 }],
    [true, { name: 'HMAC', hash: 'SHA-256' }],
    [true, { name: 'HMAC', hash: 'SHA-256', length: 256 }],
    [false, { name: 'HMAC', hash: 'SHA-256', length: 25 }],
    [false, { name: 'HMAC', hash: 'SHA-256', length: 0 }],
  ],
  'deriveKey': [
    [true,
     { name: 'HKDF', hash: 'SHA-256', salt: Buffer.alloc(0), info: Buffer.alloc(0) },
     { name: 'AES-CBC', length: 128 }],
    [false,
     { name: 'HKDF', hash: 'SHA-256', salt: Buffer.alloc(0), info: Buffer.alloc(0) },
     { name: 'AES-CBC', length: 25 }],

    [true,
     { name: 'HKDF', hash: 'SHA-256', salt: Buffer.alloc(0), info: Buffer.alloc(0) },
     { name: 'HMAC', hash: 'SHA-256', length: 256 }],
  ],
  'deriveBits': [
    [true, { name: 'HKDF', hash: 'SHA-256', salt: Buffer.alloc(0), info: Buffer.alloc(0) }, 8],
    [true, { name: 'HKDF', hash: 'SHA-256', salt: Buffer.alloc(0), info: Buffer.alloc(0) }, 0],
    [false, { name: 'HKDF', hash: 'SHA-256', salt: Buffer.alloc(0), info: Buffer.alloc(0) }, null],
    [false, { name: 'HKDF', hash: 'SHA-256', salt: Buffer.alloc(0), info: Buffer.alloc(0) }, 7],
    [false, { name: 'HKDF', hash: 'Invalid', salt: Buffer.alloc(0), info: Buffer.alloc(0) }, 8],
    [false, { name: 'HKDF', hash: 'Ed25519', salt: Buffer.alloc(0), info: Buffer.alloc(0) }, 8],

    [true, { name: 'PBKDF2', hash: 'SHA-256', salt: Buffer.alloc(0), iterations: 1 }, 8],
    [true, { name: 'PBKDF2', hash: 'SHA-256', salt: Buffer.alloc(0), iterations: 1 }, 0],
    [false, { name: 'PBKDF2', hash: 'SHA-256', salt: Buffer.alloc(0), iterations: 0 }, 8],
    [false, { name: 'PBKDF2', hash: 'SHA-256', salt: Buffer.alloc(0), iterations: 1 }, null],
    [false, { name: 'PBKDF2', hash: 'SHA-256', salt: Buffer.alloc(0), iterations: 1 }, 7],
    [false, { name: 'PBKDF2', hash: 'Invalid', salt: Buffer.alloc(0), iterations: 1 }, 8],
    [false, { name: 'PBKDF2', hash: 'Ed25519', salt: Buffer.alloc(0), iterations: 1 }, 8],

    [true,
     { name: 'ECDH', public: ECDH.publicKey }],
    [false, { name: 'ECDH', public: X448.publicKey }],
    [false, { name: 'ECDH', public: ECDH.privateKey }],
    [false, 'ECDH'],

    [true, { name: 'X25519', public: X25519.publicKey }],
    [false, { name: 'X25519', public: X448.publicKey }],
    [false, { name: 'X25519', public: X25519.privateKey }],
    [false, 'X25519'],

    [true, { name: 'X448', public: X448.publicKey }],
    [false, { name: 'X448', public: X25519.publicKey }],
    [false, { name: 'X448', public: X448.privateKey }],
    [false, 'X448'],
  ],
  'importKey': [
    [false, 'SHA-1'],
    [false, 'Invalid'],
    [true, 'X25519'],
    [true, 'X448'],
    [true, 'Ed25519'],
    [true, 'Ed448'],
    [true, { name: 'HMAC', hash: 'SHA-256' }],
    [true, { name: 'HMAC', hash: 'SHA-256', length: 256 }],
    [false, { name: 'HMAC', hash: 'SHA-256', length: 25 }],
    [true, { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256', ...RSA_KEY_GEN }],
    [true, { name: 'RSA-PSS', hash: 'SHA-256', ...RSA_KEY_GEN }],
    [true, { name: 'RSA-OAEP', hash: 'SHA-256', ...RSA_KEY_GEN }],
    [true, { name: 'ECDSA', namedCurve: 'P-256' }],
    [false, { name: 'ECDSA', namedCurve: 'X25519' }],
    [true, 'AES-CTR'],
    [true, 'AES-CBC'],
    [true, 'AES-GCM'],
    [true, 'AES-KW'],
    [true, { name: 'HMAC', hash: 'SHA-256' }],
    [true, { name: 'HMAC', hash: 'SHA-256', length: 256 }],
    [false, { name: 'HMAC', hash: 'SHA-256', length: 25 }],
    [false, { name: 'HMAC', hash: 'SHA-256', length: 0 }],
    [true, 'HKDF'],
    [true, 'PBKDF2'],
  ],
  'exportKey': [
    [false, 'SHA-1'],
    [false, 'Invalid'],
    [false, 'HKDF'],
    [false, 'PBKDF2'],
    // TODO:
  ],
  'wrapKey': [
    // TODO:
  ],
  'unwrapKey': [
    // TODO:
  ],
  'unsupported operation': [
    [false, ''],
    [false, 'Ed25519'],
  ],
};

vectors.verify = vectors.sign;
vectors.decrypt = vectors.encrypt;

for (const operation of Object.keys(vectors)) {
  for (const [expectation, ...args] of vectors[operation]) {
    assert.strictEqual(
      SubtleCrypto.supports(operation, ...args),
      expectation,
      new Error(
        `expected ${expectation}, got ${!expectation}`,
        { cause: { operation, args } }
      )
    );
  }
}
