'use strict';

const {
  SafeSet,
} = primordials;

const { Buffer } = require('buffer');

const {
  ECKeyExportJob,
  KeyObjectHandle,
  SignJob,
  kCryptoJobAsync,
  kKeyTypePrivate,
  kKeyTypePublic,
  kSignJobModeSign,
  kSignJobModeVerify,
} = internalBinding('crypto');

const {
  codes: {
    ERR_CRYPTO_INVALID_JWK,
  },
} = require('internal/errors');

const {
  getUsagesUnion,
  hasAnyNotIn,
  jobPromise,
  validateKeyOps,
  kHandle,
  kKeyObject,
} = require('internal/crypto/util');

const {
  lazyDOMException,
  promisify,
} = require('internal/util');

const {
  generateKeyPair: _generateKeyPair,
} = require('internal/crypto/keygen');

const {
  InternalCryptoKey,
  PrivateKeyObject,
  PublicKeyObject,
  createPrivateKey,
  createPublicKey,
} = require('internal/crypto/keys');

const generateKeyPair = promisify(_generateKeyPair);

function verifyAcceptableCfrgKeyUse(name, isPublic, usages) {
  let checkSet;
  switch (name) {
    case 'X25519':
      // Fall through
    case 'X448':
      checkSet = isPublic ? [] : ['deriveKey', 'deriveBits'];
      break;
    case 'Ed25519':
      // Fall through
    case 'Ed448':
      checkSet = isPublic ? ['verify'] : ['sign'];
      break;
    default:
      throw lazyDOMException(
        'The algorithm is not supported', 'NotSupportedError');
  }
  if (hasAnyNotIn(usages, checkSet)) {
    throw lazyDOMException(
      `Unsupported key usage for a ${name} key`,
      'SyntaxError');
  }
}

function createCFRGRawKey(name, keyData, isPublic) {
  const handle = new KeyObjectHandle();

  switch (name) {
    case 'Ed25519':
    case 'X25519':
      if (keyData.byteLength !== 32) {
        throw lazyDOMException(
          `${name} raw keys must be exactly 32-bytes`, 'DataError');
      }
      break;
    case 'Ed448':
      if (keyData.byteLength !== 57) {
        throw lazyDOMException(
          `${name} raw keys must be exactly 57-bytes`, 'DataError');
      }
      break;
    case 'X448':
      if (keyData.byteLength !== 56) {
        throw lazyDOMException(
          `${name} raw keys must be exactly 56-bytes`, 'DataError');
      }
      break;
  }

  const keyType = isPublic ? kKeyTypePublic : kKeyTypePrivate;
  if (!handle.initEDRaw(name, keyData, keyType)) {
    throw lazyDOMException('Invalid keyData', 'DataError');
  }

  return isPublic ? new PublicKeyObject(handle) : new PrivateKeyObject(handle);
}

async function cfrgGenerateKey(algorithm, extractable, keyUsages) {
  const { name } = algorithm;

  const usageSet = new SafeSet(keyUsages);
  switch (name) {
    case 'Ed25519':
      // Fall through
    case 'Ed448':
      if (hasAnyNotIn(usageSet, ['sign', 'verify'])) {
        throw lazyDOMException(
          `Unsupported key usage for an ${name} key`,
          'SyntaxError');
      }
      break;
    case 'X25519':
      // Fall through
    case 'X448':
      if (hasAnyNotIn(usageSet, ['deriveKey', 'deriveBits'])) {
        throw lazyDOMException(
          `Unsupported key usage for an ${name} key`,
          'SyntaxError');
      }
      break;
  }
  let genKeyType;
  switch (name) {
    case 'Ed25519':
      genKeyType = 'ed25519';
      break;
    case 'Ed448':
      genKeyType = 'ed448';
      break;
    case 'X25519':
      genKeyType = 'x25519';
      break;
    case 'X448':
      genKeyType = 'x448';
      break;
  }

  const keyPair = await generateKeyPair(genKeyType).catch((err) => {
    throw lazyDOMException(
      'The operation failed for an operation-specific reason',
      { name: 'OperationError', cause: err });
  });

  let publicUsages;
  let privateUsages;
  switch (name) {
    case 'Ed25519':
      // Fall through
    case 'Ed448':
      publicUsages = getUsagesUnion(usageSet, 'verify');
      privateUsages = getUsagesUnion(usageSet, 'sign');
      break;
    case 'X25519':
      // Fall through
    case 'X448':
      publicUsages = [];
      privateUsages = getUsagesUnion(usageSet, 'deriveKey', 'deriveBits');
      break;
  }

  const keyAlgorithm = { name };

  const publicKey =
    new InternalCryptoKey(
      keyPair.publicKey,
      keyAlgorithm,
      publicUsages,
      true);

  const privateKey =
    new InternalCryptoKey(
      keyPair.privateKey,
      keyAlgorithm,
      privateUsages,
      extractable);

  return { __proto__: null, privateKey, publicKey };
}

