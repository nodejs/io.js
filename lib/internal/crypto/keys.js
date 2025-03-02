'use strict';

const {
  ArrayPrototypeSlice,
  ObjectDefineProperties,
  ObjectDefineProperty,
  ObjectSetPrototypeOf,
  SafeSet,
  Symbol,
  SymbolToStringTag,
  Uint8Array,
} = primordials;

const {
  KeyObjectHandle,
  createNativeKeyObjectClass,
  kKeyTypeSecret,
  kKeyTypePublic,
  kKeyTypePrivate,
  kKeyFormatPEM,
  kKeyFormatDER,
  kKeyFormatJWK,
  kKeyEncodingPKCS1,
  kKeyEncodingPKCS8,
  kKeyEncodingSPKI,
  kKeyEncodingSEC1,
} = internalBinding('crypto');

const {
  validateObject,
  validateOneOf,
  validateString,
} = require('internal/validators');

const {
  codes: {
    ERR_CRYPTO_INCOMPATIBLE_KEY_OPTIONS,
    ERR_CRYPTO_INVALID_JWK,
    ERR_CRYPTO_INVALID_KEY_OBJECT_TYPE,
    ERR_ILLEGAL_CONSTRUCTOR,
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_INVALID_THIS,
  },
} = require('internal/errors');

const {
  kHandle,
  kKeyObject,
  getArrayBufferOrView,
  bigIntArrayToUnsignedBigInt,
  normalizeAlgorithm,
  hasAnyNotIn,
} = require('internal/crypto/util');

const {
  isAnyArrayBuffer,
  isArrayBufferView,
} = require('internal/util/types');

const {
  markTransferMode,
  kClone,
  kDeserialize,
} = require('internal/worker/js_transferable');

const {
  customInspectSymbol: kInspect,
  kEnumerableProperty,
  lazyDOMException,
} = require('internal/util');

const { inspect } = require('internal/util/inspect');

const { Buffer } = require('buffer');

const kAlgorithm = Symbol('kAlgorithm');
const kExtractable = Symbol('kExtractable');
const kKeyType = Symbol('kKeyType');
const kKeyUsages = Symbol('kKeyUsages');

// Key input contexts.
const kConsumePublic = 0;
const kConsumePrivate = 1;
const kCreatePublic = 2;
const kCreatePrivate = 3;

const encodingNames = [];
for (const m of [[kKeyEncodingPKCS1, 'pkcs1'], [kKeyEncodingPKCS8, 'pkcs8'],
                 [kKeyEncodingSPKI, 'spki'], [kKeyEncodingSEC1, 'sec1']])
  encodingNames[m[0]] = m[1];

