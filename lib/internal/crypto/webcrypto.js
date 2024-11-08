'use strict';

const {
  ArrayPrototypeIncludes,
  JSONParse,
  JSONStringify,
  ObjectDefineProperties,
  ReflectApply,
  ReflectConstruct,
  StringPrototypeRepeat,
  SymbolToStringTag,
} = primordials;

const {
  kWebCryptoKeyFormatRaw,
  kWebCryptoKeyFormatPKCS8,
  kWebCryptoKeyFormatSPKI,
  kWebCryptoCipherEncrypt,
  kWebCryptoCipherDecrypt,
} = internalBinding('crypto');

const { TextDecoder, TextEncoder } = require('internal/encoding');

const {
  codes: {
    ERR_ILLEGAL_CONSTRUCTOR,
    ERR_INVALID_THIS,
  },
} = require('internal/errors');

const {
  CryptoKey,
  importGenericSecretKey,
} = require('internal/crypto/keys');

const {
  asyncDigest,
} = require('internal/crypto/hash');

const {
  getBlockSize,
  normalizeAlgorithm,
  normalizeHashName,
  validateMaxBufferLength,
  kHandle,
  kKeyObject,
} = require('internal/crypto/util');

const {
  kEnumerableProperty,
  lazyDOMException,
} = require('internal/util');

const {
  getRandomValues: _getRandomValues,
  randomUUID: _randomUUID,
} = require('internal/crypto/random');

let webidl;

async function digest(algorithm, data) {
  if (this !== subtle) throw new ERR_INVALID_THIS('SubtleCrypto');

  webidl ??= require('internal/crypto/webidl');
  const prefix = "Failed to execute 'digest' on 'SubtleCrypto'";
  webidl.requiredArguments(arguments.length, 2, { prefix });
  algorithm = webidl.converters.AlgorithmIdentifier(algorithm, {
    prefix,
    context: '1st argument',
  });
  data = webidl.converters.BufferSource(data, {
    prefix,
    context: '2nd argument',
  });

  algorithm = normalizeAlgorithm(algorithm, 'digest');

  return ReflectApply(asyncDigest, this, [algorithm, data]);
}

function randomUUID() {
  if (this !== crypto) throw new ERR_INVALID_THIS('Crypto');
  return _randomUUID();
}

async function generateKey(
  algorithm,
  extractable,
  keyUsages) {
  if (this !== subtle) throw new ERR_INVALID_THIS('SubtleCrypto');

  webidl ??= require('internal/crypto/webidl');
  const prefix = "Failed to execute 'generateKey' on 'SubtleCrypto'";
  webidl.requiredArguments(arguments.length, 3, { prefix });
  algorithm = webidl.converters.AlgorithmIdentifier(algorithm, {
    prefix,
    context: '1st argument',
  });
  extractable = webidl.converters.boolean(extractable, {
    prefix,
    context: '2nd argument',
  });
  keyUsages = webidl.converters['sequence<KeyUsage>'](keyUsages, {
    prefix,
    context: '3rd argument',
  });

  algorithm = normalizeAlgorithm(algorithm, 'generateKey');
  let result;
  let resultType;
  switch (algorithm.name) {
    case 'RSASSA-PKCS1-v1_5':
      // Fall through
    case 'RSA-PSS':
      // Fall through
    case 'RSA-OAEP':
      resultType = 'CryptoKeyPair';
      result = await require('internal/crypto/rsa')
        .rsaKeyGenerate(algorithm, extractable, keyUsages);
      break;
    case 'Ed25519':
      // Fall through
    case 'Ed448':
      // Fall through
    case 'X25519':
      // Fall through
    case 'X448':
      resultType = 'CryptoKeyPair';
      result = await require('internal/crypto/cfrg')
        .cfrgGenerateKey(algorithm, extractable, keyUsages);
      break;
    case 'ECDSA':
      // Fall through
    case 'ECDH':
      resultType = 'CryptoKeyPair';
      result = await require('internal/crypto/ec')
        .ecGenerateKey(algorithm, extractable, keyUsages);
      break;
    case 'HMAC':
      resultType = 'CryptoKey';
      result = await require('internal/crypto/mac')
        .hmacGenerateKey(algorithm, extractable, keyUsages);
      break;
    case 'AES-CTR':
      // Fall through
    case 'AES-CBC':
      // Fall through
    case 'AES-GCM':
      // Fall through
    case 'AES-KW':
      resultType = 'CryptoKey';
      result = await require('internal/crypto/aes')
        .aesGenerateKey(algorithm, extractable, keyUsages);
      break;
    default:
      throw lazyDOMException('Unrecognized algorithm name', 'NotSupportedError');
  }

  if (
    (resultType === 'CryptoKey' &&
      (result.type === 'secret' || result.type === 'private') &&
      result.usages.length === 0) ||
    (resultType === 'CryptoKeyPair' && result.privateKey.usages.length === 0)
  ) {
    throw lazyDOMException(
      'Usages cannot be empty when creating a key.',
      'SyntaxError');
  }

  return result;
}

