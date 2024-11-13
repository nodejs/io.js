'use strict';

const {
  ArrayBufferIsView,
  ArrayBufferPrototypeGetByteLength,
  ArrayPrototypeIncludes,
  ArrayPrototypePush,
  BigInt,
  DataViewPrototypeGetBuffer,
  DataViewPrototypeGetByteLength,
  DataViewPrototypeGetByteOffset,
  FunctionPrototypeBind,
  Number,
  ObjectDefineProperty,
  ObjectEntries,
  ObjectKeys,
  ObjectPrototypeHasOwnProperty,
  Promise,
  StringPrototypeToUpperCase,
  Symbol,
  TypedArrayPrototypeGetBuffer,
  TypedArrayPrototypeGetByteLength,
  TypedArrayPrototypeGetByteOffset,
  TypedArrayPrototypeSlice,
  Uint8Array,
} = primordials;

const {
  getCiphers: _getCiphers,
  getCurves: _getCurves,
  getHashes: _getHashes,
  setEngine: _setEngine,
  secureHeapUsed: _secureHeapUsed,
  getCachedAliases,
} = internalBinding('crypto');

const { getOptionValue } = require('internal/options');

const {
  crypto: {
    ENGINE_METHOD_ALL,
  },
} = internalBinding('constants');

const normalizeHashName = require('internal/crypto/hashnames');

const {
  codes: {
    ERR_CRYPTO_CUSTOM_ENGINE_NOT_SUPPORTED,
    ERR_CRYPTO_ENGINE_UNKNOWN,
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_OUT_OF_RANGE,
  },
  hideStackFrames,
} = require('internal/errors');

const {
  validateArray,
  validateNumber,
  validateString,
} = require('internal/validators');

const { Buffer } = require('buffer');

const {
  cachedResult,
  emitExperimentalWarning,
  filterDuplicateStrings,
  lazyDOMException,
} = require('internal/util');

const {
  namespace: {
    isBuildingSnapshot,
    addSerializeCallback,
  },
} = require('internal/v8/startup_snapshot');

const {
  isDataView,
  isArrayBufferView,
  isAnyArrayBuffer,
} = require('internal/util/types');

const kHandle = Symbol('kHandle');
const kKeyObject = Symbol('kKeyObject');

// This is here because many functions accepted binary strings without
// any explicit encoding in older versions of node, and we don't want
// to break them unnecessarily.
function toBuf(val, encoding) {
  if (typeof val === 'string') {
    if (encoding === 'buffer')
      encoding = 'utf8';
    return Buffer.from(val, encoding);
  }
  return val;
}

let _hashCache;
function getHashCache() {
  if (_hashCache === undefined) {
    _hashCache = getCachedAliases();
    if (isBuildingSnapshot()) {
      // For dynamic linking, clear the map.
      addSerializeCallback(() => { _hashCache = undefined; });
    }
  }
  return _hashCache;
}

function getCachedHashId(algorithm) {
  const result = getHashCache()[algorithm];
  return result === undefined ? -1 : result;
}

const getCiphers = cachedResult(() => filterDuplicateStrings(_getCiphers()));
const getHashes = cachedResult(() => filterDuplicateStrings(_getHashes()));
const getCurves = cachedResult(() => filterDuplicateStrings(_getCurves()));

function setEngine(id, flags) {
  validateString(id, 'id');
  if (flags)
    validateNumber(flags, 'flags');
  flags = flags >>> 0;

  // Use provided engine for everything by default
  if (flags === 0)
    flags = ENGINE_METHOD_ALL;

  if (typeof _setEngine !== 'function')
    throw new ERR_CRYPTO_CUSTOM_ENGINE_NOT_SUPPORTED();
  if (!_setEngine(id, flags))
    throw new ERR_CRYPTO_ENGINE_UNKNOWN(id);
}

