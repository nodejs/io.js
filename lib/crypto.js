// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

// Note: In 0.8 and before, crypto functions all defaulted to using
// binary-encoded strings rather than buffers.

'use strict';

const {
  ObjectDefineProperties,
  ObjectDefineProperty,
} = primordials;

const {
  assertCrypto,
  deprecate,
} = require('internal/util');
assertCrypto();

const {
  ERR_CRYPTO_FIPS_FORCED,
  ERR_WORKER_UNSUPPORTED_OPERATION,
} = require('internal/errors').codes;
const constants = internalBinding('constants').crypto;
const { getOptionValue } = require('internal/options');
const {
  getFipsCrypto,
  setFipsCrypto,
  timingSafeEqual,
} = internalBinding('crypto');
const {
  checkPrime,
  checkPrimeSync,
  generatePrime,
  generatePrimeSync,
  randomBytes,
  randomFill,
  randomFillSync,
  randomInt,
  randomUUID,
} = require('internal/crypto/random');
const {
  pbkdf2,
  pbkdf2Sync,
} = require('internal/crypto/pbkdf2');
const {
  scrypt,
  scryptSync,
} = require('internal/crypto/scrypt');
const {
  hkdf,
  hkdfSync,
} = require('internal/crypto/hkdf');
const {
  generateKeyPair,
  generateKeyPairSync,
  generateKey,
  generateKeySync,
} = require('internal/crypto/keygen');
const {
  createSecretKey,
  createPublicKey,
  createPrivateKey,
  KeyObject,
} = require('internal/crypto/keys');
const {
  DiffieHellman,
  DiffieHellmanGroup,
  ECDH,
  diffieHellman,
} = require('internal/crypto/diffiehellman');
const {
  Cipheriv,
  Decipheriv,
  privateDecrypt,
  privateEncrypt,
  publicDecrypt,
  publicEncrypt,
  getCipherInfo,
} = require('internal/crypto/cipher');
const {
  Sign,
  signOneShot,
  Verify,
  verifyOneShot,
} = require('internal/crypto/sig');
const {
  Hash,
  Hmac,
  hash,
} = require('internal/crypto/hash');
const {
  X509Certificate,
} = require('internal/crypto/x509');
const {
  getCiphers,
  getCurves,
  getHashes,
  setEngine,
  secureHeapUsed,
} = require('internal/crypto/util');
const Certificate = require('internal/crypto/certificate');

let webcrypto;
function lazyWebCrypto() {
  webcrypto ??= require('internal/crypto/webcrypto');
  return webcrypto;
}

let ownsProcessState;
function lazyOwnsProcessState() {
  ownsProcessState ??= require('internal/worker').ownsProcessState;
  return ownsProcessState;
}

// These helper functions are needed because the constructors can
// use new, in which case V8 cannot inline the recursive constructor call
function createHash(algorithm, options) {
  return new Hash(algorithm, options);
}

function createCipheriv(cipher, key, iv, options) {
  return new Cipheriv(cipher, key, iv, options);
}

function createDecipheriv(cipher, key, iv, options) {
  return new Decipheriv(cipher, key, iv, options);
}

function createDiffieHellman(sizeOrKey, keyEncoding, generator, genEncoding) {
  return new DiffieHellman(sizeOrKey, keyEncoding, generator, genEncoding);
}

function createDiffieHellmanGroup(name) {
  return new DiffieHellmanGroup(name);
}

function createECDH(curve) {
  return new ECDH(curve);
}

function createHmac(hmac, key, options) {
  return new Hmac(hmac, key, options);
}

function createSign(algorithm, options) {
  return new Sign(algorithm, options);
}

function createVerify(algorithm, options) {
  return new Verify(algorithm, options);
}