async function deriveBits(algorithm, baseKey, length = null) {
  if (this !== subtle) throw new ERR_INVALID_THIS('SubtleCrypto');

  webidl ??= require('internal/crypto/webidl');
  const prefix = "Failed to execute 'deriveBits' on 'SubtleCrypto'";
  webidl.requiredArguments(arguments.length, 2, { prefix });
  algorithm = webidl.converters.AlgorithmIdentifier(algorithm, {
    prefix,
    context: '1st argument',
  });
  baseKey = webidl.converters.CryptoKey(baseKey, {
    prefix,
    context: '2nd argument',
  });
  if (length !== null) {
    length = webidl.converters['unsigned long'](length, {
      prefix,
      context: '3rd argument',
    });
  }

  algorithm = normalizeAlgorithm(algorithm, 'deriveBits');
  if (!ArrayPrototypeIncludes(baseKey.usages, 'deriveBits')) {
    throw lazyDOMException(
      'baseKey does not have deriveBits usage',
      'InvalidAccessError');
  }
  if (baseKey.algorithm.name !== algorithm.name)
    throw lazyDOMException('Key algorithm mismatch', 'InvalidAccessError');
  switch (algorithm.name) {
    case 'X25519':
      // Fall through
    case 'X448':
      // Fall through
    case 'ECDH':
      return require('internal/crypto/diffiehellman')
        .ecdhDeriveBits(algorithm, baseKey, length);
    case 'HKDF':
      return require('internal/crypto/hkdf')
        .hkdfDeriveBits(algorithm, baseKey, length);
    case 'PBKDF2':
      return require('internal/crypto/pbkdf2')
        .pbkdf2DeriveBits(algorithm, baseKey, length);
  }
  throw lazyDOMException('Unrecognized algorithm name', 'NotSupportedError');
}

function getKeyLength({ name, length, hash }) {
  switch (name) {
    case 'AES-CTR':
    case 'AES-CBC':
    case 'AES-GCM':
    case 'AES-KW':
      if (length !== 128 && length !== 192 && length !== 256)
        throw lazyDOMException('Invalid key length', 'OperationError');

      return length;
    case 'HMAC':
      if (length === undefined) {
        return getBlockSize(hash?.name);
      }

      if (typeof length === 'number' && length !== 0) {
        return length;
      }

      throw lazyDOMException('Invalid key length', 'OperationError');
    case 'HKDF':
    case 'PBKDF2':
      return null;
  }
}