// Creating the KeyObject class is a little complicated due to inheritance
// and the fact that KeyObjects should be transferable between threads,
// which requires the KeyObject base class to be implemented in C++.
// The creation requires a callback to make sure that the NativeKeyObject
// base class cannot exist without the other KeyObject implementations.
const {
  0: KeyObject,
  1: SecretKeyObject,
  2: PublicKeyObject,
  3: PrivateKeyObject,
} = createNativeKeyObjectClass((NativeKeyObject) => {
  // Publicly visible KeyObject class.
  class KeyObject extends NativeKeyObject {
    constructor(type, handle) {
      if (type !== 'secret' && type !== 'public' && type !== 'private')
        throw new ERR_INVALID_ARG_VALUE('type', type);
      if (typeof handle !== 'object' || !(handle instanceof KeyObjectHandle))
        throw new ERR_INVALID_ARG_TYPE('handle', 'object', handle);

      super(handle);

      this[kKeyType] = type;

      ObjectDefineProperty(this, kHandle, {
        __proto__: null,
        value: handle,
        enumerable: false,
        configurable: false,
        writable: false,
      });
    }

    get type() {
      return this[kKeyType];
    }

    static from(key) {
      if (!isCryptoKey(key))
        throw new ERR_INVALID_ARG_TYPE('key', 'CryptoKey', key);
      return key[kKeyObject];
    }

    equals(otherKeyObject) {
      if (!isKeyObject(otherKeyObject)) {
        throw new ERR_INVALID_ARG_TYPE(
          'otherKeyObject', 'KeyObject', otherKeyObject);
      }

      return otherKeyObject.type === this.type &&
        this[kHandle].equals(otherKeyObject[kHandle]);
    }
  }

  ObjectDefineProperties(KeyObject.prototype, {
    [SymbolToStringTag]: {
      __proto__: null,
      configurable: true,
      value: 'KeyObject',
    },
  });

  let webidl;

  class SecretKeyObject extends KeyObject {
    constructor(handle) {
      super('secret', handle);
    }

    get symmetricKeySize() {
      return this[kHandle].getSymmetricKeySize();
    }

    export(options) {
      if (options !== undefined) {
        validateObject(options, 'options');
        validateOneOf(
          options.format, 'options.format', [undefined, 'buffer', 'jwk']);
        if (options.format === 'jwk') {
          return this[kHandle].exportJwk({}, false);
        }
      }
      return this[kHandle].export();
    }

    toCryptoKey(algorithm, extractable, keyUsages) {
      webidl ??= require('internal/crypto/webidl');
      algorithm = normalizeAlgorithm(webidl.converters.AlgorithmIdentifier(algorithm), 'importKey');
      extractable = webidl.converters.boolean(extractable);
      keyUsages = webidl.converters['sequence<KeyUsage>'](keyUsages);

      let result;
      switch (algorithm.name) {
        case 'HMAC':
          result = require('internal/crypto/mac')
            .hmacImportKey('KeyObject', this, algorithm, extractable, keyUsages);
          break;
        case 'AES-CTR':
          // Fall through
        case 'AES-CBC':
          // Fall through
        case 'AES-GCM':
          // Fall through
        case 'AES-KW':
          result = require('internal/crypto/aes')
            .aesImportKey(algorithm, 'KeyObject', this, extractable, keyUsages);
          break;
        case 'HKDF':
          // Fall through
        case 'PBKDF2':
          result = importGenericSecretKey(
            algorithm,
            'KeyObject',
            this,
            extractable,
            keyUsages);
          break;
        default:
          throw lazyDOMException('Unrecognized algorithm name', 'NotSupportedError');
      }

      if (result.usages.length === 0) {
        throw lazyDOMException(
          `Usages cannot be empty when importing a ${result.type} key.`,
          'SyntaxError');
      }

      return result;
    }
  }

  const kAsymmetricKeyType = Symbol('kAsymmetricKeyType');
  const kAsymmetricKeyDetails = Symbol('kAsymmetricKeyDetails');

  function normalizeKeyDetails(details = {}) {
    if (details.publicExponent !== undefined) {
      return {
        ...details,
        publicExponent:
          bigIntArrayToUnsignedBigInt(new Uint8Array(details.publicExponent)),
      };
    }
    return details;
  }

  class AsymmetricKeyObject extends KeyObject {
    // eslint-disable-next-line no-useless-constructor
    constructor(type, handle) {
      super(type, handle);
    }

    get asymmetricKeyType() {
      return this[kAsymmetricKeyType] ||= this[kHandle].getAsymmetricKeyType();
    }

    get asymmetricKeyDetails() {
      switch (this.asymmetricKeyType) {
        case 'rsa':
        case 'rsa-pss':
        case 'dsa':
        case 'ec':
          return this[kAsymmetricKeyDetails] ||= normalizeKeyDetails(
            this[kHandle].keyDetail({}),
          );
        default:
          return {};
      }
    }

    toCryptoKey(algorithm, extractable, keyUsages) {
      webidl ??= require('internal/crypto/webidl');
      algorithm = normalizeAlgorithm(webidl.converters.AlgorithmIdentifier(algorithm), 'importKey');
      extractable = webidl.converters.boolean(extractable);
      keyUsages = webidl.converters['sequence<KeyUsage>'](keyUsages);

      let result;
      switch (algorithm.name) {
        case 'RSASSA-PKCS1-v1_5':
          // Fall through
        case 'RSA-PSS':
          // Fall through
        case 'RSA-OAEP':
          result = require('internal/crypto/rsa')
            .rsaImportKey('KeyObject', this, algorithm, extractable, keyUsages);
          break;
        case 'ECDSA':
          // Fall through
        case 'ECDH':
          result = require('internal/crypto/ec')
            .ecImportKey('KeyObject', this, algorithm, extractable, keyUsages);
          break;
        case 'Ed25519':
          // Fall through
        case 'Ed448':
          // Fall through
        case 'X25519':
          // Fall through
        case 'X448':
          result = require('internal/crypto/cfrg')
            .cfrgImportKey('KeyObject', this, algorithm, extractable, keyUsages);
          break;
        default:
          throw lazyDOMException('Unrecognized algorithm name', 'NotSupportedError');
      }

      if (result.type === 'private' && result.usages.length === 0) {
        throw lazyDOMException(
          `Usages cannot be empty when importing a ${result.type} key.`,
          'SyntaxError');
      }

      return result;
    }
  }

  class PublicKeyObject extends AsymmetricKeyObject {
    constructor(handle) {
      super('public', handle);
    }

    export(options) {
      if (options && options.format === 'jwk') {
        return this[kHandle].exportJwk({}, false);
      }
      const {
        format,
        type,
      } = parsePublicKeyEncoding(options, this.asymmetricKeyType);
      return this[kHandle].export(format, type);
    }
  }

  class PrivateKeyObject extends AsymmetricKeyObject {
    constructor(handle) {
      super('private', handle);
    }

    export(options) {
      if (options && options.format === 'jwk') {
        if (options.passphrase !== undefined) {
          throw new ERR_CRYPTO_INCOMPATIBLE_KEY_OPTIONS(
            'jwk', 'does not support encryption');
        }
        return this[kHandle].exportJwk({}, false);
      }
      const {
        format,
        type,
        cipher,
        passphrase,
      } = parsePrivateKeyEncoding(options, this.asymmetricKeyType);
      return this[kHandle].export(format, type, cipher, passphrase);
    }
  }

  return [KeyObject, SecretKeyObject, PublicKeyObject, PrivateKeyObject];
});

