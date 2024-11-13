'use strict';

const {
  ArrayIsArray,
  ArrayPrototypeFilter,
  ArrayPrototypeForEach,
  ArrayPrototypeJoin,
  StringPrototypeSplit,
  StringPrototypeStartsWith,
} = primordials;

const {
  codes: {
    ERR_CRYPTO_CUSTOM_ENGINE_NOT_SUPPORTED,
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
  },
} = require('internal/errors');

const {
  kEmptyObject,
} = require('internal/util');

const {
  isArrayBufferView,
} = require('internal/util/types');

const {
  validateBuffer,
  validateInt32,
  validateObject,
  validateString,
} = require('internal/validators');

const {
  toBuf,
} = require('internal/crypto/util');

const {
  crypto: {
    TLS1_2_VERSION,
    TLS1_3_VERSION,
  },
} = internalBinding('constants');

function getDefaultEcdhCurve() {
  // We do it this way because DEFAULT_ECDH_CURVE can be
  // changed by users, so we need to grab the current
  // value, but we want the evaluation to be lazy.
  return require('tls').DEFAULT_ECDH_CURVE || 'auto';
}

function getDefaultCiphers() {
  // We do it this way because DEFAULT_CIPHERS can be
  // changed by users, so we need to grab the current
  // value, but we want the evaluation to be lazy.
  return require('tls').DEFAULT_CIPHERS;
}

function addCACerts(context, certs, name) {
  ArrayPrototypeForEach(certs, (cert) => {
    validateKeyOrCertOption(name, cert);
    context.addCACert(cert);
  });
}

function setCerts(context, certs, name) {
  ArrayPrototypeForEach(certs, (cert) => {
    validateKeyOrCertOption(name, cert);
    context.setCert(cert);
  });
}

function validateKeyOrCertOption(name, value) {
  if (typeof value !== 'string' && !isArrayBufferView(value)) {
    throw new ERR_INVALID_ARG_TYPE(
      name,
      [
        'string',
        'Buffer',
        'TypedArray',
        'DataView',
      ],
      value,
    );
  }
}

function setKey(context, key, passphrase, name) {
  validateKeyOrCertOption(`${name}.key`, key);
  if (passphrase !== undefined && passphrase !== null)
    validateString(passphrase, `${name}.passphrase`);
  context.setKey(key, passphrase);
}

function processCiphers(ciphers, name) {
  ciphers = StringPrototypeSplit(ciphers || getDefaultCiphers(), ':');

  const cipherList =
    ArrayPrototypeJoin(
      ArrayPrototypeFilter(
        ciphers,
        (cipher) => {
          if (cipher.length === 0) return false;
          if (StringPrototypeStartsWith(cipher, 'TLS_')) return false;
          if (StringPrototypeStartsWith(cipher, '!TLS_')) return false;
          return true;
        }), ':');

  const cipherSuites =
    ArrayPrototypeJoin(
      ArrayPrototypeFilter(
        ciphers,
        (cipher) => {
          if (cipher.length === 0) return false;
          if (StringPrototypeStartsWith(cipher, 'TLS_')) return true;
          if (StringPrototypeStartsWith(cipher, '!TLS_')) return true;
          return false;
        }), ':');

  // Specifying empty cipher suites for both TLS1.2 and TLS1.3 is invalid, its
  // not possible to handshake with no suites.
  if (cipherSuites === '' && cipherList === '')
    throw new ERR_INVALID_ARG_VALUE(name, ciphers);

  return { cipherList, cipherSuites };
}

