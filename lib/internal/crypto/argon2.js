'use strict';

const {
  FunctionPrototypeCall,
} = primordials;

const { Buffer } = require('buffer');

const {
  Argon2Job,
  kCryptoJobAsync,
  kCryptoJobSync,
} = internalBinding('crypto');

const { getArrayBufferOrView } = require('internal/crypto/util');
const { validateFunction, validateInteger } = require('internal/validators');

const {
  codes: {
    ERR_CRYPTO_ARGON2_INVALID_PARAMETER,
    ERR_CRYPTO_ARGON2_NOT_SUPPORTED,
    ERR_CRYPTO_INVALID_SALT_LENGTH,
  },
} = require('internal/errors');

const defaults = {
  algorithm: 'ARGON2ID',
  secret: Buffer.alloc(0),
  ad: Buffer.alloc(0),
  iter: 3,
  lanes: 4,
  memcost: 64 << 10,  // 64Ki * 1 KiB = 64 MiB
};

/**
 * @param {ArrayBufferLike} password
 * @param {ArrayBufferLike} salt
 * @param {number} keylen
 * @param {object} [options]
 * @param {'ARGON2D' | 'ARGON2I' | 'ARGON2ID'} [options.algorithm='ARGON2ID']
 * @param {ArrayBufferLike} [options.secret]
 * @param {ArrayBufferLike} [options.ad]
 * @param {number} [options.iter=3]
 * @param {number} [options.lanes=4]
 * @param {number} [options.memcost=64 << 10]
 * @param {Function} callback
 */
function argon2(password, salt, keylen, options, callback = defaults) {
  if (callback === defaults) {
    callback = options;
    options = defaults;
  }

  options = check(password, salt, keylen, options);
  const { algorithm, secret, ad, iter, lanes, memcost } = options;
  ({ password, salt, keylen } = options);

  validateFunction(callback, 'callback');

  const job = new Argon2Job(
    kCryptoJobAsync, password, salt, algorithm, secret, ad, iter, lanes, memcost, keylen);

  job.ondone = (error, result) => {
    if (error !== undefined)
      return FunctionPrototypeCall(callback, job, error);
    const buf = Buffer.from(result);
    return FunctionPrototypeCall(callback, job, null, buf);
  };

  job.run();
}

/**
 * @param {ArrayBufferLike} password
 * @param {ArrayBufferLike} salt
 * @param {number} keylen
 * @param {object} [options]
 * @param {'ARGON2D' | 'ARGON2I' | 'ARGON2ID'} [options.algorithm='ARGON2ID']
 * @param {ArrayBufferLike} [options.secret]
 * @param {ArrayBufferLike} [options.ad]
 * @param {number} [options.iter=3]
 * @param {number} [options.lanes=4]
 * @param {number} [options.memcost=64 << 10]
 */
function argon2Sync(password, salt, keylen, options = defaults) {
  options = check(password, salt, keylen, options);
  const { algorithm, secret, ad, iter, lanes, memcost } = options;
  ({ password, salt, keylen } = options);
  const job = new Argon2Job(
    kCryptoJobSync, password, salt, algorithm, secret, ad, iter, lanes, memcost, keylen);
  const { 0: err, 1: result } = job.run();

  if (err !== undefined)
    throw err;

  return Buffer.from(result);
}

/**
 * @param {ArrayBufferLike} password
 * @param {ArrayBufferLike} salt
 * @param {number} keylen
 * @param {object} options
 * @param {'ARGON2D' | 'ARGON2I' | 'ARGON2ID'} [options.algorithm='ARGON2ID']
 * @param {ArrayBufferLike} [options.secret]
 * @param {ArrayBufferLike} [options.ad]
 * @param {number} [options.iter=3]
 * @param {number} [options.lanes=4]
 * @param {number} [options.memcost=64 << 10]
 */
function check(password, salt, keylen, options) {
  if (Argon2Job === undefined)
    throw new ERR_CRYPTO_ARGON2_NOT_SUPPORTED();

  password = getArrayBufferOrView(password, 'password');

  salt = getArrayBufferOrView(salt, 'salt');
  if (salt.byteLength < 8 || salt.byteLength > (2 ** 32) - 1) {
    throw new ERR_CRYPTO_INVALID_SALT_LENGTH(8, (2 ** 32) - 1);
  }

  validateInteger(keylen, 'keylen', 4, (2 ** 32) - 1);

  let { algorithm, secret, ad, iter, lanes, memcost } = defaults;
  if (options && options !== defaults) {
    const has_algorithm = options.algorithm !== undefined;
    if (has_algorithm) {
      algorithm = options.algorithm.toUpperCase();
      if (algorithm !== 'ARGON2D' && algorithm !== 'ARGON2I' && algorithm !== 'ARGON2ID')
        throw new ERR_CRYPTO_ARGON2_INVALID_PARAMETER('algorithm', algorithm);
    }
    const has_secret = options.secret !== undefined;
    if (has_secret) {
      secret = getArrayBufferOrView(options.secret);
    }
    const has_ad = options.ad !== undefined;
    if (has_ad) {
      ad = getArrayBufferOrView(options.ad);
    }
    const has_iter = options.iter !== undefined;
    if (has_iter) {
      iter = options.iter;
      validateInteger(iter, 'iter', 1, (2 ** 32) - 1);
    }
    const has_lanes = options.lanes !== undefined;
    if (has_lanes) {
      lanes = options.lanes;
      validateInteger(lanes, 'lanes', 1, (2 ** 24) - 1);
    }
    const has_memcost = options.memcost !== undefined;
    if (has_memcost) {
      memcost = options.memcost;
      validateInteger(memcost, 'memcost', 8 * lanes, (2 ** 32) - 1);
    }
  }

  return { password, salt, algorithm, secret, ad, keylen, iter, lanes, memcost };
}

module.exports = {
  argon2,
  argon2Sync,
};