function parseKeyFormat(formatStr, defaultFormat, optionName) {
  if (formatStr === undefined && defaultFormat !== undefined)
    return defaultFormat;
  else if (formatStr === 'pem')
    return kKeyFormatPEM;
  else if (formatStr === 'der')
    return kKeyFormatDER;
  else if (formatStr === 'jwk')
    return kKeyFormatJWK;
  throw new ERR_INVALID_ARG_VALUE(optionName, formatStr);
}

function parseKeyType(typeStr, required, keyType, isPublic, optionName) {
  if (typeStr === undefined && !required) {
    return undefined;
  } else if (typeStr === 'pkcs1') {
    if (keyType !== undefined && keyType !== 'rsa') {
      throw new ERR_CRYPTO_INCOMPATIBLE_KEY_OPTIONS(
        typeStr, 'can only be used for RSA keys');
    }
    return kKeyEncodingPKCS1;
  } else if (typeStr === 'spki' && isPublic !== false) {
    return kKeyEncodingSPKI;
  } else if (typeStr === 'pkcs8' && isPublic !== true) {
    return kKeyEncodingPKCS8;
  } else if (typeStr === 'sec1' && isPublic !== true) {
    if (keyType !== undefined && keyType !== 'ec') {
      throw new ERR_CRYPTO_INCOMPATIBLE_KEY_OPTIONS(
        typeStr, 'can only be used for EC keys');
    }
    return kKeyEncodingSEC1;
  }

  throw new ERR_INVALID_ARG_VALUE(optionName, typeStr);
}

function option(name, objName) {
  return objName === undefined ?
    `options.${name}` : `options.${objName}.${name}`;
}

function parseKeyFormatAndType(enc, keyType, isPublic, objName) {
  const { format: formatStr, type: typeStr } = enc;

  const isInput = keyType === undefined;
  const format = parseKeyFormat(formatStr,
                                isInput ? kKeyFormatPEM : undefined,
                                option('format', objName));

  const isRequired = (!isInput ||
                      format === kKeyFormatDER) &&
                      format !== kKeyFormatJWK;
  const type = parseKeyType(typeStr,
                            isRequired,
                            keyType,
                            isPublic,
                            option('type', objName));
  return { format, type };
}

