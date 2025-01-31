'use strict';

// This tests early errors for invalid encodings.

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');

const {
  generateKeyPair,
  generateKeyPairSync,
} = require('crypto');
const { inspect } = require('util');

const { hasOpenSSL3 } = require('../common/crypto');

// Test invalid parameter encoding.
{
  assert.throws(() => generateKeyPairSync('ec', {
    namedCurve: 'P-256',
    paramEncoding: 'otherEncoding',
    publicKeyEncoding: {
      type: 'spki',
      format: 'pem'
    },
    privateKeyEncoding: {
      type: 'pkcs8',
      format: 'pem',
      cipher: 'aes-128-cbc',
      passphrase: 'top secret'
    }
  }), {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_VALUE',
    message: "The property 'options.paramEncoding' is invalid. " +
      "Received 'otherEncoding'"
  });
}

{
  // Test invalid key types.
  for (const type of [undefined, null, 0]) {
    assert.throws(() => generateKeyPairSync(type, {}), {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_TYPE',
      message: 'The "type" argument must be of type string.' +
               common.invalidArgTypeHelper(type)
    });
  }

  assert.throws(() => generateKeyPairSync('rsa2', {}), {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_VALUE',
    message: "The argument 'type' must be a supported key type. Received 'rsa2'"
  });
}

{
  // Test keygen without options object.
  assert.throws(() => generateKeyPair('rsa', common.mustNotCall()), {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "options" argument must be of type object. ' +
      'Received undefined'
  });

  // Even if no options are required, it should be impossible to pass anything
  // but an object (or undefined).
  assert.throws(() => generateKeyPair('ed448', 0, common.mustNotCall()), {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "options" argument must be of type object. ' +
      'Received type number (0)'
  });
}