const getArrayBufferOrView = hideStackFrames((buffer, name, encoding) => {
  if (isAnyArrayBuffer(buffer))
    return buffer;
  if (typeof buffer === 'string') {
    if (encoding === 'buffer')
      encoding = 'utf8';
    return Buffer.from(buffer, encoding);
  }
  if (!isArrayBufferView(buffer)) {
    throw new ERR_INVALID_ARG_TYPE.HideStackFramesError(
      name,
      [
        'string',
        'ArrayBuffer',
        'Buffer',
        'TypedArray',
        'DataView',
      ],
      buffer,
    );
  }
  return buffer;
});

// The maximum buffer size that we'll support in the WebCrypto impl
const kMaxBufferLength = (2 ** 31) - 1;

// The EC named curves that we currently support via the Web Crypto API.
const kNamedCurveAliases = {
  'P-256': 'prime256v1',
  'P-384': 'secp384r1',
  'P-521': 'secp521r1',
};

const kAesKeyLengths = [128, 192, 256];

const kSupportedAlgorithms = {
  'digest': {
    'SHA-1': null,
    'SHA-256': null,
    'SHA-384': null,
    'SHA-512': null,
  },
  'generateKey': {
    'RSASSA-PKCS1-v1_5': 'RsaHashedKeyGenParams',
    'RSA-PSS': 'RsaHashedKeyGenParams',
    'RSA-OAEP': 'RsaHashedKeyGenParams',
    'ECDSA': 'EcKeyGenParams',
    'ECDH': 'EcKeyGenParams',
    'AES-CTR': 'AesKeyGenParams',
    'AES-CBC': 'AesKeyGenParams',
    'AES-GCM': 'AesKeyGenParams',
    'AES-KW': 'AesKeyGenParams',
    'HMAC': 'HmacKeyGenParams',
  },
  'sign': {
    'RSASSA-PKCS1-v1_5': null,
    'RSA-PSS': 'RsaPssParams',
    'ECDSA': 'EcdsaParams',
    'HMAC': null,
  },
  'verify': {
    'RSASSA-PKCS1-v1_5': null,
    'RSA-PSS': 'RsaPssParams',
    'ECDSA': 'EcdsaParams',
    'HMAC': null,
  },
  'importKey': {
    'RSASSA-PKCS1-v1_5': 'RsaHashedImportParams',
    'RSA-PSS': 'RsaHashedImportParams',
    'RSA-OAEP': 'RsaHashedImportParams',
    'ECDSA': 'EcKeyImportParams',
    'ECDH': 'EcKeyImportParams',
    'HMAC': 'HmacImportParams',
    'HKDF': null,
    'PBKDF2': null,
    'AES-CTR': null,
    'AES-CBC': null,
    'AES-GCM': null,
    'AES-KW': null,
  },
  'deriveBits': {
    'HKDF': 'HkdfParams',
    'PBKDF2': 'Pbkdf2Params',
    'ECDH': 'EcdhKeyDeriveParams',
  },
  'encrypt': {
    'RSA-OAEP': 'RsaOaepParams',
    'AES-CBC': 'AesCbcParams',
    'AES-GCM': 'AesGcmParams',
    'AES-CTR': 'AesCtrParams',
  },
  'decrypt': {
    'RSA-OAEP': 'RsaOaepParams',
    'AES-CBC': 'AesCbcParams',
    'AES-GCM': 'AesGcmParams',
    'AES-CTR': 'AesCtrParams',
  },
  'get key length': {
    'AES-CBC': 'AesDerivedKeyParams',
    'AES-CTR': 'AesDerivedKeyParams',
    'AES-GCM': 'AesDerivedKeyParams',
    'AES-KW': 'AesDerivedKeyParams',
    'HMAC': 'HmacImportParams',
    'HKDF': null,
    'PBKDF2': null,
  },
  'wrapKey': {
    'AES-KW': null,
  },
  'unwrapKey': {
    'AES-KW': null,
  },
};