async function deriveKey(
  algorithm,
  baseKey,
  derivedKeyAlgorithm,
  extractable,
  keyUsages) {
  if (this !== subtle) throw new ERR_INVALID_THIS('SubtleCrypto');

  webidl ??= require('internal/crypto/webidl');
  const prefix = "Failed to execute 'deriveKey' on 'SubtleCrypto'";
  webidl.requiredArguments(arguments.length, 5, { prefix });
  algorithm = webidl.converters.AlgorithmIdentifier(algorithm, {
    prefix,
    context: '1st argument',
  });
  baseKey = webidl.converters.CryptoKey(baseKey, {
    prefix,
    context: '2nd argument',
  });
  derivedKeyAlgorithm = webidl.converters.AlgorithmIdentifier(derivedKeyAlgorithm, {
    prefix,
    context: '3rd argument',
  });
  extractable = webidl.converters.boolean(extractable, {
    prefix,
    context: '4th argument',
  });
  keyUsages = webidl.converters['sequence<KeyUsage>'](keyUsages, {
    prefix,
    context: '5th argument',
  });

  algorithm = normalizeAlgorithm(algorithm, 'deriveBits');
  derivedKeyAlgorithm = normalizeAlgorithm(derivedKeyAlgorithm, 'importKey');
  if (!ArrayPrototypeIncludes(baseKey.usages, 'deriveKey')) {
    throw lazyDOMException(
      'baseKey does not have deriveKey usage',
      'InvalidAccessError');
  }
  if (baseKey.algorithm.name !== algorithm.name)
    throw lazyDOMException('Key algorithm mismatch', 'InvalidAccessError');

  const length = getKeyLength(normalizeAlgorithm(arguments[2], 'get key length'));
  let bits;
  switch (algorithm.name) {
    case 'X25519':
      // Fall through
    case 'X448':
      // Fall through
    case 'ECDH':
      bits = await require('internal/crypto/diffiehellman')
        .ecdhDeriveBits(algorithm, baseKey, length);
      break;
    case 'HKDF':
      bits = await require('internal/crypto/hkdf')
        .hkdfDeriveBits(algorithm, baseKey, length);
      break;
    case 'PBKDF2':
      bits = await require('internal/crypto/pbkdf2')
        .pbkdf2DeriveBits(algorithm, baseKey, length);
      break;
    default:
      throw lazyDOMException('Unrecognized algorithm name', 'NotSupportedError');
  }

  return ReflectApply(
    importKey,
    this,
    ['raw', bits, derivedKeyAlgorithm, extractable, keyUsages],
  );
}

async function exportKeySpki(key) {
  switch (key.algorithm.name) {
    case 'RSASSA-PKCS1-v1_5':
      // Fall through
    case 'RSA-PSS':
      // Fall through
    case 'RSA-OAEP':
      if (key.type === 'public') {
        return require('internal/crypto/rsa')
          .rsaExportKey(key, kWebCryptoKeyFormatSPKI);
      }
      break;
    case 'ECDSA':
      // Fall through
    case 'ECDH':
      if (key.type === 'public') {
        return require('internal/crypto/ec')
          .ecExportKey(key, kWebCryptoKeyFormatSPKI);
      }
      break;
    case 'Ed25519':
      // Fall through
    case 'Ed448':
      // Fall through
    case 'X25519':
      // Fall through
    case 'X448':
      if (key.type === 'public') {
        return require('internal/crypto/cfrg')
          .cfrgExportKey(key, kWebCryptoKeyFormatSPKI);
      }
      break;
  }

  throw lazyDOMException(
    `Unable to export a raw ${key.algorithm.name} ${key.type} key`,
    'InvalidAccessError');
}

async function exportKeyPkcs8(key) {
  switch (key.algorithm.name) {
    case 'RSASSA-PKCS1-v1_5':
      // Fall through
    case 'RSA-PSS':
      // Fall through
    case 'RSA-OAEP':
      if (key.type === 'private') {
        return require('internal/crypto/rsa')
          .rsaExportKey(key, kWebCryptoKeyFormatPKCS8);
      }
      break;
    case 'ECDSA':
      // Fall through
    case 'ECDH':
      if (key.type === 'private') {
        return require('internal/crypto/ec')
          .ecExportKey(key, kWebCryptoKeyFormatPKCS8);
      }
      break;
    case 'Ed25519':
      // Fall through
    case 'Ed448':
      // Fall through
    case 'X25519':
      // Fall through
    case 'X448':
      if (key.type === 'private') {
        return require('internal/crypto/cfrg')
          .cfrgExportKey(key, kWebCryptoKeyFormatPKCS8);
      }
      break;
  }

  throw lazyDOMException(
    `Unable to export a pkcs8 ${key.algorithm.name} ${key.type} key`,
    'InvalidAccessError');
}