{
  // Invalid publicKeyEncoding.
  for (const enc of [0, 'a', true]) {
    assert.throws(() => generateKeyPairSync('rsa', {
      modulusLength: 4096,
      publicKeyEncoding: enc,
      privateKeyEncoding: {
        type: 'pkcs1',
        format: 'pem'
      }
    }), {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_VALUE',
      message: "The property 'options.publicKeyEncoding' is invalid. " +
        `Received ${inspect(enc)}`
    });
  }

  // Missing publicKeyEncoding.type.
  for (const type of [undefined, null, 0, true, {}]) {
    assert.throws(() => generateKeyPairSync('rsa', {
      modulusLength: 4096,
      publicKeyEncoding: {
        type,
        format: 'pem'
      },
      privateKeyEncoding: {
        type: 'pkcs1',
        format: 'pem'
      }
    }), {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_VALUE',
      message: "The property 'options.publicKeyEncoding.type' is invalid. " +
        `Received ${inspect(type)}`
    });
  }

  // Missing / invalid publicKeyEncoding.format.
  for (const format of [undefined, null, 0, false, 'a', {}]) {
    assert.throws(() => generateKeyPairSync('rsa', {
      modulusLength: 4096,
      publicKeyEncoding: {
        type: 'pkcs1',
        format
      },
      privateKeyEncoding: {
        type: 'pkcs1',
        format: 'pem'
      }
    }), {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_VALUE',
      message: "The property 'options.publicKeyEncoding.format' is invalid. " +
        `Received ${inspect(format)}`
    });
  }

  // Invalid privateKeyEncoding.
  for (const enc of [0, 'a', true]) {
    assert.throws(() => generateKeyPairSync('rsa', {
      modulusLength: 4096,
      publicKeyEncoding: {
        type: 'pkcs1',
        format: 'pem'
      },
      privateKeyEncoding: enc
    }), {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_VALUE',
      message: "The property 'options.privateKeyEncoding' is invalid. " +
        `Received ${inspect(enc)}`
    });
  }

  // Missing / invalid privateKeyEncoding.type.
  for (const type of [undefined, null, 0, true, {}]) {
    assert.throws(() => generateKeyPairSync('rsa', {
      modulusLength: 4096,
      publicKeyEncoding: {
        type: 'pkcs1',
        format: 'pem'
      },
      privateKeyEncoding: {
        type,
        format: 'pem'
      }
    }), {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_VALUE',
      message: "The property 'options.privateKeyEncoding.type' is invalid. " +
        `Received ${inspect(type)}`
    });
  }

  // Missing / invalid privateKeyEncoding.format.
  for (const format of [undefined, null, 0, false, 'a', {}]) {
    assert.throws(() => generateKeyPairSync('rsa', {
      modulusLength: 4096,
      publicKeyEncoding: {
        type: 'pkcs1',
        format: 'pem'
      },
      privateKeyEncoding: {
        type: 'pkcs1',
        format
      }
    }), {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_VALUE',
      message: "The property 'options.privateKeyEncoding.format' is invalid. " +
        `Received ${inspect(format)}`
    });
  }

  // Cipher of invalid type.
  for (const cipher of [0, true, {}]) {
    assert.throws(() => generateKeyPairSync('rsa', {
      modulusLength: 4096,
      publicKeyEncoding: {
        type: 'pkcs1',
        format: 'pem'
      },
      privateKeyEncoding: {
        type: 'pkcs1',
        format: 'pem',
        cipher
      }
    }), {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_VALUE',
      message: "The property 'options.privateKeyEncoding.cipher' is invalid. " +
        `Received ${inspect(cipher)}`
    });
  }

  // Invalid cipher.
  assert.throws(() => generateKeyPairSync('rsa', {
    modulusLength: 4096,
    publicKeyEncoding: {
      type: 'pkcs1',
      format: 'pem'
    },
    privateKeyEncoding: {
      type: 'pkcs8',
      format: 'pem',
      cipher: 'foo',
      passphrase: 'secret'
    }
  }), {
    name: 'Error',
    code: 'ERR_CRYPTO_UNKNOWN_CIPHER',
    message: 'Unknown cipher'
  });

  // Cipher, but no valid passphrase.
  for (const passphrase of [undefined, null, 5, false, true]) {
    assert.throws(() => generateKeyPairSync('rsa', {
      modulusLength: 4096,
      publicKeyEncoding: {
        type: 'pkcs1',
        format: 'pem'
      },
      privateKeyEncoding: {
        type: 'pkcs8',
        format: 'pem',
        cipher: 'aes-128-cbc',
        passphrase
      }
    }), {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_VALUE',
      message: "The property 'options.privateKeyEncoding.passphrase' " +
        `is invalid. Received ${inspect(passphrase)}`
    });
  }

  // Test invalid callbacks.
  for (const cb of [undefined, null, 0, {}]) {
    assert.throws(() => generateKeyPair('rsa', {
      modulusLength: 512,
      publicKeyEncoding: { type: 'pkcs1', format: 'pem' },
      privateKeyEncoding: { type: 'pkcs1', format: 'pem' }
    }, cb), {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_TYPE'
    });
  }
}

