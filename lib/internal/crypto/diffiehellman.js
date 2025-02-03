'use strict';

const {
  ArrayBufferPrototypeSlice,
  MathCeil,
  ObjectDefineProperty,
  SafeSet,
  Uint8Array,
} = primordials;

const { Buffer } = require('buffer');

const {
  DiffieHellman: _DiffieHellman,
  DiffieHellmanGroup: _DiffieHellmanGroup,
  ECDH: _ECDH,
  ECDHBitsJob,
  ECDHConvertKey: _ECDHConvertKey,
  statelessDH,
  kCryptoJobAsync,
} = internalBinding('crypto');

const {
  codes: {
    ERR_CRYPTO_ECDH_INVALID_FORMAT,
    ERR_CRYPTO_ECDH_INVALID_PUBLIC_KEY,
    ERR_CRYPTO_INCOMPATIBLE_KEY,
    ERR_CRYPTO_INVALID_KEY_OBJECT_TYPE,
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
  },
} = require('internal/errors');

const {
  validateInt32,
  validateObject,
  validateString,
} = require('internal/validators');

const {
  isArrayBufferView,
  isAnyArrayBuffer,
} = require('internal/util/types');

const {
  lazyDOMException,
} = require('internal/util');

const {
  KeyObject,
} = require('internal/crypto/keys');

const {
  getArrayBufferOrView,
  jobPromise,
  toBuf,
  kHandle,
  kKeyObject,
} = require('internal/crypto/util');

const {
  crypto: {
    POINT_CONVERSION_COMPRESSED,
    POINT_CONVERSION_HYBRID,
    POINT_CONVERSION_UNCOMPRESSED,
  },
} = internalBinding('constants');

const DH_GENERATOR = 2;

function DiffieHellman(sizeOrKey, keyEncoding, generator, genEncoding) {
  if (!(this instanceof DiffieHellman))
    return new DiffieHellman(sizeOrKey, keyEncoding, generator, genEncoding);

  if (typeof sizeOrKey !== 'number' &&
      typeof sizeOrKey !== 'string' &&
      !isArrayBufferView(sizeOrKey) &&
      !isAnyArrayBuffer(sizeOrKey)) {
    throw new ERR_INVALID_ARG_TYPE(
      'sizeOrKey',
      ['number', 'string', 'ArrayBuffer', 'Buffer', 'TypedArray', 'DataView'],
      sizeOrKey,
    );
  }

  // Sizes < 0 don't make sense but they _are_ accepted (and subsequently
  // rejected with ERR_OSSL_BN_BITS_TOO_SMALL) by OpenSSL. The glue code
  // in node_crypto.cc accepts values that are IsInt32() for that reason
  // and that's why we do that here too.
  if (typeof sizeOrKey === 'number')
    validateInt32(sizeOrKey, 'sizeOrKey');

  if (keyEncoding && !Buffer.isEncoding(keyEncoding) &&
      keyEncoding !== 'buffer') {
    genEncoding = generator;
    generator = keyEncoding;
    keyEncoding = false;
  }

  if (typeof sizeOrKey !== 'number')
    sizeOrKey = toBuf(sizeOrKey, keyEncoding);

  if (!generator) {
    generator = DH_GENERATOR;
  } else if (typeof generator === 'number') {
    validateInt32(generator, 'generator');
  } else if (typeof generator === 'string') {
    generator = toBuf(generator, genEncoding);
  } else if (!isArrayBufferView(generator) && !isAnyArrayBuffer(generator)) {
    throw new ERR_INVALID_ARG_TYPE(
      'generator',
      ['number', 'string', 'ArrayBuffer', 'Buffer', 'TypedArray', 'DataView'],
      generator,
    );
  }


  this[kHandle] = new _DiffieHellman(sizeOrKey, generator);
  ObjectDefineProperty(this, 'verifyError', {
    __proto__: null,
    enumerable: true,
    value: this[kHandle].verifyError,
    writable: false,
  });
}