async function exportKeyRaw(key) {
  switch (key.algorithm.name) {
    case 'ECDSA':
      // Fall through
    case 'ECDH':
      if (key.type === 'public') {
        return require('internal/crypto/ec')
          .ecExportKey(key, kWebCryptoKeyFormatRaw);
      }
      break;
    case 'Ed25519':
      // Fall through
    case 'Ed448':
      // Fall through
    case 'X25519':
      // Fall through
    case 'X448':
      if (key.type === 'public') {
        return require('internal/crypto/cfrg')
          .cfrgExportKey(key, kWebCryptoKeyFormatRaw);
      }
      break;
    case 'AES-CTR':
      // Fall through
    case 'AES-CBC':
      // Fall through
    case 'AES-GCM':
      // Fall through
    case 'AES-KW':
      // Fall through
    case 'HMAC':
      return key[kKeyObject].export().buffer;
  }

  throw lazyDOMException(
    `Unable to export a raw ${key.algorithm.name} ${key.type} key`,
    'InvalidAccessError');
}

async function exportKeyJWK(key) {
  const jwk = key[kKeyObject][kHandle].exportJwk({
    key_ops: key.usages,
    ext: key.extractable,
  }, true);
  switch (key.algorithm.name) {
    case 'RSASSA-PKCS1-v1_5':
      jwk.alg = normalizeHashName(
        key.algorithm.hash.name,
        normalizeHashName.kContextJwkRsa);
      return jwk;
    case 'RSA-PSS':
      jwk.alg = normalizeHashName(
        key.algorithm.hash.name,
        normalizeHashName.kContextJwkRsaPss);
      return jwk;
    case 'RSA-OAEP':
      jwk.alg = normalizeHashName(
        key.algorithm.hash.name,
        normalizeHashName.kContextJwkRsaOaep);
      return jwk;
    case 'ECDSA':
      // Fall through
    case 'ECDH':
      jwk.crv ||= key.algorithm.namedCurve;
      return jwk;
    case 'X25519':
      // Fall through
    case 'X448':
      jwk.crv ||= key.algorithm.name;
      return jwk;
    case 'Ed25519':
      // Fall through
    case 'Ed448':
      jwk.crv ||= key.algorithm.name;
      return jwk;
    case 'AES-CTR':
      // Fall through
    case 'AES-CBC':
      // Fall through
    case 'AES-GCM':
      // Fall through
    case 'AES-KW':
      jwk.alg = require('internal/crypto/aes')
        .getAlgorithmName(key.algorithm.name, key.algorithm.length);
      return jwk;
    case 'HMAC':
      jwk.alg = normalizeHashName(
        key.algorithm.hash.name,
        normalizeHashName.kContextJwkHmac);
      return jwk;
    default:
      // Fall through
  }

  throw lazyDOMException('Not yet supported', 'NotSupportedError');
}

async function exportKey(format, key) {
  if (this !== subtle) throw new ERR_INVALID_THIS('SubtleCrypto');

  webidl ??= require('internal/crypto/webidl');
  const prefix = "Failed to execute 'exportKey' on 'SubtleCrypto'";
  webidl.requiredArguments(arguments.length, 2, { prefix });
  format = webidl.converters.KeyFormat(format, {
    prefix,
    context: '1st argument',
  });
  key = webidl.converters.CryptoKey(key, {
    prefix,
    context: '2nd argument',
  });

  if (!key.extractable)
    throw lazyDOMException('key is not extractable', 'InvalidAccessException');

  switch (format) {
    case 'spki': return exportKeySpki(key);
    case 'pkcs8': return exportKeyPkcs8(key);
    case 'jwk': return exportKeyJWK(key);
    case 'raw': return exportKeyRaw(key);
  }
  throw lazyDOMException(
    'Export format is unsupported', 'NotSupportedError');
}