// Test RSA parameters.
{
  // Test invalid modulus lengths. (non-number)
  for (const modulusLength of [undefined, null, 'a', true, {}, []]) {
    assert.throws(() => generateKeyPair('rsa', {
      modulusLength
    }, common.mustNotCall()), {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_TYPE',
      message:
        'The "options.modulusLength" property must be of type number.' +
        common.invalidArgTypeHelper(modulusLength)
    });
  }

  // Test invalid modulus lengths. (non-integer)
  for (const modulusLength of [512.1, 1.3, 1.1, 5000.9, 100.5]) {
    assert.throws(() => generateKeyPair('rsa', {
      modulusLength
    }, common.mustNotCall()), {
      name: 'RangeError',
      code: 'ERR_OUT_OF_RANGE',
      message:
        'The value of "options.modulusLength" is out of range. ' +
        'It must be an integer. ' +
        `Received ${inspect(modulusLength)}`
    });
  }

  // Test invalid modulus lengths. (out of range)
  for (const modulusLength of [-1, -9, 4294967297]) {
    assert.throws(() => generateKeyPair('rsa', {
      modulusLength
    }, common.mustNotCall()), {
      name: 'RangeError',
      code: 'ERR_OUT_OF_RANGE',
    });
  }

  // Test invalid exponents. (non-number)
  for (const publicExponent of ['a', true, {}, []]) {
    assert.throws(() => generateKeyPair('rsa', {
      modulusLength: 4096,
      publicExponent
    }, common.mustNotCall()), {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_TYPE',
      message:
        'The "options.publicExponent" property must be of type number.' +
        common.invalidArgTypeHelper(publicExponent)
    });
  }

  // Test invalid exponents. (non-integer)
  for (const publicExponent of [3.5, 1.1, 50.5, 510.5]) {
    assert.throws(() => generateKeyPair('rsa', {
      modulusLength: 4096,
      publicExponent
    }, common.mustNotCall()), {
      name: 'RangeError',
      code: 'ERR_OUT_OF_RANGE',
      message:
        'The value of "options.publicExponent" is out of range. ' +
        'It must be an integer. ' +
        `Received ${inspect(publicExponent)}`
    });
  }

  // Test invalid exponents. (out of range)
  for (const publicExponent of [-5, -3, 4294967297]) {
    assert.throws(() => generateKeyPair('rsa', {
      modulusLength: 4096,
      publicExponent
    }, common.mustNotCall()), {
      name: 'RangeError',
      code: 'ERR_OUT_OF_RANGE',
    });
  }

  // Test invalid exponents. (caught by OpenSSL)
  for (const publicExponent of [1, 1 + 0x10001]) {
    generateKeyPair('rsa', {
      modulusLength: 4096,
      publicExponent
    }, common.mustCall((err) => {
      assert.strictEqual(err.name, 'Error');
      assert.match(err.message, hasOpenSSL3 ? /exponent/ : /bad e value/);
    }));
  }
}

// Test DSA parameters.
{
  // Test invalid modulus lengths. (non-number)
  for (const modulusLength of [undefined, null, 'a', true, {}, []]) {
    assert.throws(() => generateKeyPair('dsa', {
      modulusLength
    }, common.mustNotCall()), {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_TYPE',
      message:
        'The "options.modulusLength" property must be of type number.' +
        common.invalidArgTypeHelper(modulusLength)
    });
  }

  // Test invalid modulus lengths. (non-integer)
  for (const modulusLength of [512.1, 1.3, 1.1, 5000.9, 100.5]) {
    assert.throws(() => generateKeyPair('dsa', {
      modulusLength
    }, common.mustNotCall()), {
      name: 'RangeError',
      code: 'ERR_OUT_OF_RANGE',
    });
  }

  // Test invalid modulus lengths. (out of range)
  for (const modulusLength of [-1, -9, 4294967297]) {
    assert.throws(() => generateKeyPair('dsa', {
      modulusLength
    }, common.mustNotCall()), {
      name: 'RangeError',
      code: 'ERR_OUT_OF_RANGE',
    });
  }

  // Test invalid divisor lengths. (non-number)
  for (const divisorLength of ['a', true, {}, []]) {
    assert.throws(() => generateKeyPair('dsa', {
      modulusLength: 2048,
      divisorLength
    }, common.mustNotCall()), {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_TYPE',
      message:
        'The "options.divisorLength" property must be of type number.' +
        common.invalidArgTypeHelper(divisorLength)
    });
  }

  // Test invalid divisor lengths. (non-integer)
  for (const divisorLength of [4096.1, 5.1, 6.9, 9.5]) {
    assert.throws(() => generateKeyPair('dsa', {
      modulusLength: 2048,
      divisorLength
    }, common.mustNotCall()), {
      name: 'RangeError',
      code: 'ERR_OUT_OF_RANGE',
      message:
        'The value of "options.divisorLength" is out of range. ' +
        'It must be an integer. ' +
        `Received ${inspect(divisorLength)}`
    });
  }

  // Test invalid divisor lengths. (out of range)
  for (const divisorLength of [-1, -6, -9, 2147483648]) {
    assert.throws(() => generateKeyPair('dsa', {
      modulusLength: 2048,
      divisorLength
    }, common.mustNotCall()), {
      name: 'RangeError',
      code: 'ERR_OUT_OF_RANGE',
      message:
        'The value of "options.divisorLength" is out of range. ' +
        'It must be >= 0 && <= 2147483647. ' +
        `Received ${inspect(divisorLength)}`
    });
  }
}