function DiffieHellmanGroup(name) {
  if (!(this instanceof DiffieHellmanGroup))
    return new DiffieHellmanGroup(name);
  this[kHandle] = new _DiffieHellmanGroup(name);
  ObjectDefineProperty(this, 'verifyError', {
    __proto__: null,
    enumerable: true,
    value: this[kHandle].verifyError,
    writable: false,
  });
}


DiffieHellmanGroup.prototype.generateKeys =
    DiffieHellman.prototype.generateKeys =
    dhGenerateKeys;

function dhGenerateKeys(encoding) {
  const keys = this[kHandle].generateKeys();
  return encode(keys, encoding);
}


DiffieHellmanGroup.prototype.computeSecret =
    DiffieHellman.prototype.computeSecret =
    dhComputeSecret;

function dhComputeSecret(key, inEnc, outEnc) {
  key = getArrayBufferOrView(key, 'key', inEnc);
  const ret = this[kHandle].computeSecret(key);
  if (typeof ret === 'string')
    throw new ERR_CRYPTO_ECDH_INVALID_PUBLIC_KEY();
  return encode(ret, outEnc);
}


DiffieHellmanGroup.prototype.getPrime =
    DiffieHellman.prototype.getPrime =
    dhGetPrime;

function dhGetPrime(encoding) {
  const prime = this[kHandle].getPrime();
  return encode(prime, encoding);
}


DiffieHellmanGroup.prototype.getGenerator =
    DiffieHellman.prototype.getGenerator =
    dhGetGenerator;

function dhGetGenerator(encoding) {
  const generator = this[kHandle].getGenerator();
  return encode(generator, encoding);
}


DiffieHellmanGroup.prototype.getPublicKey =
    DiffieHellman.prototype.getPublicKey =
    dhGetPublicKey;

function dhGetPublicKey(encoding) {
  const key = this[kHandle].getPublicKey();
  return encode(key, encoding);
}


DiffieHellmanGroup.prototype.getPrivateKey =
    DiffieHellman.prototype.getPrivateKey =
    dhGetPrivateKey;

function dhGetPrivateKey(encoding) {
  const key = this[kHandle].getPrivateKey();
  return encode(key, encoding);
}


DiffieHellman.prototype.setPublicKey = function setPublicKey(key, encoding) {
  key = getArrayBufferOrView(key, 'key', encoding);
  this[kHandle].setPublicKey(key);
  return this;
};


DiffieHellman.prototype.setPrivateKey = function setPrivateKey(key, encoding) {
  key = getArrayBufferOrView(key, 'key', encoding);
  this[kHandle].setPrivateKey(key);
  return this;
};


function ECDH(curve) {
  if (!(this instanceof ECDH))
    return new ECDH(curve);

  validateString(curve, 'curve');
  this[kHandle] = new _ECDH(curve);
}

ECDH.prototype.computeSecret = DiffieHellman.prototype.computeSecret;
ECDH.prototype.setPrivateKey = DiffieHellman.prototype.setPrivateKey;
ECDH.prototype.setPublicKey = DiffieHellman.prototype.setPublicKey;
ECDH.prototype.getPrivateKey = DiffieHellman.prototype.getPrivateKey;

ECDH.prototype.generateKeys = function generateKeys(encoding, format) {
  this[kHandle].generateKeys();

  return this.getPublicKey(encoding, format);
};

ECDH.prototype.getPublicKey = function getPublicKey(encoding, format) {
  const f = getFormat(format);
  const key = this[kHandle].getPublicKey(f);
  return encode(key, encoding);
};

ECDH.convertKey = function convertKey(key, curve, inEnc, outEnc, format) {
  validateString(curve, 'curve');
  key = getArrayBufferOrView(key, 'key', inEnc);
  const f = getFormat(format);
  const convertedKey = _ECDHConvertKey(key, curve, f);
  return encode(convertedKey, outEnc);
};

function encode(buffer, encoding) {
  if (encoding && encoding !== 'buffer')
    buffer = buffer.toString(encoding);
  return buffer;
}