async function importKey(
  format,
  keyData,
  algorithm,
  extractable,
  keyUsages) {
  if (this !== subtle) throw new ERR_INVALID_THIS('SubtleCrypto');

  webidl ??= require('internal/crypto/webidl');
  const prefix = "Failed to execute 'importKey' on 'SubtleCrypto'";
  webidl.requiredArguments(arguments.length, 4, { prefix });
  format = webidl.converters.KeyFormat(format, {
    prefix,
    context: '1st argument',
  });
  const type = format === 'jwk' ? 'JsonWebKey' : 'BufferSource';
  keyData = webidl.converters[type](keyData, {
    prefix,
    context: '2nd argument',
  });
  algorithm = webidl.converters.AlgorithmIdentifier(algorithm, {
    prefix,
    context: '3rd argument',
  });
  extractable = webidl.converters.boolean(extractable, {
    prefix,
    context: '4th argument',
  });
  keyUsages = webidl.converters['sequence<KeyUsage>'](keyUsages, {
    prefix,
    context: '5th argument',
  });

  algorithm = normalizeAlgorithm(algorithm, 'importKey');
  let result;
  switch (algorithm.name) {
    case 'RSASSA-PKCS1-v1_5':
      // Fall through
    case 'RSA-PSS':
      // Fall through
    case 'RSA-OAEP':
      result = require('internal/crypto/rsa')
        .rsaImportKey(format, keyData, algorithm, extractable, keyUsages);
      break;
    case 'ECDSA':
      // Fall through
    case 'ECDH':
      result = require('internal/crypto/ec')
        .ecImportKey(format, keyData, algorithm, extractable, keyUsages);
      break;
    case 'Ed25519':
      // Fall through
    case 'Ed448':
      // Fall through
    case 'X25519':
      // Fall through
    case 'X448':
      result = require('internal/crypto/cfrg')
        .cfrgImportKey(format, keyData, algorithm, extractable, keyUsages);
      break;
    case 'HMAC':
      result = require('internal/crypto/mac')
        .hmacImportKey(format, keyData, algorithm, extractable, keyUsages);
      break;
    case 'AES-CTR':
      // Fall through
    case 'AES-CBC':
      // Fall through
    case 'AES-GCM':
      // Fall through
    case 'AES-KW':
      result = require('internal/crypto/aes')
        .aesImportKey(algorithm, format, keyData, extractable, keyUsages);
      break;
    case 'HKDF':
      // Fall through
    case 'PBKDF2':
      result = importGenericSecretKey(
        algorithm,
        format,
        keyData,
        extractable,
        keyUsages);
      break;
    default:
      throw lazyDOMException('Unrecognized algorithm name', 'NotSupportedError');
  }

  if ((result.type === 'secret' || result.type === 'private') && result.usages.length === 0) {
    throw lazyDOMException(
      `Usages cannot be empty when importing a ${result.type} key.`,
      'SyntaxError');
  }

  return result;
}

// subtle.wrapKey() is essentially a subtle.exportKey() followed
// by a subtle.encrypt().
async function wrapKey(format, key, wrappingKey, algorithm) {
  if (this !== subtle) throw new ERR_INVALID_THIS('SubtleCrypto');

  webidl ??= require('internal/crypto/webidl');
  const prefix = "Failed to execute 'wrapKey' on 'SubtleCrypto'";
  webidl.requiredArguments(arguments.length, 4, { prefix });
  format = webidl.converters.KeyFormat(format, {
    prefix,
    context: '1st argument',
  });
  key = webidl.converters.CryptoKey(key, {
    prefix,
    context: '2nd argument',
  });
  wrappingKey = webidl.converters.CryptoKey(wrappingKey, {
    prefix,
    context: '3rd argument',
  });
  algorithm = webidl.converters.AlgorithmIdentifier(algorithm, {
    prefix,
    context: '4th argument',
  });

  try {
    algorithm = normalizeAlgorithm(algorithm, 'wrapKey');
  } catch {
    algorithm = normalizeAlgorithm(algorithm, 'encrypt');
  }
  let keyData = await ReflectApply(exportKey, this, [format, key]);

  if (format === 'jwk') {
    const ec = new TextEncoder();
    const raw = JSONStringify(keyData);
    // As per the NOTE in step 13 https://w3c.github.io/webcrypto/#SubtleCrypto-method-wrapKey
    // we're padding AES-KW wrapped JWK to make sure it is always a multiple of 8 bytes
    // in length
    if (algorithm.name === 'AES-KW' && raw.length % 8 !== 0) {
      keyData = ec.encode(raw + StringPrototypeRepeat(' ', 8 - (raw.length % 8)));
    } else {
      keyData = ec.encode(raw);
    }
  }

  return cipherOrWrap(
    kWebCryptoCipherEncrypt,
    algorithm,
    wrappingKey,
    keyData,
    'wrapKey');
}