// Test EC parameters.
{
  // Test invalid curves.
  assert.throws(() => {
    generateKeyPairSync('ec', {
      namedCurve: 'abcdef',
      publicKeyEncoding: { type: 'spki', format: 'pem' },
      privateKeyEncoding: { type: 'sec1', format: 'pem' }
    });
  }, {
    name: 'TypeError',
    message: 'Invalid EC curve name'
  });

  // Test error type when curve is not a string
  for (const namedCurve of [true, {}, [], 123]) {
    assert.throws(() => {
      generateKeyPairSync('ec', {
        namedCurve,
        publicKeyEncoding: { type: 'spki', format: 'pem' },
        privateKeyEncoding: { type: 'sec1', format: 'pem' }
      });
    }, {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_TYPE',
      message:
        'The "options.namedCurve" property must be of type string.' +
        common.invalidArgTypeHelper(namedCurve)
    });
  }

  // It should recognize both NIST and standard curve names.
  generateKeyPair('ec', {
    namedCurve: 'P-256',
  }, common.mustSucceed((publicKey, privateKey) => {
    assert.deepStrictEqual(publicKey.asymmetricKeyDetails, {
      namedCurve: 'prime256v1'
    });
    assert.deepStrictEqual(privateKey.asymmetricKeyDetails, {
      namedCurve: 'prime256v1'
    });
  }));

  generateKeyPair('ec', {
    namedCurve: 'secp256k1',
  }, common.mustSucceed((publicKey, privateKey) => {
    assert.deepStrictEqual(publicKey.asymmetricKeyDetails, {
      namedCurve: 'secp256k1'
    });
    assert.deepStrictEqual(privateKey.asymmetricKeyDetails, {
      namedCurve: 'secp256k1'
    });
  }));
}

{
  assert.throws(() => {
    generateKeyPair('dh', common.mustNotCall());
  }, {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "options" argument must be of type object. Received undefined'
  });

  assert.throws(() => {
    generateKeyPair('dh', {}, common.mustNotCall());
  }, {
    name: 'TypeError',
    code: 'ERR_MISSING_OPTION',
    message: 'At least one of the group, prime, or primeLength options is ' +
             'required'
  });

  assert.throws(() => {
    generateKeyPair('dh', {
      group: 'modp0'
    }, common.mustNotCall());
  }, {
    name: 'Error',
    code: 'ERR_CRYPTO_UNKNOWN_DH_GROUP',
    message: 'Unknown DH group'
  });

  assert.throws(() => {
    generateKeyPair('dh', {
      primeLength: 2147483648
    }, common.mustNotCall());
  }, {
    name: 'RangeError',
    code: 'ERR_OUT_OF_RANGE',
    message: 'The value of "options.primeLength" is out of range. ' +
             'It must be >= 0 && <= 2147483647. ' +
             'Received 2147483648',
  });

  assert.throws(() => {
    generateKeyPair('dh', {
      primeLength: -1
    }, common.mustNotCall());
  }, {
    name: 'RangeError',
    code: 'ERR_OUT_OF_RANGE',
    message: 'The value of "options.primeLength" is out of range. ' +
             'It must be >= 0 && <= 2147483647. ' +
             'Received -1',
  });

  assert.throws(() => {
    generateKeyPair('dh', {
      primeLength: 2,
      generator: 2147483648,
    }, common.mustNotCall());
  }, {
    name: 'RangeError',
    code: 'ERR_OUT_OF_RANGE',
    message: 'The value of "options.generator" is out of range. ' +
             'It must be >= 0 && <= 2147483647. ' +
             'Received 2147483648',
  });

  assert.throws(() => {
    generateKeyPair('dh', {
      primeLength: 2,
      generator: -1,
    }, common.mustNotCall());
  }, {
    name: 'RangeError',
    code: 'ERR_OUT_OF_RANGE',
    message: 'The value of "options.generator" is out of range. ' +
             'It must be >= 0 && <= 2147483647. ' +
             'Received -1',
  });

  // Test incompatible options.
  const allOpts = {
    group: 'modp5',
    prime: Buffer.alloc(0),
    primeLength: 1024,
    generator: 2
  };
  const incompatible = [
    ['group', 'prime'],
    ['group', 'primeLength'],
    ['group', 'generator'],
    ['prime', 'primeLength'],
  ];
  for (const [opt1, opt2] of incompatible) {
    assert.throws(() => {
      generateKeyPairSync('dh', {
        [opt1]: allOpts[opt1],
        [opt2]: allOpts[opt2]
      });
    }, {
      name: 'TypeError',
      code: 'ERR_INCOMPATIBLE_OPTION_PAIR',
      message: `Option "${opt1}" cannot be used in combination with option ` +
               `"${opt2}"`
    });
  }
}