function isStringOrBuffer(val) {
  return typeof val === 'string' ||
         isArrayBufferView(val) ||
         isAnyArrayBuffer(val);
}

function parseKeyEncoding(enc, keyType, isPublic, objName) {
  validateObject(enc, 'options');

  const isInput = keyType === undefined;

  const {
    format,
    type,
  } = parseKeyFormatAndType(enc, keyType, isPublic, objName);

  let cipher, passphrase, encoding;
  if (isPublic !== true) {
    ({ cipher, passphrase, encoding } = enc);

    if (!isInput) {
      if (cipher != null) {
        if (typeof cipher !== 'string')
          throw new ERR_INVALID_ARG_VALUE(option('cipher', objName), cipher);
        if (format === kKeyFormatDER &&
            (type === kKeyEncodingPKCS1 ||
             type === kKeyEncodingSEC1)) {
          throw new ERR_CRYPTO_INCOMPATIBLE_KEY_OPTIONS(
            encodingNames[type], 'does not support encryption');
        }
      } else if (passphrase !== undefined) {
        throw new ERR_INVALID_ARG_VALUE(option('cipher', objName), cipher);
      }
    }

    if ((isInput && passphrase !== undefined &&
         !isStringOrBuffer(passphrase)) ||
        (!isInput && cipher != null && !isStringOrBuffer(passphrase))) {
      throw new ERR_INVALID_ARG_VALUE(option('passphrase', objName),
                                      passphrase);
    }
  }

  if (passphrase !== undefined)
    passphrase = getArrayBufferOrView(passphrase, 'key.passphrase', encoding);

  return { format, type, cipher, passphrase };
}

// Parses the public key encoding based on an object. keyType must be undefined
// when this is used to parse an input encoding and must be a valid key type if
// used to parse an output encoding.
function parsePublicKeyEncoding(enc, keyType, objName) {
  return parseKeyEncoding(enc, keyType, keyType ? true : undefined, objName);
}

// Parses the private key encoding based on an object. keyType must be undefined
// when this is used to parse an input encoding and must be a valid key type if
// used to parse an output encoding.
function parsePrivateKeyEncoding(enc, keyType, objName) {
  return parseKeyEncoding(enc, keyType, false, objName);
}

function getKeyObjectHandle(key, ctx) {
  if (ctx === kCreatePrivate) {
    throw new ERR_INVALID_ARG_TYPE(
      'key',
      ['string', 'ArrayBuffer', 'Buffer', 'TypedArray', 'DataView'],
      key,
    );
  }

  if (key.type !== 'private') {
    if (ctx === kConsumePrivate || ctx === kCreatePublic)
      throw new ERR_CRYPTO_INVALID_KEY_OBJECT_TYPE(key.type, 'private');
    if (key.type !== 'public') {
      throw new ERR_CRYPTO_INVALID_KEY_OBJECT_TYPE(key.type,
                                                   'private or public');
    }
  }

  return key[kHandle];
}

function getKeyTypes(allowKeyObject, bufferOnly = false) {
  const types = [
    'ArrayBuffer',
    'Buffer',
    'TypedArray',
    'DataView',
    'string', // Only if bufferOnly == false
    'KeyObject', // Only if allowKeyObject == true && bufferOnly == false
    'CryptoKey', // Only if allowKeyObject == true && bufferOnly == false
  ];
  if (bufferOnly) {
    return ArrayPrototypeSlice(types, 0, 4);
  } else if (!allowKeyObject) {
    return ArrayPrototypeSlice(types, 0, 5);
  }
  return types;
}