function cfrgExportKey(key, format) {
  return jobPromise(() => new ECKeyExportJob(
    kCryptoJobAsync,
    format,
    key[kKeyObject][kHandle]));
}

function cfrgImportKey(
  format,
  keyData,
  algorithm,
  extractable,
  keyUsages) {

  const { name } = algorithm;
  let keyObject;
  const usagesSet = new SafeSet(keyUsages);
  switch (format) {
    case 'KeyObject': {
      verifyAcceptableCfrgKeyUse(name, keyData.type === 'public', usagesSet);
      keyObject = keyData;
      break;
    }
    case 'spki': {
      verifyAcceptableCfrgKeyUse(name, true, usagesSet);
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
      verifyAcceptableCfrgKeyUse(name, false, usagesSet);
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
      if (keyData.kty !== 'OKP')
        throw lazyDOMException('Invalid JWK "kty" Parameter', 'DataError');
      if (keyData.crv !== name)
        throw lazyDOMException(
          'JWK "crv" Parameter and algorithm name mismatch', 'DataError');
      const isPublic = keyData.d === undefined;

      if (usagesSet.size > 0 && keyData.use !== undefined) {
        let checkUse;
        switch (name) {
          case 'Ed25519':
            // Fall through
          case 'Ed448':
            checkUse = 'sig';
            break;
          case 'X25519':
            // Fall through
          case 'X448':
            checkUse = 'enc';
            break;
        }
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

      if (!isPublic && typeof keyData.x !== 'string') {
        throw lazyDOMException('Invalid JWK', 'DataError');
      }

      verifyAcceptableCfrgKeyUse(
        name,
        isPublic,
        usagesSet);

      try {
        const publicKeyObject = createCFRGRawKey(
          name,
          Buffer.from(keyData.x, 'base64'),
          true);

        if (isPublic) {
          keyObject = publicKeyObject;
        } else {
          keyObject = createCFRGRawKey(
            name,
            Buffer.from(keyData.d, 'base64'),
            false);

          if (!createPublicKey(keyObject).equals(publicKeyObject)) {
            throw new ERR_CRYPTO_INVALID_JWK();
          }
        }
      } catch (err) {
        throw lazyDOMException('Invalid keyData', { name: 'DataError', cause: err });
      }
      break;
    }
    case 'raw': {
      verifyAcceptableCfrgKeyUse(name, true, usagesSet);
      keyObject = createCFRGRawKey(name, keyData, true);
      break;
    }
  }

  if (keyObject.asymmetricKeyType !== name.toLowerCase()) {
    throw lazyDOMException('Invalid key type', 'DataError');
  }

  return new InternalCryptoKey(
    keyObject,
    { name },
    keyUsages,
    extractable);
}

function eddsaSignVerify(key, data, { name, context }, signature) {
  const mode = signature === undefined ? kSignJobModeSign : kSignJobModeVerify;
  const type = mode === kSignJobModeSign ? 'private' : 'public';

  if (key.type !== type)
    throw lazyDOMException(`Key must be a ${type} key`, 'InvalidAccessError');

  if (name === 'Ed448' && context?.byteLength) {
    throw lazyDOMException(
      'Non zero-length context is not yet supported.', 'NotSupportedError');
  }

  return jobPromise(() => new SignJob(
    kCryptoJobAsync,
    mode,
    key[kKeyObject][kHandle],
    undefined,
    undefined,
    undefined,
    data,
    undefined,
    undefined,
    undefined,
    undefined,
    signature));
}

module.exports = {
  cfrgExportKey,
  cfrgImportKey,
  cfrgGenerateKey,
  eddsaSignVerify,
};