// Test invalid key encoding types.
{
  // Invalid public key type.
  for (const type of ['foo', 'pkcs8', 'sec1']) {
    assert.throws(() => {
      generateKeyPairSync('rsa', {
        modulusLength: 4096,
        publicKeyEncoding: { type, format: 'pem' },
        privateKeyEncoding: { type: 'pkcs8', format: 'pem' }
      });
    }, {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_VALUE',
      message: "The property 'options.publicKeyEncoding.type' is invalid. " +
        `Received ${inspect(type)}`
    });
  }

  // Invalid hash value.
  for (const hashValue of [123, true, {}, []]) {
    assert.throws(() => {
      generateKeyPairSync('rsa-pss', {
        modulusLength: 4096,
        hashAlgorithm: hashValue
      });
    }, {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_TYPE',
      message:
      'The "options.hashAlgorithm" property must be of type string.' +
        common.invalidArgTypeHelper(hashValue)
    });
  }

  // too long salt length
  assert.throws(() => {
    generateKeyPair('rsa-pss', {
      modulusLength: 512,
      saltLength: 2147483648,
      hashAlgorithm: 'sha256',
      mgf1HashAlgorithm: 'sha256'
    }, common.mustNotCall());
  }, {
    name: 'RangeError',
    code: 'ERR_OUT_OF_RANGE',
    message: 'The value of "options.saltLength" is out of range. ' +
             'It must be >= 0 && <= 2147483647. ' +
             'Received 2147483648'
  });

  assert.throws(() => {
    generateKeyPair('rsa-pss', {
      modulusLength: 512,
      saltLength: -1,
      hashAlgorithm: 'sha256',
      mgf1HashAlgorithm: 'sha256'
    }, common.mustNotCall());
  }, {
    name: 'RangeError',
    code: 'ERR_OUT_OF_RANGE',
    message: 'The value of "options.saltLength" is out of range. ' +
             'It must be >= 0 && <= 2147483647. ' +
             'Received -1'
  });

  // Invalid private key type.
  for (const type of ['foo', 'spki']) {
    assert.throws(() => {
      generateKeyPairSync('rsa', {
        modulusLength: 4096,
        publicKeyEncoding: { type: 'spki', format: 'pem' },
        privateKeyEncoding: { type, format: 'pem' }
      });
    }, {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_VALUE',
      message: "The property 'options.privateKeyEncoding.type' is invalid. " +
        `Received ${inspect(type)}`
    });
  }

  // Key encoding doesn't match key type.
  for (const type of ['dsa', 'ec']) {
    assert.throws(() => {
      generateKeyPairSync(type, {
        modulusLength: 4096,
        namedCurve: 'P-256',
        publicKeyEncoding: { type: 'pkcs1', format: 'pem' },
        privateKeyEncoding: { type: 'pkcs8', format: 'pem' }
      });
    }, {
      name: 'Error',
      code: 'ERR_CRYPTO_INCOMPATIBLE_KEY_OPTIONS',
      message: 'The selected key encoding pkcs1 can only be used for RSA keys.'
    });

    assert.throws(() => {
      generateKeyPairSync(type, {
        modulusLength: 4096,
        namedCurve: 'P-256',
        publicKeyEncoding: { type: 'spki', format: 'pem' },
        privateKeyEncoding: { type: 'pkcs1', format: 'pem' }
      });
    }, {
      name: 'Error',
      code: 'ERR_CRYPTO_INCOMPATIBLE_KEY_OPTIONS',
      message: 'The selected key encoding pkcs1 can only be used for RSA keys.'
    });
  }

  for (const type of ['rsa', 'dsa']) {
    assert.throws(() => {
      generateKeyPairSync(type, {
        modulusLength: 4096,
        publicKeyEncoding: { type: 'spki', format: 'pem' },
        privateKeyEncoding: { type: 'sec1', format: 'pem' }
      });
    }, {
      name: 'Error',
      code: 'ERR_CRYPTO_INCOMPATIBLE_KEY_OPTIONS',
      message: 'The selected key encoding sec1 can only be used for EC keys.'
    });
  }

  // Attempting to encrypt a DER-encoded, non-PKCS#8 key.
  for (const type of ['pkcs1', 'sec1']) {
    assert.throws(() => {
      generateKeyPairSync(type === 'pkcs1' ? 'rsa' : 'ec', {
        modulusLength: 4096,
        namedCurve: 'P-256',
        publicKeyEncoding: { type: 'spki', format: 'pem' },
        privateKeyEncoding: {
          type,
          format: 'der',
          cipher: 'aes-128-cbc',
          passphrase: 'hello'
        }
      });
    }, {
      name: 'Error',
      code: 'ERR_CRYPTO_INCOMPATIBLE_KEY_OPTIONS',
      message: `The selected key encoding ${type} does not support encryption.`
    });
  }
}