function getKeyObjectHandleFromJwk(key, ctx) {
  validateObject(key, 'key');
  validateOneOf(
    key.kty, 'key.kty', ['RSA', 'EC', 'OKP']);
  const isPublic = ctx === kConsumePublic || ctx === kCreatePublic;

  if (key.kty === 'OKP') {
    validateString(key.crv, 'key.crv');
    validateOneOf(
      key.crv, 'key.crv', ['Ed25519', 'Ed448', 'X25519', 'X448']);
    validateString(key.x, 'key.x');

    if (!isPublic)
      validateString(key.d, 'key.d');

    let keyData;
    if (isPublic)
      keyData = Buffer.from(key.x, 'base64');
    else
      keyData = Buffer.from(key.d, 'base64');

    switch (key.crv) {
      case 'Ed25519':
      case 'X25519':
        if (keyData.byteLength !== 32) {
          throw new ERR_CRYPTO_INVALID_JWK();
        }
        break;
      case 'Ed448':
        if (keyData.byteLength !== 57) {
          throw new ERR_CRYPTO_INVALID_JWK();
        }
        break;
      case 'X448':
        if (keyData.byteLength !== 56) {
          throw new ERR_CRYPTO_INVALID_JWK();
        }
        break;
    }

    const handle = new KeyObjectHandle();

    const keyType = isPublic ? kKeyTypePublic : kKeyTypePrivate;
    if (!handle.initEDRaw(key.crv, keyData, keyType)) {
      throw new ERR_CRYPTO_INVALID_JWK();
    }

    return handle;
  }

  if (key.kty === 'EC') {
    validateString(key.crv, 'key.crv');
    validateOneOf(
      key.crv, 'key.crv', ['P-256', 'secp256k1', 'P-384', 'P-521']);
    validateString(key.x, 'key.x');
    validateString(key.y, 'key.y');

    const jwk = {
      kty: key.kty,
      crv: key.crv,
      x: key.x,
      y: key.y,
    };

    if (!isPublic) {
      validateString(key.d, 'key.d');
      jwk.d = key.d;
    }

    const handle = new KeyObjectHandle();
    const type = handle.initJwk(jwk, jwk.crv);
    if (type === undefined)
      throw new ERR_CRYPTO_INVALID_JWK();

    return handle;
  }

  // RSA
  validateString(key.n, 'key.n');
  validateString(key.e, 'key.e');

  const jwk = {
    kty: key.kty,
    n: key.n,
    e: key.e,
  };

  if (!isPublic) {
    validateString(key.d, 'key.d');
    validateString(key.p, 'key.p');
    validateString(key.q, 'key.q');
    validateString(key.dp, 'key.dp');
    validateString(key.dq, 'key.dq');
    validateString(key.qi, 'key.qi');
    jwk.d = key.d;
    jwk.p = key.p;
    jwk.q = key.q;
    jwk.dp = key.dp;
    jwk.dq = key.dq;
    jwk.qi = key.qi;
  }

  const handle = new KeyObjectHandle();
  const type = handle.initJwk(jwk);
  if (type === undefined)
    throw new ERR_CRYPTO_INVALID_JWK();

  return handle;
}

function prepareAsymmetricKey(key, ctx) {
  if (isKeyObject(key)) {
    // Best case: A key object, as simple as that.
    return { data: getKeyObjectHandle(key, ctx) };
  } else if (isCryptoKey(key)) {
    return { data: getKeyObjectHandle(key[kKeyObject], ctx) };
  } else if (isStringOrBuffer(key)) {
    // Expect PEM by default, mostly for backward compatibility.
    return { format: kKeyFormatPEM, data: getArrayBufferOrView(key, 'key') };
  } else if (typeof key === 'object') {
    const { key: data, encoding, format } = key;

    // The 'key' property can be a KeyObject as well to allow specifying
    // additional options such as padding along with the key.
    if (isKeyObject(data))
      return { data: getKeyObjectHandle(data, ctx) };
    else if (isCryptoKey(data))
      return { data: getKeyObjectHandle(data[kKeyObject], ctx) };
    else if (format === 'jwk') {
      validateObject(data, 'key.key');
      return { data: getKeyObjectHandleFromJwk(data, ctx), format: 'jwk' };
    }

    // Either PEM or DER using PKCS#1 or SPKI.
    if (!isStringOrBuffer(data)) {
      throw new ERR_INVALID_ARG_TYPE(
        'key.key',
        getKeyTypes(ctx !== kCreatePrivate),
        data);
    }

    const isPublic =
      (ctx === kConsumePrivate || ctx === kCreatePrivate) ? false : undefined;
    return {
      data: getArrayBufferOrView(data, 'key', encoding),
      ...parseKeyEncoding(key, undefined, isPublic),
    };
  }
  throw new ERR_INVALID_ARG_TYPE(
    'key',
    getKeyTypes(ctx !== kCreatePrivate),
    key);
}

