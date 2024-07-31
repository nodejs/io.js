'use strict';

const {
  MathCeil,
  SafeSet,
  Uint8Array,
} = primordials;

const {
  KeyObjectHandle,
  RSACipherJob,
  RSAKeyExportJob,
  SignJob,
  kCryptoJobAsync,
  kSignJobModeSign,
  kSignJobModeVerify,
  kKeyVariantRSA_SSA_PKCS1_v1_5,
  kKeyVariantRSA_PSS,
  kKeyVariantRSA_OAEP,
  kKeyTypePrivate,
  kWebCryptoCipherEncrypt,
  RSA_PKCS1_PSS_PADDING,
} = internalBinding('crypto');

const {
  validateInt32,
} = require('internal/validators');

const {
  bigIntArrayToUnsignedInt,
  getDigestSizeInBytes,
  getUsagesUnion,
  hasAnyNotIn,
  jobPromise,
  normalizeHashName,
  validateKeyOps,
  validateMaxBufferLength,
  kHandle,
  kKeyObject,
} = require('internal/crypto/util');

const {
  lazyDOMException,
  promisify,
} = require('internal/util');

const {
  InternalCryptoKey,
  PrivateKeyObject,
  PublicKeyObject,
  createPublicKey,
  createPrivateKey,
} = require('internal/crypto/keys');

const {
  generateKeyPair: _generateKeyPair,
} = require('internal/crypto/keygen');

const kRsaVariants = {
  'RSASSA-PKCS1-v1_5': kKeyVariantRSA_SSA_PKCS1_v1_5,
  'RSA-PSS': kKeyVariantRSA_PSS,
  'RSA-OAEP': kKeyVariantRSA_OAEP,
};
const generateKeyPair = promisify(_generateKeyPair);

function verifyAcceptableRsaKeyUse(name, isPublic, usages) {
  let checkSet;
  switch (name) {
    case 'RSA-OAEP':
      checkSet = isPublic ? ['encrypt', 'wrapKey'] : ['decrypt', 'unwrapKey'];
      break;
    case 'RSA-PSS':
      // Fall through
    case 'RSASSA-PKCS1-v1_5':
      checkSet = isPublic ? ['verify'] : ['sign'];
      break;
    default:
      throw lazyDOMException(
        'The algorithm is not supported', 'NotSupportedError');
  }
  if (hasAnyNotIn(usages, checkSet)) {
    throw lazyDOMException(
      `Unsupported key usage for an ${name} key`,
      'SyntaxError');
  }
}

function rsaOaepCipher(mode, key, data, { label }) {
  const type = mode === kWebCryptoCipherEncrypt ? 'public' : 'private';
  if (key.type !== type) {
    throw lazyDOMException(
      'The requested operation is not valid for the provided key',
      'InvalidAccessError');
  }
  if (label !== undefined) {
    validateMaxBufferLength(label, 'algorithm.label');
  }

  return jobPromise(() => new RSACipherJob(
    kCryptoJobAsync,
    mode,
    key[kKeyObject][kHandle],
    data,
    kKeyVariantRSA_OAEP,
    normalizeHashName(key.algorithm.hash.name),
    label));
}

async function rsaKeyGenerate(
  algorithm,
  extractable,
  keyUsages) {

  const {
    name,
    modulusLength,
    publicExponent,
    hash,
  } = algorithm;

  const usageSet = new SafeSet(keyUsages);

  const publicExponentConverted = bigIntArrayToUnsignedInt(publicExponent);
  if (publicExponentConverted === undefined) {
    throw lazyDOMException(
      'The publicExponent must be equivalent to an unsigned 32-bit value',
      'OperationError');
  }

  switch (name) {
    case 'RSA-OAEP':
      if (hasAnyNotIn(usageSet,
                      ['encrypt', 'decrypt', 'wrapKey', 'unwrapKey'])) {
        throw lazyDOMException(
          'Unsupported key usage for a RSA key',
          'SyntaxError');
      }
      break;
    default:
      if (hasAnyNotIn(usageSet, ['sign', 'verify'])) {
        throw lazyDOMException(
          'Unsupported key usage for a RSA key',
          'SyntaxError');
      }
  }

  const keypair = await generateKeyPair('rsa', {
    modulusLength,
    publicExponent: publicExponentConverted,
  }).catch((err) => {
    throw lazyDOMException(
      'The operation failed for an operation-specific reason',
      { name: 'OperationError', cause: err });
  });

  const keyAlgorithm = {
    name,
    modulusLength,
    publicExponent,
    hash: { name: hash.name },
  };

  let publicUsages;
  let privateUsages;
  switch (name) {
    case 'RSA-OAEP': {
      publicUsages = getUsagesUnion(usageSet, 'encrypt', 'wrapKey');
      privateUsages = getUsagesUnion(usageSet, 'decrypt', 'unwrapKey');
      break;
    }
    default: {
      publicUsages = getUsagesUnion(usageSet, 'verify');
      privateUsages = getUsagesUnion(usageSet, 'sign');
      break;
    }
  }

  const publicKey =
    new InternalCryptoKey(
      keypair.publicKey,
      keyAlgorithm,
      publicUsages,
      true);

  const privateKey =
    new InternalCryptoKey(
      keypair.privateKey,
      keyAlgorithm,
      privateUsages,
      extractable);

  return { __proto__: null, publicKey, privateKey };
}