const experimentalAlgorithms = ObjectEntries({
  'X25519': {
    generateKey: null,
    importKey: null,
    deriveBits: 'EcdhKeyDeriveParams',
  },
  'Ed25519': {
    generateKey: null,
    sign: null,
    verify: null,
    importKey: null,
  },
  'X448': {
    generateKey: null,
    importKey: null,
    deriveBits: 'EcdhKeyDeriveParams',
  },
  'Ed448': {
    generateKey: null,
    sign: 'Ed448Params',
    verify: 'Ed448Params',
    importKey: null,
  },
});

for (let i = 0; i < experimentalAlgorithms.length; i++) {
  const name = experimentalAlgorithms[i][0];
  const ops = ObjectEntries(experimentalAlgorithms[i][1]);
  for (let j = 0; j < ops.length; j++) {
    const { 0: op, 1: dict } = ops[j];
    ObjectDefineProperty(kSupportedAlgorithms[op], name, {
      get() {
        emitExperimentalWarning(`The ${name} Web Crypto API algorithm`);
        return dict;
      },
      __proto__: null,
      enumerable: true,
    });
  }
}

const simpleAlgorithmDictionaries = {
  AesGcmParams: { iv: 'BufferSource', additionalData: 'BufferSource' },
  RsaHashedKeyGenParams: { hash: 'HashAlgorithmIdentifier' },
  EcKeyGenParams: {},
  HmacKeyGenParams: { hash: 'HashAlgorithmIdentifier' },
  RsaPssParams: {},
  EcdsaParams: { hash: 'HashAlgorithmIdentifier' },
  HmacImportParams: { hash: 'HashAlgorithmIdentifier' },
  HkdfParams: {
    hash: 'HashAlgorithmIdentifier',
    salt: 'BufferSource',
    info: 'BufferSource',
  },
  Ed448Params: { context: 'BufferSource' },
  Pbkdf2Params: { hash: 'HashAlgorithmIdentifier', salt: 'BufferSource' },
  RsaOaepParams: { label: 'BufferSource' },
  RsaHashedImportParams: { hash: 'HashAlgorithmIdentifier' },
  EcKeyImportParams: {},
};

function validateMaxBufferLength(data, name) {
  if (data.byteLength > kMaxBufferLength) {
    throw lazyDOMException(
      `${name} must be less than ${kMaxBufferLength + 1} bits`,
      'OperationError');
  }
}

let webidl;

// https://w3c.github.io/webcrypto/#algorithm-normalization-normalize-an-algorithm
// adapted for Node.js from Deno's implementation
// https://github.com/denoland/deno/blob/v1.29.1/ext/crypto/00_crypto.js#L195
function normalizeAlgorithm(algorithm, op) {
  if (typeof algorithm === 'string')
    return normalizeAlgorithm({ name: algorithm }, op);

  webidl ??= require('internal/crypto/webidl');

  // 1.
  const registeredAlgorithms = kSupportedAlgorithms[op];
  // 2. 3.
  const initialAlg = webidl.converters.Algorithm(algorithm, {
    prefix: 'Failed to normalize algorithm',
    context: 'passed algorithm',
  });
  // 4.
  let algName = initialAlg.name;

  // 5.
  let desiredType;
  for (const key in registeredAlgorithms) {
    if (!ObjectPrototypeHasOwnProperty(registeredAlgorithms, key)) {
      continue;
    }
    if (
      StringPrototypeToUpperCase(key) === StringPrototypeToUpperCase(algName)
    ) {
      algName = key;
      desiredType = registeredAlgorithms[key];
    }
  }
  if (desiredType === undefined)
    throw lazyDOMException('Unrecognized algorithm name', 'NotSupportedError');

  // Fast path everything below if the registered dictionary is null
  if (desiredType === null)
    return { name: algName };

  // 6.
  const normalizedAlgorithm = webidl.converters[desiredType](algorithm, {
    prefix: 'Failed to normalize algorithm',
    context: 'passed algorithm',
  });
  // 7.
  normalizedAlgorithm.name = algName;

  // 9.
  const dict = simpleAlgorithmDictionaries[desiredType];
  // 10.
  const dictKeys = dict ? ObjectKeys(dict) : [];
  for (let i = 0; i < dictKeys.length; i++) {
    const member = dictKeys[i];
    if (!ObjectPrototypeHasOwnProperty(dict, member))
      continue;
    const idlType = dict[member];
    const idlValue = normalizedAlgorithm[member];
    // 3.
    if (idlType === 'BufferSource' && idlValue) {
      const isView = ArrayBufferIsView(idlValue);
      normalizedAlgorithm[member] = TypedArrayPrototypeSlice(
        new Uint8Array(
          isView ? getDataViewOrTypedArrayBuffer(idlValue) : idlValue,
          isView ? getDataViewOrTypedArrayByteOffset(idlValue) : 0,
          isView ? getDataViewOrTypedArrayByteLength(idlValue) : ArrayBufferPrototypeGetByteLength(idlValue),
        ),
      );
    } else if (idlType === 'HashAlgorithmIdentifier') {
      normalizedAlgorithm[member] = normalizeAlgorithm(idlValue, 'digest');
    } else if (idlType === 'AlgorithmIdentifier') {
      // This extension point is not used by any supported algorithm (yet?)
      throw lazyDOMException('Not implemented.', 'NotSupportedError');
    }
  }

  return normalizedAlgorithm;
}