function preparePrivateKey(key) {
  return prepareAsymmetricKey(key, kConsumePrivate);
}

function preparePublicOrPrivateKey(key) {
  return prepareAsymmetricKey(key, kConsumePublic);
}

function prepareSecretKey(key, encoding, bufferOnly = false) {
  if (!bufferOnly) {
    if (isKeyObject(key)) {
      if (key.type !== 'secret')
        throw new ERR_CRYPTO_INVALID_KEY_OBJECT_TYPE(key.type, 'secret');
      return key[kHandle];
    } else if (isCryptoKey(key)) {
      if (key.type !== 'secret')
        throw new ERR_CRYPTO_INVALID_KEY_OBJECT_TYPE(key.type, 'secret');
      return key[kKeyObject][kHandle];
    }
  }
  if (typeof key !== 'string' &&
      !isArrayBufferView(key) &&
      !isAnyArrayBuffer(key)) {
    throw new ERR_INVALID_ARG_TYPE(
      'key',
      getKeyTypes(!bufferOnly, bufferOnly),
      key);
  }
  return getArrayBufferOrView(key, 'key', encoding);
}

function createSecretKey(key, encoding) {
  key = prepareSecretKey(key, encoding, true);
  const handle = new KeyObjectHandle();
  handle.init(kKeyTypeSecret, key);
  return new SecretKeyObject(handle);
}

function createPublicKey(key) {
  const { format, type, data, passphrase } =
    prepareAsymmetricKey(key, kCreatePublic);
  let handle;
  if (format === 'jwk') {
    handle = data;
  } else {
    handle = new KeyObjectHandle();
    handle.init(kKeyTypePublic, data, format, type, passphrase);
  }
  return new PublicKeyObject(handle);
}

function createPrivateKey(key) {
  const { format, type, data, passphrase } =
    prepareAsymmetricKey(key, kCreatePrivate);
  let handle;
  if (format === 'jwk') {
    handle = data;
  } else {
    handle = new KeyObjectHandle();
    handle.init(kKeyTypePrivate, data, format, type, passphrase);
  }
  return new PrivateKeyObject(handle);
}

function isKeyObject(obj) {
  return obj != null && obj[kKeyType] !== undefined;
}

// Our implementation of CryptoKey is a simple wrapper around a KeyObject
// that adapts it to the standard interface.
// TODO(@jasnell): Embedder environments like electron may have issues
// here similar to other things like URL. A chromium provided CryptoKey
// will not be recognized as a Node.js CryptoKey, and vice versa. It
// would be fantastic if we could find a way of making those interop.
class CryptoKey {
  constructor() {
    throw new ERR_ILLEGAL_CONSTRUCTOR();
  }

  [kInspect](depth, options) {
    if (depth < 0)
      return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `CryptoKey ${inspect({
      type: this.type,
      extractable: this.extractable,
      algorithm: this.algorithm,
      usages: this.usages,
    }, opts)}`;
  }

  get type() {
    if (!(this instanceof CryptoKey))
      throw new ERR_INVALID_THIS('CryptoKey');
    return this[kKeyObject].type;
  }

  get extractable() {
    if (!(this instanceof CryptoKey))
      throw new ERR_INVALID_THIS('CryptoKey');
    return this[kExtractable];
  }

  get algorithm() {
    if (!(this instanceof CryptoKey))
      throw new ERR_INVALID_THIS('CryptoKey');
    return this[kAlgorithm];
  }

  get usages() {
    if (!(this instanceof CryptoKey))
      throw new ERR_INVALID_THIS('CryptoKey');
    return this[kKeyUsages];
  }
}

ObjectDefineProperties(CryptoKey.prototype, {
  type: kEnumerableProperty,
  extractable: kEnumerableProperty,
  algorithm: kEnumerableProperty,
  usages: kEnumerableProperty,
  [SymbolToStringTag]: {
    __proto__: null,
    configurable: true,
    value: 'CryptoKey',
  },
});