function getFormat(format) {
  if (format) {
    if (format === 'compressed')
      return POINT_CONVERSION_COMPRESSED;
    if (format === 'hybrid')
      return POINT_CONVERSION_HYBRID;
    if (format !== 'uncompressed')
      throw new ERR_CRYPTO_ECDH_INVALID_FORMAT(format);
  }
  return POINT_CONVERSION_UNCOMPRESSED;
}

const dhEnabledKeyTypes = new SafeSet(['dh', 'ec', 'x448', 'x25519']);

function diffieHellman(options) {
  validateObject(options, 'options');

  const { privateKey, publicKey } = options;
  if (!(privateKey instanceof KeyObject))
    throw new ERR_INVALID_ARG_VALUE('options.privateKey', privateKey);

  if (!(publicKey instanceof KeyObject))
    throw new ERR_INVALID_ARG_VALUE('options.publicKey', publicKey);

  if (privateKey.type !== 'private')
    throw new ERR_CRYPTO_INVALID_KEY_OBJECT_TYPE(privateKey.type, 'private');

  if (publicKey.type !== 'public' && publicKey.type !== 'private') {
    throw new ERR_CRYPTO_INVALID_KEY_OBJECT_TYPE(publicKey.type,
                                                 'private or public');
  }

  const privateType = privateKey.asymmetricKeyType;
  const publicType = publicKey.asymmetricKeyType;
  if (privateType !== publicType || !dhEnabledKeyTypes.has(privateType)) {
    throw new ERR_CRYPTO_INCOMPATIBLE_KEY('key types for Diffie-Hellman',
                                          `${privateType} and ${publicType}`);
  }

  return statelessDH(privateKey[kHandle], publicKey[kHandle]);
}

let masks;

// The ecdhDeriveBits function is part of the Web Crypto API and serves both
// deriveKeys and deriveBits functions.
async function ecdhDeriveBits(algorithm, baseKey, length) {
  const { 'public': key } = algorithm;

  if (key.type !== 'public') {
    throw lazyDOMException(
      'algorithm.public must be a public key', 'InvalidAccessError');
  }
  if (baseKey.type !== 'private') {
    throw lazyDOMException(
      'baseKey must be a private key', 'InvalidAccessError');
  }

  if (
    key.algorithm.name !== 'ECDH' &&
    key.algorithm.name !== 'X25519' &&
    key.algorithm.name !== 'X448'
  ) {
    throw lazyDOMException('Keys must be ECDH, X25519, or X448 keys', 'InvalidAccessError');
  }

  if (key.algorithm.name !== baseKey.algorithm.name) {
    throw lazyDOMException(
      'The public and private keys must be of the same type',
      'InvalidAccessError');
  }

  if (
    key.algorithm.name === 'ECDH' &&
    key.algorithm.namedCurve !== baseKey.algorithm.namedCurve
  ) {
    throw lazyDOMException('Named curve mismatch', 'InvalidAccessError');
  }

  const bits = await jobPromise(() => new ECDHBitsJob(
    kCryptoJobAsync,
    key[kKeyObject][kHandle],
    baseKey[kKeyObject][kHandle]));

  // If a length is not specified, return the full derived secret
  if (length === null)
    return bits;

  // If the length is not a multiple of 8 the nearest ceiled
  // multiple of 8 is sliced.
  const sliceLength = MathCeil(length / 8);

  const { byteLength } = bits;
  // If the length is larger than the derived secret, throw.
  if (byteLength < sliceLength)
    throw lazyDOMException('derived bit length is too small', 'OperationError');

  const slice = ArrayBufferPrototypeSlice(bits, 0, sliceLength);

  const mod = length % 8;
  if (mod === 0)
    return slice;

  // eslint-disable-next-line no-sparse-arrays
  masks ||= [, 0b10000000, 0b11000000, 0b11100000, 0b11110000, 0b11111000, 0b11111100, 0b11111110];

  const masked = new Uint8Array(slice);
  masked[sliceLength - 1] = masked[sliceLength - 1] & masks[mod];
  return masked.buffer;
}

module.exports = {
  DiffieHellman,
  DiffieHellmanGroup,
  ECDH,
  diffieHellman,
  ecdhDeriveBits,
};