// subtle.unwrapKey() is essentially a subtle.decrypt() followed
// by a subtle.importKey().
async function unwrapKey(
  format,
  wrappedKey,
  unwrappingKey,
  unwrapAlgo,
  unwrappedKeyAlgo,
  extractable,
  keyUsages) {
  if (this !== subtle) throw new ERR_INVALID_THIS('SubtleCrypto');

  webidl ??= require('internal/crypto/webidl');
  const prefix = "Failed to execute 'unwrapKey' on 'SubtleCrypto'";
  webidl.requiredArguments(arguments.length, 7, { prefix });
  format = webidl.converters.KeyFormat(format, {
    prefix,
    context: '1st argument',
  });
  wrappedKey = webidl.converters.BufferSource(wrappedKey, {
    prefix,
    context: '2nd argument',
  });
  unwrappingKey = webidl.converters.CryptoKey(unwrappingKey, {
    prefix,
    context: '3rd argument',
  });
  unwrapAlgo = webidl.converters.AlgorithmIdentifier(unwrapAlgo, {
    prefix,
    context: '4th argument',
  });
  unwrappedKeyAlgo = webidl.converters.AlgorithmIdentifier(
    unwrappedKeyAlgo,
    {
      prefix,
      context: '5th argument',
    },
  );
  extractable = webidl.converters.boolean(extractable, {
    prefix,
    context: '6th argument',
  });
  keyUsages = webidl.converters['sequence<KeyUsage>'](keyUsages, {
    prefix,
    context: '7th argument',
  });

  try {
    unwrapAlgo = normalizeAlgorithm(unwrapAlgo, 'unwrapKey');
  } catch {
    unwrapAlgo = normalizeAlgorithm(unwrapAlgo, 'decrypt');
  }

  let keyData = await cipherOrWrap(
    kWebCryptoCipherDecrypt,
    unwrapAlgo,
    unwrappingKey,
    wrappedKey,
    'unwrapKey');

  if (format === 'jwk') {
    // The fatal: true option is only supported in builds that have ICU.
    const options = process.versions.icu !== undefined ?
      { fatal: true } : undefined;
    const dec = new TextDecoder('utf-8', options);
    try {
      keyData = JSONParse(dec.decode(keyData));
    } catch {
      throw lazyDOMException('Invalid wrapped JWK key', 'DataError');
    }
  }

  return ReflectApply(
    importKey,
    this,
    [format, keyData, unwrappedKeyAlgo, extractable, keyUsages],
  );
}

function signVerify(algorithm, key, data, signature) {
  let usage = 'sign';
  if (signature !== undefined) {
    usage = 'verify';
  }
  algorithm = normalizeAlgorithm(algorithm, usage);

  if (!ArrayPrototypeIncludes(key.usages, usage) ||
      algorithm.name !== key.algorithm.name) {
    throw lazyDOMException(
      `Unable to use this key to ${usage}`,
      'InvalidAccessError');
  }

  switch (algorithm.name) {
    case 'RSA-PSS':
      // Fall through
    case 'RSASSA-PKCS1-v1_5':
      return require('internal/crypto/rsa')
        .rsaSignVerify(key, data, algorithm, signature);
    case 'ECDSA':
      return require('internal/crypto/ec')
        .ecdsaSignVerify(key, data, algorithm, signature);
    case 'Ed25519':
      // Fall through
    case 'Ed448':
      // Fall through
      return require('internal/crypto/cfrg')
        .eddsaSignVerify(key, data, algorithm, signature);
    case 'HMAC':
      return require('internal/crypto/mac')
        .hmacSignVerify(key, data, algorithm, signature);
  }
  throw lazyDOMException('Unrecognized algorithm name', 'NotSupportedError');
}

