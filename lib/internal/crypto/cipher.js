'use strict';

const {
  ObjectSetPrototypeOf,
  ReflectApply,
  StringPrototypeToLowerCase,
} = primordials;

const {
  CipherBase,
  privateDecrypt: _privateDecrypt,
  privateEncrypt: _privateEncrypt,
  publicDecrypt: _publicDecrypt,
  publicEncrypt: _publicEncrypt,
  getCipherInfo: _getCipherInfo,
} = internalBinding('crypto');

const {
  crypto: {
    RSA_PKCS1_OAEP_PADDING,
    RSA_PKCS1_PADDING,
  },
} = internalBinding('constants');

const {
  codes: {
    ERR_CRYPTO_INVALID_STATE,
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_UNKNOWN_ENCODING,
  },
} = require('internal/errors');

const {
  validateEncoding,
  validateInt32,
  validateObject,
  validateString,
} = require('internal/validators');

const {
  preparePrivateKey,
  preparePublicOrPrivateKey,
  prepareSecretKey,
} = require('internal/crypto/keys');

const {
  getArrayBufferOrView,
  getStringOption,
  kHandle,
} = require('internal/crypto/util');

const {
  isArrayBufferView,
} = require('internal/util/types');

const assert = require('internal/assert');

const LazyTransform = require('internal/streams/lazy_transform');

const { normalizeEncoding } = require('internal/util');

const { StringDecoder } = require('string_decoder');

function rsaFunctionFor(method, defaultPadding, keyType) {
  return (options, buffer) => {
    const { format, type, data, passphrase } =
      keyType === 'private' ?
        preparePrivateKey(options) :
        preparePublicOrPrivateKey(options);
    const padding = options.padding || defaultPadding;
    const { oaepHash, encoding } = options;
    let { oaepLabel } = options;
    if (oaepHash !== undefined)
      validateString(oaepHash, 'key.oaepHash');
    if (oaepLabel !== undefined)
      oaepLabel = getArrayBufferOrView(oaepLabel, 'key.oaepLabel', encoding);
    buffer = getArrayBufferOrView(buffer, 'buffer', encoding);
    return method(data, format, type, passphrase, buffer, padding, oaepHash,
                  oaepLabel);
  };
}

const publicEncrypt = rsaFunctionFor(_publicEncrypt, RSA_PKCS1_OAEP_PADDING,
                                     'public');
const publicDecrypt = rsaFunctionFor(_publicDecrypt, RSA_PKCS1_PADDING,
                                     'public');
const privateEncrypt = rsaFunctionFor(_privateEncrypt, RSA_PKCS1_PADDING,
                                      'private');
const privateDecrypt = rsaFunctionFor(_privateDecrypt, RSA_PKCS1_OAEP_PADDING,
                                      'private');

function getDecoder(decoder, encoding) {
  const normalizedEncoding = normalizeEncoding(encoding);
  decoder ||= new StringDecoder(encoding);
  if (decoder.encoding !== normalizedEncoding) {
    if (normalizedEncoding === undefined) {
      throw new ERR_UNKNOWN_ENCODING(encoding);
    }
    assert(false, 'Cannot change encoding');
  }
  return decoder;
}

function getUIntOption(options, key) {
  let value;
  if (options && (value = options[key]) != null) {
    if (value >>> 0 !== value)
      throw new ERR_INVALID_ARG_VALUE(`options.${key}`, value);
    return value;
  }
  return -1;
}

function createCipherBase(cipher, credential, options, decipher, iv) {
  const authTagLength = getUIntOption(options, 'authTagLength');
  this[kHandle] = new CipherBase(decipher);
  if (iv === undefined) {
    this[kHandle].init(cipher, credential, authTagLength);
  } else {
    this[kHandle].initiv(cipher, credential, iv, authTagLength);
  }
  this._decoder = null;

  ReflectApply(LazyTransform, this, [options]);
}

function createCipherWithIV(cipher, key, options, decipher, iv) {
  validateString(cipher, 'cipher');
  const encoding = getStringOption(options, 'encoding');
  key = prepareSecretKey(key, encoding);
  iv = iv === null ? null : getArrayBufferOrView(iv, 'iv');
  ReflectApply(createCipherBase, this, [cipher, key, options, decipher, iv]);
}

// The Cipher class is part of the legacy Node.js crypto API. It exposes
// a stream-based encryption/decryption model. For backwards compatibility
// the Cipher class is defined using the legacy function syntax rather than
// ES6 classes.

function Cipher(cipher, password, options) {
  if (!(this instanceof Cipher))
    return new Cipher(cipher, password, options);
}

ObjectSetPrototypeOf(Cipher.prototype, LazyTransform.prototype);
ObjectSetPrototypeOf(Cipher, LazyTransform);

Cipher.prototype._transform = function _transform(chunk, encoding, callback) {
  this.push(this[kHandle].update(chunk, encoding));
  callback();
};

Cipher.prototype._flush = function _flush(callback) {
  try {
    this.push(this[kHandle].final());
  } catch (e) {
    callback(e);
    return;
  }
  callback();
};

Cipher.prototype.update = function update(data, inputEncoding, outputEncoding) {
  if (typeof data === 'string') {
    validateEncoding(data, inputEncoding);
  } else if (!isArrayBufferView(data)) {
    throw new ERR_INVALID_ARG_TYPE(
      'data', ['string', 'Buffer', 'TypedArray', 'DataView'], data);
  }

  const ret = this[kHandle].update(data, inputEncoding);

  if (outputEncoding && outputEncoding !== 'buffer') {
    this._decoder = getDecoder(this._decoder, outputEncoding);
    return this._decoder.write(ret);
  }

  return ret;
};