function getDataViewOrTypedArrayBuffer(V) {
  return isDataView(V) ?
    DataViewPrototypeGetBuffer(V) : TypedArrayPrototypeGetBuffer(V);
}

function getDataViewOrTypedArrayByteOffset(V) {
  return isDataView(V) ?
    DataViewPrototypeGetByteOffset(V) : TypedArrayPrototypeGetByteOffset(V);
}

function getDataViewOrTypedArrayByteLength(V) {
  return isDataView(V) ?
    DataViewPrototypeGetByteLength(V) : TypedArrayPrototypeGetByteLength(V);
}

function hasAnyNotIn(set, checks) {
  for (const s of set)
    if (!ArrayPrototypeIncludes(checks, s))
      return true;
  return false;
}

function validateBitLength(length, name, required = false) {
  if (length !== undefined || required) {
    validateNumber(length, name);
    if (length < 0)
      throw new ERR_OUT_OF_RANGE(name, '> 0');
    if (length % 8) {
      throw new ERR_INVALID_ARG_VALUE(
        name,
        length,
        'must be a multiple of 8');
    }
  }
}

function validateByteLength(buf, name, target) {
  if (buf.byteLength !== target) {
    throw lazyDOMException(
      `${name} must contain exactly ${target} bytes`,
      'OperationError');
  }
}

const validateByteSource = hideStackFrames((val, name) => {
  val = toBuf(val);

  if (isAnyArrayBuffer(val) || isArrayBufferView(val))
    return val;

  throw new ERR_INVALID_ARG_TYPE.HideStackFramesError(
    name,
    [
      'string',
      'ArrayBuffer',
      'TypedArray',
      'DataView',
      'Buffer',
    ],
    val);
});

function onDone(resolve, reject, err, result) {
  if (err) {
    return reject(lazyDOMException(
      'The operation failed for an operation-specific reason',
      { name: 'OperationError', cause: err }));
  }
  resolve(result);
}

function jobPromise(getJob) {
  return new Promise((resolve, reject) => {
    try {
      const job = getJob();
      job.ondone = FunctionPrototypeBind(onDone, job, resolve, reject);
      job.run();
    } catch (err) {
      onDone(resolve, reject, err);
    }
  });
}

// In WebCrypto, the publicExponent option in RSA is represented as a
// WebIDL "BigInteger"... that is, a Uint8Array that allows an arbitrary
// number of leading zero bits. Our conventional APIs for reading
// an unsigned int from a Buffer are not adequate. The implementation
// here is adapted from the chromium implementation here:
// https://github.com/chromium/chromium/blob/HEAD/third_party/blink/public/platform/web_crypto_algorithm_params.h, but ported to JavaScript
// Returns undefined if the conversion was unsuccessful.
function bigIntArrayToUnsignedInt(input) {
  let result = 0;

  for (let n = 0; n < input.length; ++n) {
    const n_reversed = input.length - n - 1;
    if (n_reversed >= 4 && input[n])
      return;  // Too large
    result |= input[n] << 8 * n_reversed;
  }

  return result;
}