async function sign(algorithm, key, data) {
  if (this !== subtle) throw new ERR_INVALID_THIS('SubtleCrypto');

  webidl ??= require('internal/crypto/webidl');
  const prefix = "Failed to execute 'sign' on 'SubtleCrypto'";
  webidl.requiredArguments(arguments.length, 3, { prefix });
  algorithm = webidl.converters.AlgorithmIdentifier(algorithm, {
    prefix,
    context: '1st argument',
  });
  key = webidl.converters.CryptoKey(key, {
    prefix,
    context: '2nd argument',
  });
  data = webidl.converters.BufferSource(data, {
    prefix,
    context: '3rd argument',
  });

  return signVerify(algorithm, key, data);
}

async function verify(algorithm, key, signature, data) {
  if (this !== subtle) throw new ERR_INVALID_THIS('SubtleCrypto');

  webidl ??= require('internal/crypto/webidl');
  const prefix = "Failed to execute 'verify' on 'SubtleCrypto'";
  webidl.requiredArguments(arguments.length, 4, { prefix });
  algorithm = webidl.converters.AlgorithmIdentifier(algorithm, {
    prefix,
    context: '1st argument',
  });
  key = webidl.converters.CryptoKey(key, {
    prefix,
    context: '2nd argument',
  });
  signature = webidl.converters.BufferSource(signature, {
    prefix,
    context: '3rd argument',
  });
  data = webidl.converters.BufferSource(data, {
    prefix,
    context: '4th argument',
  });

  return signVerify(algorithm, key, data, signature);
}

async function cipherOrWrap(mode, algorithm, key, data, op) {
  // We use a Node.js style error here instead of a DOMException because
  // the WebCrypto spec is not specific what kind of error is to be thrown
  // in this case. Both Firefox and Chrome throw simple TypeErrors here.
  // The key algorithm and cipher algorithm must match, and the
  // key must have the proper usage.
  if (key.algorithm.name !== algorithm.name ||
      !ArrayPrototypeIncludes(key.usages, op)) {
    throw lazyDOMException(
      'The requested operation is not valid for the provided key',
      'InvalidAccessError');
  }

  // While WebCrypto allows for larger input buffer sizes, we limit
  // those to sizes that can fit within uint32_t because of limitations
  // in the OpenSSL API.
  validateMaxBufferLength(data, 'data');

  switch (algorithm.name) {
    case 'RSA-OAEP':
      return require('internal/crypto/rsa')
        .rsaCipher(mode, key, data, algorithm);
    case 'AES-CTR':
      // Fall through
    case 'AES-CBC':
      // Fall through
    case 'AES-GCM':
      return require('internal/crypto/aes')
        .aesCipher(mode, key, data, algorithm);
    case 'AES-KW':
      if (op === 'wrapKey' || op === 'unwrapKey') {
        return require('internal/crypto/aes')
          .aesCipher(mode, key, data, algorithm);
      }
  }
  throw lazyDOMException('Unrecognized algorithm name', 'NotSupportedError');
}

async function encrypt(algorithm, key, data) {
  if (this !== subtle) throw new ERR_INVALID_THIS('SubtleCrypto');

  webidl ??= require('internal/crypto/webidl');
  const prefix = "Failed to execute 'encrypt' on 'SubtleCrypto'";
  webidl.requiredArguments(arguments.length, 3, { prefix });
  algorithm = webidl.converters.AlgorithmIdentifier(algorithm, {
    prefix,
    context: '1st argument',
  });
  key = webidl.converters.CryptoKey(key, {
    prefix,
    context: '2nd argument',
  });
  data = webidl.converters.BufferSource(data, {
    prefix,
    context: '3rd argument',
  });

  algorithm = normalizeAlgorithm(algorithm, 'encrypt');
  return cipherOrWrap(kWebCryptoCipherEncrypt, algorithm, key, data, 'encrypt');
}