Cipher.prototype.final = function final(outputEncoding) {
  const ret = this[kHandle].final();

  if (outputEncoding && outputEncoding !== 'buffer') {
    this._decoder = getDecoder(this._decoder, outputEncoding);
    return this._decoder.end(ret);
  }

  return ret;
};


Cipher.prototype.setAutoPadding = function setAutoPadding(ap) {
  if (!this[kHandle].setAutoPadding(!!ap))
    throw new ERR_CRYPTO_INVALID_STATE('setAutoPadding');
  return this;
};

Cipher.prototype.getAuthTag = function getAuthTag() {
  const ret = this[kHandle].getAuthTag();
  if (ret === undefined)
    throw new ERR_CRYPTO_INVALID_STATE('getAuthTag');
  return ret;
};


function setAuthTag(tagbuf, encoding) {
  tagbuf = getArrayBufferOrView(tagbuf, 'buffer', encoding);
  if (!this[kHandle].setAuthTag(tagbuf))
    throw new ERR_CRYPTO_INVALID_STATE('setAuthTag');
  return this;
}

Cipher.prototype.setAAD = function setAAD(aadbuf, options) {
  const encoding = getStringOption(options, 'encoding');
  const plaintextLength = getUIntOption(options, 'plaintextLength');
  aadbuf = getArrayBufferOrView(aadbuf, 'aadbuf', encoding);
  if (!this[kHandle].setAAD(aadbuf, plaintextLength))
    throw new ERR_CRYPTO_INVALID_STATE('setAAD');
  return this;
};

// The Cipheriv class is part of the legacy Node.js crypto API. It exposes
// a stream-based encryption/decryption model. For backwards compatibility
// the Cipheriv class is defined using the legacy function syntax rather than
// ES6 classes.

function Cipheriv(cipher, key, iv, options) {
  if (!(this instanceof Cipheriv))
    return new Cipheriv(cipher, key, iv, options);

  ReflectApply(createCipherWithIV, this, [cipher, key, options, true, iv]);
}

function addCipherPrototypeFunctions(constructor) {
  constructor.prototype._transform = Cipher.prototype._transform;
  constructor.prototype._flush = Cipher.prototype._flush;
  constructor.prototype.update = Cipher.prototype.update;
  constructor.prototype.final = Cipher.prototype.final;
  constructor.prototype.setAutoPadding = Cipher.prototype.setAutoPadding;
  if (constructor === Cipheriv) {
    constructor.prototype.getAuthTag = Cipher.prototype.getAuthTag;
  } else {
    constructor.prototype.setAuthTag = setAuthTag;
  }
  constructor.prototype.setAAD = Cipher.prototype.setAAD;
}

ObjectSetPrototypeOf(Cipheriv.prototype, LazyTransform.prototype);
ObjectSetPrototypeOf(Cipheriv, LazyTransform);
addCipherPrototypeFunctions(Cipheriv);

// The Decipher class is part of the legacy Node.js crypto API. It exposes
// a stream-based encryption/decryption model. For backwards compatibility
// the Decipher class is defined using the legacy function syntax rather than
// ES6 classes.

function Decipher(cipher, password, options) {
  if (!(this instanceof Decipher))
    return new Decipher(cipher, password, options);
}

ObjectSetPrototypeOf(Decipher.prototype, LazyTransform.prototype);
ObjectSetPrototypeOf(Decipher, LazyTransform);
addCipherPrototypeFunctions(Decipher);

// The Decipheriv class is part of the legacy Node.js crypto API. It exposes
// a stream-based encryption/decryption model. For backwards compatibility
// the Decipheriv class is defined using the legacy function syntax rather than
// ES6 classes.

function Decipheriv(cipher, key, iv, options) {
  if (!(this instanceof Decipheriv))
    return new Decipheriv(cipher, key, iv, options);

  ReflectApply(createCipherWithIV, this, [cipher, key, options, false, iv]);
}

ObjectSetPrototypeOf(Decipheriv.prototype, LazyTransform.prototype);
ObjectSetPrototypeOf(Decipheriv, LazyTransform);
addCipherPrototypeFunctions(Decipheriv);

function getCipherInfo(nameOrNid, options) {
  if (typeof nameOrNid !== 'string' && typeof nameOrNid !== 'number') {
    throw new ERR_INVALID_ARG_TYPE(
      'nameOrNid',
      ['string', 'number'],
      nameOrNid);
  }
  if (typeof nameOrNid === 'number')
    validateInt32(nameOrNid, 'nameOrNid');
  let keyLength, ivLength;
  if (options !== undefined) {
    validateObject(options, 'options');
    ({ keyLength, ivLength } = options);
    if (keyLength !== undefined)
      validateInt32(keyLength, 'options.keyLength');
    if (ivLength !== undefined)
      validateInt32(ivLength, 'options.ivLength');
  }

  const ret = _getCipherInfo({}, nameOrNid, keyLength, ivLength);
  if (ret !== undefined) {
    ret.name &&= StringPrototypeToLowerCase(ret.name);
    ret.type &&= StringPrototypeToLowerCase(ret.type);
  }
  return ret;
}

module.exports = {
  Cipheriv,
  Decipheriv,
  privateDecrypt,
  privateEncrypt,
  publicDecrypt,
  publicEncrypt,
  getCipherInfo,
};