module.exports = {
  // Methods
  checkPrime,
  checkPrimeSync,
  createCipheriv,
  createDecipheriv,
  createDiffieHellman,
  createDiffieHellmanGroup,
  createECDH,
  createHash,
  createHmac,
  createPrivateKey,
  createPublicKey,
  createSecretKey,
  createSign,
  createVerify,
  diffieHellman,
  generatePrime,
  generatePrimeSync,
  getCiphers,
  getCipherInfo,
  getCurves,
  getDiffieHellman: createDiffieHellmanGroup,
  getHashes,
  hkdf,
  hkdfSync,
  pbkdf2,
  pbkdf2Sync,
  generateKeyPair,
  generateKeyPairSync,
  generateKey,
  generateKeySync,
  privateDecrypt,
  privateEncrypt,
  publicDecrypt,
  publicEncrypt,
  randomBytes,
  randomFill,
  randomFillSync,
  randomInt,
  randomUUID,
  scrypt,
  scryptSync,
  sign: signOneShot,
  setEngine,
  timingSafeEqual,
  getFips,
  setFips,
  verify: verifyOneShot,
  hash,

  // Classes
  Certificate,
  Cipheriv,
  Decipheriv,
  DiffieHellman,
  DiffieHellmanGroup,
  ECDH,
  Hash: deprecate(Hash, 'crypto.Hash constructor is deprecated.', 'DEP0179'),
  Hmac: deprecate(Hmac, 'crypto.Hmac constructor is deprecated.', 'DEP0181'),
  KeyObject,
  Sign,
  Verify,
  X509Certificate,
  secureHeapUsed,
};

function getFips() {
  return getOptionValue('--force-fips') ? 1 : getFipsCrypto();
}

function setFips(val) {
  if (getOptionValue('--force-fips')) {
    if (val) return;
    throw new ERR_CRYPTO_FIPS_FORCED();
  } else {
    if (!lazyOwnsProcessState()) {
      throw new ERR_WORKER_UNSUPPORTED_OPERATION('Calling crypto.setFips()');
    }
    setFipsCrypto(val);
  }
}

function getRandomValues(array) {
  return lazyWebCrypto().crypto.getRandomValues(array);
}

ObjectDefineProperty(constants, 'defaultCipherList', {
  __proto__: null,
  get() {
    const value = getOptionValue('--tls-cipher-list');
    ObjectDefineProperty(this, 'defaultCipherList', {
      __proto__: null,
      writable: true,
      configurable: true,
      enumerable: true,
      value,
    });
    return value;
  },
  set(val) {
    ObjectDefineProperty(this, 'defaultCipherList', {
      __proto__: null,
      writable: true,
      configurable: true,
      enumerable: true,
      value: val,
    });
  },
  configurable: true,
  enumerable: true,
});

function getRandomBytesAlias(key) {
  return {
    enumerable: false,
    configurable: true,
    get() {
      let value;
      if (getOptionValue('--pending-deprecation')) {
        value = deprecate(
          randomBytes,
          `crypto.${key} is deprecated.`,
          'DEP0115');
      } else {
        value = randomBytes;
      }
      ObjectDefineProperty(
        this,
        key,
        {
          __proto__: null,
          enumerable: false,
          configurable: true,
          writable: true,
          value: value,
        },
      );
      return value;
    },
    set(value) {
      ObjectDefineProperty(
        this,
        key,
        {
          __proto__: null,
          enumerable: true,
          configurable: true,
          writable: true,
          value,
        },
      );
    },
  };
}

ObjectDefineProperties(module.exports, {
  fips: {
    __proto__: null,
    get: deprecate(getFips, 'The crypto.fips is deprecated. ' +
      'Please use crypto.getFips()', 'DEP0093'),
    set: deprecate(setFips, 'The crypto.fips is deprecated. ' +
      'Please use crypto.setFips()', 'DEP0093'),
  },
  constants: {
    __proto__: null,
    configurable: false,
    enumerable: true,
    value: constants,
  },

  webcrypto: {
    __proto__: null,
    configurable: false,
    enumerable: true,
    get() { return lazyWebCrypto().crypto; },
    set: undefined,
  },

  subtle: {
    __proto__: null,
    configurable: false,
    enumerable: true,
    get() { return lazyWebCrypto().crypto.subtle; },
    set: undefined,
  },

  getRandomValues: {
    __proto__: null,
    configurable: false,
    enumerable: true,
    get: () => getRandomValues,
    set: undefined,
  },

  // Aliases for randomBytes are deprecated.
  // The ecosystem needs those to exist for backwards compatibility.
  prng: getRandomBytesAlias('prng'),
  pseudoRandomBytes: getRandomBytesAlias('pseudoRandomBytes'),
  rng: getRandomBytesAlias('rng'),
});