async function decrypt(algorithm, key, data) {
  if (this !== subtle) throw new ERR_INVALID_THIS('SubtleCrypto');

  webidl ??= require('internal/crypto/webidl');
  const prefix = "Failed to execute 'decrypt' on 'SubtleCrypto'";
  webidl.requiredArguments(arguments.length, 3, { prefix });
  algorithm = webidl.converters.AlgorithmIdentifier(algorithm, {
    prefix,
    context: '1st argument',
  });
  key = webidl.converters.CryptoKey(key, {
    prefix,
    context: '2nd argument',
  });
  data = webidl.converters.BufferSource(data, {
    prefix,
    context: '3rd argument',
  });

  algorithm = normalizeAlgorithm(algorithm, 'decrypt');
  return cipherOrWrap(kWebCryptoCipherDecrypt, algorithm, key, data, 'decrypt');
}

// The SubtleCrypto and Crypto classes are defined as part of the
// Web Crypto API standard: https://www.w3.org/TR/WebCryptoAPI/

class SubtleCrypto {
  constructor() {
    throw new ERR_ILLEGAL_CONSTRUCTOR();
  }
}
const subtle = ReflectConstruct(function() {}, [], SubtleCrypto);

class Crypto {
  constructor() {
    throw new ERR_ILLEGAL_CONSTRUCTOR();
  }

  get subtle() {
    if (this !== crypto) throw new ERR_INVALID_THIS('Crypto');
    return subtle;
  }
}
const crypto = ReflectConstruct(function() {}, [], Crypto);

function getRandomValues(array) {
  if (this !== crypto) throw new ERR_INVALID_THIS('Crypto');

  webidl ??= require('internal/crypto/webidl');
  const prefix = "Failed to execute 'getRandomValues' on 'Crypto'";
  webidl.requiredArguments(arguments.length, 1, { prefix });

  return ReflectApply(_getRandomValues, this, arguments);
}

ObjectDefineProperties(
  Crypto.prototype, {
    [SymbolToStringTag]: {
      __proto__: null,
      enumerable: false,
      configurable: true,
      writable: false,
      value: 'Crypto',
    },
    subtle: kEnumerableProperty,
    getRandomValues: {
      __proto__: null,
      enumerable: true,
      configurable: true,
      writable: true,
      value: getRandomValues,
    },
    randomUUID: {
      __proto__: null,
      enumerable: true,
      configurable: true,
      writable: true,
      value: randomUUID,
    },
  });

ObjectDefineProperties(
  SubtleCrypto.prototype, {
    [SymbolToStringTag]: {
      __proto__: null,
      enumerable: false,
      configurable: true,
      writable: false,
      value: 'SubtleCrypto',
    },
    encrypt: {
      __proto__: null,
      enumerable: true,
      configurable: true,
      writable: true,
      value: encrypt,
    },
    decrypt: {
      __proto__: null,
      enumerable: true,
      configurable: true,
      writable: true,
      value: decrypt,
    },
    sign: {
      __proto__: null,
      enumerable: true,
      configurable: true,
      writable: true,
      value: sign,
    },
    verify: {
      __proto__: null,
      enumerable: true,
      configurable: true,
      writable: true,
      value: verify,
    },
    digest: {
      __proto__: null,
      enumerable: true,
      configurable: true,
      writable: true,
      value: digest,
    },
    generateKey: {
      __proto__: null,
      enumerable: true,
      configurable: true,
      writable: true,
      value: generateKey,
    },
    deriveKey: {
      __proto__: null,
      enumerable: true,
      configurable: true,
      writable: true,
      value: deriveKey,
    },
    deriveBits: {
      __proto__: null,
      enumerable: true,
      configurable: true,
      writable: true,
      value: deriveBits,
    },
    importKey: {
      __proto__: null,
      enumerable: true,
      configurable: true,
      writable: true,
      value: importKey,
    },
    exportKey: {
      __proto__: null,
      enumerable: true,
      configurable: true,
      writable: true,
      value: exportKey,
    },
    wrapKey: {
      __proto__: null,
      enumerable: true,
      configurable: true,
      writable: true,
      value: wrapKey,
    },
    unwrapKey: {
      __proto__: null,
      enumerable: true,
      configurable: true,
      writable: true,
      value: unwrapKey,
    },
  });

module.exports = {
  Crypto,
  CryptoKey,
  SubtleCrypto,
  crypto,
};