function rsaExportKey(key, format) {
  return jobPromise(() => new RSAKeyExportJob(
    kCryptoJobAsync,
    format,
    key[kKeyObject][kHandle],
    kRsaVariants[key.algorithm.name]));
}

async function rsaImportKey(
  format,
  keyData,
  algorithm,
  extractable,
  keyUsages) {
  const usagesSet = new SafeSet(keyUsages);
  let keyObject;
  switch (format) {
    case 'spki': {
      verifyAcceptableRsaKeyUse(algorithm.name, true, usagesSet);
      try {
        keyObject = createPublicKey({
          key: keyData,
          format: 'der',
          type: 'spki',
        });
      } catch (err) {
        throw lazyDOMException(
          'Invalid keyData', { name: 'DataError', cause: err });
      }
      break;
    }
    case 'pkcs8': {
      verifyAcceptableRsaKeyUse(algorithm.name, false, usagesSet);
      try {
        keyObject = createPrivateKey({
          key: keyData,
          format: 'der',
          type: 'pkcs8',
        });
      } catch (err) {
        throw lazyDOMException(
          'Invalid keyData', { name: 'DataError', cause: err });
      }
      break;
    }
    case 'jwk': {
      if (!keyData.kty)
        throw lazyDOMException('Invalid keyData', 'DataError');

      if (keyData.kty !== 'RSA')
        throw lazyDOMException('Invalid JWK "kty" Parameter', 'DataError');

      verifyAcceptableRsaKeyUse(
        algorithm.name,
        keyData.d === undefined,
        usagesSet);

      if (usagesSet.size > 0 && keyData.use !== undefined) {
        const checkUse = algorithm.name === 'RSA-OAEP' ? 'enc' : 'sig';
        if (keyData.use !== checkUse)
          throw lazyDOMException('Invalid JWK "use" Parameter', 'DataError');
      }

      validateKeyOps(keyData.key_ops, usagesSet);

      if (keyData.ext !== undefined &&
          keyData.ext === false &&
          extractable === true) {
        throw lazyDOMException(
          'JWK "ext" Parameter and extractable mismatch',
          'DataError');
      }

      if (keyData.alg !== undefined) {
        const hash =
          normalizeHashName(keyData.alg, normalizeHashName.kContextWebCrypto);
        if (hash !== algorithm.hash.name)
          throw lazyDOMException(
            'JWK "alg" does not match the requested algorithm',
            'DataError');
      }

      const handle = new KeyObjectHandle();
      const type = handle.initJwk(keyData);
      if (type === undefined)
        throw lazyDOMException('Invalid JWK', 'DataError');

      keyObject = type === kKeyTypePrivate ?
        new PrivateKeyObject(handle) :
        new PublicKeyObject(handle);

      break;
    }
    default:
      throw lazyDOMException(
        `Unable to import RSA key with format ${format}`,
        'NotSupportedError');
  }

  if (keyObject.asymmetricKeyType !== 'rsa') {
    throw lazyDOMException('Invalid key type', 'DataError');
  }

  const {
    modulusLength,
    publicExponent,
  } = keyObject[kHandle].keyDetail({});

  return new InternalCryptoKey(keyObject, {
    name: algorithm.name,
    modulusLength,
    publicExponent: new Uint8Array(publicExponent),
    hash: algorithm.hash,
  }, keyUsages, extractable);
}

async function rsaSignVerify(key, data, { saltLength }, signature) {
  const mode = signature === undefined ? kSignJobModeSign : kSignJobModeVerify;
  const type = mode === kSignJobModeSign ? 'private' : 'public';

  if (key.type !== type)
    throw lazyDOMException(`Key must be a ${type} key`, 'InvalidAccessError');

  return jobPromise(() => {
    if (key.algorithm.name === 'RSA-PSS') {
      validateInt32(
        saltLength,
        'algorithm.saltLength',
        0,
        MathCeil((key.algorithm.modulusLength - 1) / 8) - getDigestSizeInBytes(key.algorithm.hash.name) - 2);
    }

    return new SignJob(
      kCryptoJobAsync,
      signature === undefined ? kSignJobModeSign : kSignJobModeVerify,
      key[kKeyObject][kHandle],
      undefined,
      undefined,
      undefined,
      data,
      normalizeHashName(key.algorithm.hash.name),
      saltLength,
      key.algorithm.name === 'RSA-PSS' ? RSA_PKCS1_PSS_PADDING : undefined,
      undefined,
      signature);
  });
}


module.exports = {
  rsaCipher: rsaOaepCipher,
  rsaExportKey,
  rsaImportKey,
  rsaKeyGenerate,
  rsaSignVerify,
};