{
  // Test RSA-PSS.
  assert.throws(
    () => {
      generateKeyPair('rsa-pss', {
        modulusLength: 512,
        saltLength: 16,
        hashAlgorithm: 'sha256',
        mgf1HashAlgorithm: undefined
      });
    },
    {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_TYPE',
    }
  );

  for (const mgf1HashAlgorithm of [null, 0, false, {}, []]) {
    assert.throws(
      () => {
        generateKeyPair('rsa-pss', {
          modulusLength: 512,
          saltLength: 16,
          hashAlgorithm: 'sha256',
          mgf1HashAlgorithm
        }, common.mustNotCall());
      },
      {
        name: 'TypeError',
        code: 'ERR_INVALID_ARG_TYPE',
        message:
          'The "options.mgf1HashAlgorithm" property must be of type string.' +
          common.invalidArgTypeHelper(mgf1HashAlgorithm)

      }
    );
  }

  assert.throws(() => generateKeyPair('rsa-pss', {
    modulusLength: 512,
    hashAlgorithm: 'sha2',
  }, common.mustNotCall()), {
    name: 'TypeError',
    code: 'ERR_CRYPTO_INVALID_DIGEST',
    message: 'Invalid digest: sha2'
  });

  assert.throws(() => generateKeyPair('rsa-pss', {
    modulusLength: 512,
    mgf1HashAlgorithm: 'sha2',
  }, common.mustNotCall()), {
    name: 'TypeError',
    code: 'ERR_CRYPTO_INVALID_DIGEST',
    message: 'Invalid MGF1 digest: sha2'
  });
}

{
  // This test makes sure deprecated and new options must
  // be the same value.

  assert.throws(() => generateKeyPair('rsa-pss', {
    modulusLength: 512,
    saltLength: 16,
    mgf1Hash: 'sha256',
    mgf1HashAlgorithm: 'sha1'
  }, common.mustNotCall()), { code: 'ERR_INVALID_ARG_VALUE' });

  assert.throws(() => generateKeyPair('rsa-pss', {
    modulusLength: 512,
    saltLength: 16,
    hash: 'sha256',
    hashAlgorithm: 'sha1'
  }, common.mustNotCall()), { code: 'ERR_INVALID_ARG_VALUE' });
}
