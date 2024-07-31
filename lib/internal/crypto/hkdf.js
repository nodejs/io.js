'use strict';

const {
  FunctionPrototypeCall,
} = primordials;

const {
  HKDFJob,
  kCryptoJobAsync,
  kCryptoJobSync,
} = internalBinding('crypto');

const {
  validateFunction,
  validateInteger,
  validateString,
} = require('internal/validators');

const { kMaxLength } = require('buffer');

const {
  normalizeHashName,
  toBuf,
  validateByteSource,
  kKeyObject,
} = require('internal/crypto/util');

const {
  createSecretKey,
  isKeyObject,
} = require('internal/crypto/keys');

const {
  lazyDOMException,
  promisify,
} = require('internal/util');

const {
  isAnyArrayBuffer,
  isArrayBufferView,
} = require('internal/util/types');

const {
  codes: {
    ERR_INVALID_ARG_TYPE,
    ERR_OUT_OF_RANGE,
  },
  hideStackFrames,
} = require('internal/errors');

const validateParameters = hideStackFrames((hash, key, salt, info, length) => {
  validateString.withoutStackTrace(hash, 'digest');
  key = prepareKey(key);
  salt = validateByteSource.withoutStackTrace(salt, 'salt');
  info = validateByteSource.withoutStackTrace(info, 'info');

  validateInteger.withoutStackTrace(length, 'length', 0, kMaxLength);

  if (info.byteLength > 1024) {
    throw new ERR_OUT_OF_RANGE.HideStackFramesError(
      'info',
      'must not contain more than 1024 bytes',
      info.byteLength);
  }

  return {
    hash,
    key,
    salt,
    info,
    length,
  };
});

function prepareKey(key) {
  if (isKeyObject(key))
    return key;

  if (isAnyArrayBuffer(key))
    return createSecretKey(key);

  key = toBuf(key);

  if (!isArrayBufferView(key)) {
    throw new ERR_INVALID_ARG_TYPE(
      'ikm',
      [
        'string',
        'SecretKeyObject',
        'ArrayBuffer',
        'TypedArray',
        'DataView',
        'Buffer',
      ],
      key);
  }

  return createSecretKey(key);
}

function hkdf(hash, key, salt, info, length, callback) {
  ({
    hash,
    key,
    salt,
    info,
    length,
  } = validateParameters(hash, key, salt, info, length));

  validateFunction(callback, 'callback');

  const job = new HKDFJob(kCryptoJobAsync, hash, key, salt, info, length);

  job.ondone = (error, bits) => {
    if (error) return FunctionPrototypeCall(callback, job, error);
    FunctionPrototypeCall(callback, job, null, bits);
  };

  job.run();
}

function hkdfSync(hash, key, salt, info, length) {
  ({
    hash,
    key,
    salt,
    info,
    length,
  } = validateParameters(hash, key, salt, info, length));

  const job = new HKDFJob(kCryptoJobSync, hash, key, salt, info, length);
  const { 0: err, 1: bits } = job.run();
  if (err !== undefined)
    throw err;

  return bits;
}

const hkdfPromise = promisify(hkdf);
async function hkdfDeriveBits(algorithm, baseKey, length) {
  const { hash, salt, info } = algorithm;

  if (length === 0)
    throw lazyDOMException('length cannot be zero', 'OperationError');
  if (length === null)
    throw lazyDOMException('length cannot be null', 'OperationError');
  if (length % 8) {
    throw lazyDOMException(
      'length must be a multiple of 8',
      'OperationError');
  }

  try {
    return await hkdfPromise(
      normalizeHashName(hash.name), baseKey[kKeyObject], salt, info, length / 8,
    );
  } catch (err) {
    throw lazyDOMException(
      'The operation failed for an operation-specific reason',
      { name: 'OperationError', cause: err });
  }
}

module.exports = {
  hkdf,
  hkdfSync,
  hkdfDeriveBits,
};