function configSecureContext(context, options = kEmptyObject, name = 'options') {
  validateObject(options, name);

  const {
    allowPartialTrustChain,
    ca,
    cert,
    ciphers = getDefaultCiphers(),
    clientCertEngine,
    crl,
    dhparam,
    ecdhCurve = getDefaultEcdhCurve(),
    key,
    passphrase,
    pfx,
    privateKeyIdentifier,
    privateKeyEngine,
    sessionIdContext,
    sessionTimeout,
    sigalgs,
    ticketKeys,
  } = options;

  // Set the cipher list and cipher suite before anything else because
  // @SECLEVEL=<n> changes the security level and that affects subsequent
  // operations.
  if (ciphers !== undefined && ciphers !== null)
    validateString(ciphers, `${name}.ciphers`);

  // Work around an OpenSSL API quirk. cipherList is for TLSv1.2 and below,
  // cipherSuites is for TLSv1.3 (and presumably any later versions). TLSv1.3
  // cipher suites all have a standard name format beginning with TLS_, so split
  // the ciphers and pass them to the appropriate API.
  const {
    cipherList,
    cipherSuites,
  } = processCiphers(ciphers, `${name}.ciphers`);

  if (cipherSuites !== '')
    context.setCipherSuites(cipherSuites);
  context.setCiphers(cipherList);

  if (cipherList === '' &&
      context.getMinProto() < TLS1_3_VERSION &&
      context.getMaxProto() > TLS1_2_VERSION) {
    context.setMinProto(TLS1_3_VERSION);
  }

  // Add CA before the cert to be able to load cert's issuer in C++ code.
  // NOTE(@jasnell): ca, cert, and key are permitted to be falsy, so do not
  // change the checks to !== undefined checks.
  if (ca) {
    addCACerts(context, ArrayIsArray(ca) ? ca : [ca], `${name}.ca`);
  } else {
    context.addRootCerts();
  }

  if (allowPartialTrustChain) {
    context.setAllowPartialTrustChain();
  }

  if (cert) {
    setCerts(context, ArrayIsArray(cert) ? cert : [cert], `${name}.cert`);
  }

  // Set the key after the cert.
  // `ssl_set_pkey` returns `0` when the key does not match the cert, but
  // `ssl_set_cert` returns `1` and nullifies the key in the SSL structure
  // which leads to the crash later on.
  if (key) {
    if (ArrayIsArray(key)) {
      for (let i = 0; i < key.length; ++i) {
        const val = key[i];
        const pem = (
          val?.pem !== undefined ? val.pem : val);
        const pass = (
          val?.passphrase !== undefined ? val.passphrase : passphrase);
        setKey(context, pem, pass, name);
      }
    } else {
      setKey(context, key, passphrase, name);
    }
  }

  if (sigalgs !== undefined && sigalgs !== null) {
    validateString(sigalgs, `${name}.sigalgs`);

    if (sigalgs === '')
      throw new ERR_INVALID_ARG_VALUE(`${name}.sigalgs`, sigalgs);

    context.setSigalgs(sigalgs);
  }

  if (privateKeyIdentifier !== undefined && privateKeyIdentifier !== null) {
    if (privateKeyEngine === undefined || privateKeyEngine === null) {
      // Engine is required when privateKeyIdentifier is present
      throw new ERR_INVALID_ARG_VALUE(`${name}.privateKeyEngine`,
                                      privateKeyEngine);
    }
    if (key) {
      // Both data key and engine key can't be set at the same time
      throw new ERR_INVALID_ARG_VALUE(`${name}.privateKeyIdentifier`,
                                      privateKeyIdentifier);
    }

    if (typeof privateKeyIdentifier === 'string' &&
        typeof privateKeyEngine === 'string') {
      if (context.setEngineKey)
        context.setEngineKey(privateKeyIdentifier, privateKeyEngine);
      else
        throw new ERR_CRYPTO_CUSTOM_ENGINE_NOT_SUPPORTED();
    } else if (typeof privateKeyIdentifier !== 'string') {
      throw new ERR_INVALID_ARG_TYPE(`${name}.privateKeyIdentifier`,
                                     ['string', 'null', 'undefined'],
                                     privateKeyIdentifier);
    } else {
      throw new ERR_INVALID_ARG_TYPE(`${name}.privateKeyEngine`,
                                     ['string', 'null', 'undefined'],
                                     privateKeyEngine);
    }
  }

  validateString(ecdhCurve, `${name}.ecdhCurve`);
  context.setECDHCurve(ecdhCurve);

  if (dhparam !== undefined && dhparam !== null) {
    validateKeyOrCertOption(`${name}.dhparam`, dhparam);
    const warning = context.setDHParam(dhparam === 'auto' || dhparam);
    if (warning)
      process.emitWarning(warning, 'SecurityWarning');
  }

  if (crl !== undefined && crl !== null) {
    if (ArrayIsArray(crl)) {
      for (const val of crl) {
        validateKeyOrCertOption(`${name}.crl`, val);
        context.addCRL(val);
      }
    } else {
      validateKeyOrCertOption(`${name}.crl`, crl);
      context.addCRL(crl);
    }
  }

  if (sessionIdContext !== undefined && sessionIdContext !== null) {
    validateString(sessionIdContext, `${name}.sessionIdContext`);
    context.setSessionIdContext(sessionIdContext);
  }

  if (pfx !== undefined && pfx !== null) {
    if (ArrayIsArray(pfx)) {
      ArrayPrototypeForEach(pfx, (val) => {
        const raw = val.buf || val;
        const pass = val.passphrase || passphrase;
        if (pass !== undefined && pass !== null) {
          context.loadPKCS12(toBuf(raw), toBuf(pass));
        } else {
          context.loadPKCS12(toBuf(raw));
        }
      });
    } else if (passphrase) {
      context.loadPKCS12(toBuf(pfx), toBuf(passphrase));
    } else {
      context.loadPKCS12(toBuf(pfx));
    }
  }

  if (typeof clientCertEngine === 'string') {
    if (typeof context.setClientCertEngine !== 'function')
      throw new ERR_CRYPTO_CUSTOM_ENGINE_NOT_SUPPORTED();
    else
      context.setClientCertEngine(clientCertEngine);
  } else if (clientCertEngine !== undefined && clientCertEngine !== null) {
    throw new ERR_INVALID_ARG_TYPE(`${name}.clientCertEngine`,
                                   ['string', 'null', 'undefined'],
                                   clientCertEngine);
  }

  if (ticketKeys !== undefined && ticketKeys !== null) {
    validateBuffer(ticketKeys, `${name}.ticketKeys`);
    if (ticketKeys.byteLength !== 48) {
      throw new ERR_INVALID_ARG_VALUE(
        `${name}.ticketKeys`,
        ticketKeys.byteLength,
        'must be exactly 48 bytes');
    }
    context.setTicketKeys(ticketKeys);
  }

  if (sessionTimeout !== undefined && sessionTimeout !== null) {
    validateInt32(sessionTimeout, `${name}.sessionTimeout`, 0);
    context.setSessionTimeout(sessionTimeout);
  }
}

module.exports = {
  configSecureContext,
};