/**
 * @param {InternalCryptoKey} key
 * @param {KeyObject} keyObject
 * @param {object} algorithm
 * @param {boolean} extractable
 * @param {Set<string>} keyUsages
 */
function defineCryptoKeyProperties(
  key,
  keyObject,
  algorithm,
  extractable,
  keyUsages,
) {
  // Using symbol properties here currently instead of private
  // properties because (for now) the performance penalty of
  // private fields is still too high.
  ObjectDefineProperties(key, {
    [kKeyObject]: {
      __proto__: null,
      value: keyObject,
      enumerable: false,
      configurable: false,
      writable: false,
    },
    [kAlgorithm]: {
      __proto__: null,
      value: algorithm,
      enumerable: false,
      configurable: false,
      writable: false,
    },
    [kExtractable]: {
      __proto__: null,
      value: extractable,
      enumerable: false,
      configurable: false,
      writable: false,
    },
    [kKeyUsages]: {
      __proto__: null,
      value: keyUsages,
      enumerable: false,
      configurable: false,
      writable: false,
    },
  });
}

// All internal code must use new InternalCryptoKey to create
// CryptoKey instances. The CryptoKey class is exposed to end
// user code but is not permitted to be constructed directly.
// Using markTransferMode also allows the CryptoKey to be
// cloned to Workers.
class InternalCryptoKey {
  constructor(keyObject, algorithm, keyUsages, extractable) {
    markTransferMode(this, true, false);
    // When constructed during transfer the properties get assigned
    // in the kDeserialize call.
    if (keyObject) {
      defineCryptoKeyProperties(
        this,
        keyObject,
        algorithm,
        extractable,
        keyUsages,
      );
    }
  }

  [kClone]() {
    const keyObject = this[kKeyObject];
    const algorithm = this[kAlgorithm];
    const extractable = this[kExtractable];
    const usages = this[kKeyUsages];

    return {
      data: {
        keyObject,
        algorithm,
        usages,
        extractable,
      },
      deserializeInfo: 'internal/crypto/keys:InternalCryptoKey',
    };
  }

  [kDeserialize]({ keyObject, algorithm, usages, extractable }) {
    defineCryptoKeyProperties(this, keyObject, algorithm, extractable, usages);
  }
}
InternalCryptoKey.prototype.constructor = CryptoKey;
ObjectSetPrototypeOf(InternalCryptoKey.prototype, CryptoKey.prototype);

function isCryptoKey(obj) {
  return obj != null && obj[kKeyObject] !== undefined;
}

function importGenericSecretKey(
  algorithm,
  format,
  keyData,
  extractable,
  keyUsages,
) {
  const usagesSet = new SafeSet(keyUsages);
  const { name } = algorithm;
  if (extractable)
    throw lazyDOMException(`${name} keys are not extractable`, 'SyntaxError');

  if (hasAnyNotIn(usagesSet, ['deriveKey', 'deriveBits'])) {
    throw lazyDOMException(
      `Unsupported key usage for a ${name} key`,
      'SyntaxError');
  }

  let keyObject;
  switch (format) {
    case 'KeyObject': {
      keyObject = keyData;
      break;
    }
    case 'raw': {
      keyObject = createSecretKey(keyData);
      break;
    }
  }

  if (keyObject) {
    return new InternalCryptoKey(keyObject, { name }, keyUsages, false);
  }

  throw lazyDOMException(
    `Unable to import ${name} key with format ${format}`,
    'NotSupportedError');
}

module.exports = {
  // Public API.
  createSecretKey,
  createPublicKey,
  createPrivateKey,
  KeyObject,
  CryptoKey,
  InternalCryptoKey,

  // These are designed for internal use only and should not be exposed.
  parsePublicKeyEncoding,
  parsePrivateKeyEncoding,
  parseKeyEncoding,
  preparePrivateKey,
  preparePublicOrPrivateKey,
  prepareSecretKey,
  SecretKeyObject,
  PublicKeyObject,
  PrivateKeyObject,
  isKeyObject,
  isCryptoKey,
  importGenericSecretKey,
};
