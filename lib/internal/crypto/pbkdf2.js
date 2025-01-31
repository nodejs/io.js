'use strict';

const {
  ArrayBuffer,
  FunctionPrototypeCall,
} = primordials;

const { Buffer } = require('buffer');

const {
  PBKDF2Job,
  kCryptoJobAsync,
  kCryptoJobSync,
} = internalBinding('crypto');

const {
  validateFunction,
  validateInt32,
  validateString,
} = require('internal/validators');

const {
  getArrayBufferOrView,
  normalizeHashName,
  kKeyObject,
} = require('internal/crypto/util');

const {
  lazyDOMException,
  promisify,
} = require('internal/util');

function pbkdf2(password, salt, iterations, keylen, digest, callback) {
  if (typeof digest === 'function') {
    callback = digest;
    digest = undefined;
  }

  ({ password, salt, iterations, keylen, digest } =
    check(password, salt, iterations, keylen, digest));

  validateFunction(callback, 'callback');

  const job = new PBKDF2Job(
    kCryptoJobAsync,
    password,
    salt,
    iterations,
    keylen,
    digest);

  job.ondone = (err, result) => {
    if (err !== undefined)
      return FunctionPrototypeCall(callback, job, err);
    const buf = Buffer.from(result);
    return FunctionPrototypeCall(callback, job, null, buf);
  };

  job.run();
}

function pbkdf2Sync(password, salt, iterations, keylen, digest) {
  ({ password, salt, iterations, keylen, digest } =
    check(password, salt, iterations, keylen, digest));

  const job = new PBKDF2Job(
    kCryptoJobSync,
    password,
    salt,
    iterations,
    keylen,
    digest);

  const { 0: err, 1: result } = job.run();
  if (err !== undefined)
    throw err;

  return Buffer.from(result);
}

function check(password, salt, iterations, keylen, digest) {
  validateString(digest, 'digest');

  password = getArrayBufferOrView(password, 'password');
  salt = getArrayBufferOrView(salt, 'salt');
  // OpenSSL uses a signed int to represent these values, so we are restricted
  // to the 31-bit range here (which is plenty).
  validateInt32(iterations, 'iterations', 1);
  validateInt32(keylen, 'keylen', 0);

  return { password, salt, iterations, keylen, digest };
}

const pbkdf2Promise = promisify(pbkdf2);
async function pbkdf2DeriveBits(algorithm, baseKey, length) {
  const { iterations, hash, salt } = algorithm;
  if (iterations === 0)
    throw lazyDOMException(
      'iterations cannot be zero',
      'OperationError');

  if (length === 0)
    return new ArrayBuffer(0);
  if (length === null)
    throw lazyDOMException('length cannot be null', 'OperationError');
  if (length % 8) {
    throw lazyDOMException(
      'length must be a multiple of 8',
      'OperationError');
  }

  let result;
  try {
    result = await pbkdf2Promise(
      baseKey[kKeyObject].export(), salt, iterations, length / 8, normalizeHashName(hash.name),
    );
  } catch (err) {
    throw lazyDOMException(
      'The operation failed for an operation-specific reason',
      { name: 'OperationError', cause: err });
  }

  return result.buffer;
}

module.exports = {
  pbkdf2,
  pbkdf2Sync,
  pbkdf2DeriveBits,
};