function bigIntArrayToUnsignedBigInt(input) {
  let result = 0n;

  for (let n = 0; n < input.length; ++n) {
    const n_reversed = input.length - n - 1;
    result |= BigInt(input[n]) << 8n * BigInt(n_reversed);
  }

  return result;
}

function getStringOption(options, key) {
  let value;
  if (options && (value = options[key]) != null)
    validateString(value, `options.${key}`);
  return value;
}

function getUsagesUnion(usageSet, ...usages) {
  const newset = [];
  for (let n = 0; n < usages.length; n++) {
    if (usageSet.has(usages[n]))
      ArrayPrototypePush(newset, usages[n]);
  }
  return newset;
}

function getBlockSize(name) {
  switch (name) {
    case 'SHA-1':
      // Fall through
    case 'SHA-256':
      return 512;
    case 'SHA-384':
      // Fall through
    case 'SHA-512':
      return 1024;
  }
}

function getDigestSizeInBytes(name) {
  switch (name) {
    case 'SHA-1': return 20;
    case 'SHA-256': return 32;
    case 'SHA-384': return 48;
    case 'SHA-512': return 64;
  }
}

const kKeyOps = {
  sign: 1,
  verify: 2,
  encrypt: 3,
  decrypt: 4,
  wrapKey: 5,
  unwrapKey: 6,
  deriveKey: 7,
  deriveBits: 8,
};

function validateKeyOps(keyOps, usagesSet) {
  if (keyOps === undefined) return;
  validateArray(keyOps, 'keyData.key_ops');
  let flags = 0;
  for (let n = 0; n < keyOps.length; n++) {
    const op = keyOps[n];
    const op_flag = kKeyOps[op];
    // Skipping unknown key ops
    if (op_flag === undefined)
      continue;
    // Have we seen it already? if so, error
    if (flags & (1 << op_flag))
      throw lazyDOMException('Duplicate key operation', 'DataError');
    flags |= (1 << op_flag);

    // TODO(@jasnell): RFC7517 section 4.3 strong recommends validating
    // key usage combinations. Specifically, it says that unrelated key
    // ops SHOULD NOT be used together. We're not yet validating that here.
  }

  if (usagesSet !== undefined) {
    for (const use of usagesSet) {
      if (!ArrayPrototypeIncludes(keyOps, use)) {
        throw lazyDOMException(
          'Key operations and usage mismatch',
          'DataError');
      }
    }
  }
}

function secureHeapUsed() {
  const val = _secureHeapUsed();
  if (val === undefined)
    return { total: 0, used: 0, utilization: 0, min: 0 };
  const used = Number(_secureHeapUsed());
  const total = Number(getOptionValue('--secure-heap'));
  const min = Number(getOptionValue('--secure-heap-min'));
  const utilization = used / total;
  return { total, used, utilization, min };
}

module.exports = {
  getArrayBufferOrView,
  getCiphers,
  getCurves,
  getDataViewOrTypedArrayBuffer,
  getHashes,
  kHandle,
  kKeyObject,
  setEngine,
  toBuf,

  kNamedCurveAliases,
  kAesKeyLengths,
  normalizeAlgorithm,
  normalizeHashName,
  hasAnyNotIn,
  validateBitLength,
  validateByteLength,
  validateByteSource,
  validateKeyOps,
  jobPromise,
  validateMaxBufferLength,
  bigIntArrayToUnsignedBigInt,
  bigIntArrayToUnsignedInt,
  getBlockSize,
  getDigestSizeInBytes,
  getStringOption,
  getUsagesUnion,
  secureHeapUsed,
  getCachedHashId,
  getHashCache,
};
