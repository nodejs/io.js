'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const fs = require('fs');
const exec = require('child_process').exec;
const crypto = require('crypto');
const fixtures = require('../common/fixtures');
const {
  hasOpenSSL3,
  opensslCli,
} = require('../common/crypto');

// Test certificates
const certPem = fixtures.readKey('rsa_cert.crt');
const keyPem = fixtures.readKey('rsa_private.pem');
const keySize = 2048;

{
  const Sign = crypto.Sign;
  const instance = Sign('SHA256');
  assert(instance instanceof Sign, 'Sign is expected to return a new ' +
                                   'instance when called without `new`');
}

{
  const Verify = crypto.Verify;
  const instance = Verify('SHA256');
  assert(instance instanceof Verify, 'Verify is expected to return a new ' +
                                     'instance when called without `new`');
}

// Test handling of exceptional conditions
{
  const library = {
    configurable: true,
    set() {
      throw new Error('bye, bye, library');
    }
  };
  Object.defineProperty(Object.prototype, 'library', library);

  assert.throws(() => {
    crypto.createSign('sha1').sign(
      `-----BEGIN RSA PRIVATE KEY-----
      AAAAAAAAAAAA
      -----END RSA PRIVATE KEY-----`);
  }, { message: 'bye, bye, library' });

  delete Object.prototype.library;

  const errorStack = {
    configurable: true,
    set() {
      throw new Error('bye, bye, error stack');
    }
  };
  Object.defineProperty(Object.prototype, 'opensslErrorStack', errorStack);

  assert.throws(() => {
    crypto.createSign('SHA1')
      .update('Test123')
      .sign({
        key: keyPem,
        padding: crypto.constants.RSA_PKCS1_OAEP_PADDING
      });
  }, { message: hasOpenSSL3 ?
    'error:1C8000A5:Provider routines::illegal or unsupported padding mode' :
    'bye, bye, error stack' });

  delete Object.prototype.opensslErrorStack;
}

assert.throws(
  () => crypto.createVerify('SHA256').verify({
    key: certPem,
    padding: null,
  }, ''),
  {
    code: 'ERR_INVALID_ARG_VALUE',
    name: 'TypeError',
    message: "The property 'options.padding' is invalid. Received null",
  });

assert.throws(
  () => crypto.createVerify('SHA256').verify({
    key: certPem,
    saltLength: null,
  }, ''),
  {
    code: 'ERR_INVALID_ARG_VALUE',
    name: 'TypeError',
    message: "The property 'options.saltLength' is invalid. Received null",
  });

// Test signing and verifying
{
  const s1 = crypto.createSign('SHA1')
                   .update('Test123')
                   .sign(keyPem, 'base64');
  let s1stream = crypto.createSign('SHA1');
  s1stream.end('Test123');
  s1stream = s1stream.sign(keyPem, 'base64');
  assert.strictEqual(s1, s1stream, `${s1} should equal ${s1stream}`);

  const verified = crypto.createVerify('SHA1')
                         .update('Test')
                         .update('123')
                         .verify(certPem, s1, 'base64');
  assert.strictEqual(verified, true);
}

{
  const s2 = crypto.createSign('SHA256')
                   .update('Test123')
                   .sign(keyPem, 'latin1');
  let s2stream = crypto.createSign('SHA256');
  s2stream.end('Test123');
  s2stream = s2stream.sign(keyPem, 'latin1');
  assert.strictEqual(s2, s2stream, `${s2} should equal ${s2stream}`);

  let verified = crypto.createVerify('SHA256')
                       .update('Test')
                       .update('123')
                       .verify(certPem, s2, 'latin1');
  assert.strictEqual(verified, true);

  const verStream = crypto.createVerify('SHA256');
  verStream.write('Tes');
  verStream.write('t12');
  verStream.end('3');
  verified = verStream.verify(certPem, s2, 'latin1');
  assert.strictEqual(verified, true);
}

{
  const s3 = crypto.createSign('SHA1')
                   .update('Test123')
                   .sign(keyPem, 'buffer');
  let verified = crypto.createVerify('SHA1')
                       .update('Test')
                       .update('123')
                       .verify(certPem, s3);
  assert.strictEqual(verified, true);

  const verStream = crypto.createVerify('SHA1');
  verStream.write('Tes');
  verStream.write('t12');
  verStream.end('3');
  verified = verStream.verify(certPem, s3);
  assert.strictEqual(verified, true);
}

// Special tests for RSA_PKCS1_PSS_PADDING
{
  function testPSS(algo, hLen) {
    // Maximum permissible salt length
    const max = keySize / 8 - hLen - 2;

    function getEffectiveSaltLength(saltLength) {
      switch (saltLength) {
        case crypto.constants.RSA_PSS_SALTLEN_DIGEST:
          return hLen;
        case crypto.constants.RSA_PSS_SALTLEN_MAX_SIGN:
          return max;
        default:
          return saltLength;
      }
    }

    const signSaltLengths = [
      crypto.constants.RSA_PSS_SALTLEN_DIGEST,
      getEffectiveSaltLength(crypto.constants.RSA_PSS_SALTLEN_DIGEST),
      crypto.constants.RSA_PSS_SALTLEN_MAX_SIGN,
      getEffectiveSaltLength(crypto.constants.RSA_PSS_SALTLEN_MAX_SIGN),
      0, 16, 32, 64, 128,
    ];

    const verifySaltLengths = [
      crypto.constants.RSA_PSS_SALTLEN_DIGEST,
      getEffectiveSaltLength(crypto.constants.RSA_PSS_SALTLEN_DIGEST),
      getEffectiveSaltLength(crypto.constants.RSA_PSS_SALTLEN_MAX_SIGN),
      0, 16, 32, 64, 128,
    ];
    const errMessage = /^Error:.*data too large for key size$/;

    const data = Buffer.from('Test123');

    signSaltLengths.forEach((signSaltLength) => {
      if (signSaltLength > max) {
        // If the salt length is too big, an Error should be thrown
        assert.throws(() => {
          crypto.createSign(algo)
            .update(data)
            .sign({
              key: keyPem,
              padding: crypto.constants.RSA_PKCS1_PSS_PADDING,
              saltLength: signSaltLength
            });
        }, errMessage);
        assert.throws(() => {
          crypto.sign(algo, data, {
            key: keyPem,
            padding: crypto.constants.RSA_PKCS1_PSS_PADDING,
            saltLength: signSaltLength
          });
        }, errMessage);
      } else {
        // Otherwise, a valid signature should be generated
        const s4 = crypto.createSign(algo)
                         .update(data)
                         .sign({
                           key: keyPem,
                           padding: crypto.constants.RSA_PKCS1_PSS_PADDING,
                           saltLength: signSaltLength
                         });
        const s4_2 = crypto.sign(algo, data, {
          key: keyPem,
          padding: crypto.constants.RSA_PKCS1_PSS_PADDING,
          saltLength: signSaltLength
        });

        [s4, s4_2].forEach((sig) => {
          let verified;
          verifySaltLengths.forEach((verifySaltLength) => {
            // Verification should succeed if and only if the salt length is
            // correct
            verified = crypto.createVerify(algo)
                             .update(data)
                             .verify({
                               key: certPem,
                               padding: crypto.constants.RSA_PKCS1_PSS_PADDING,
                               saltLength: verifySaltLength
                             }, sig);
            assert.strictEqual(verified, crypto.verify(algo, data, {
              key: certPem,
              padding: crypto.constants.RSA_PKCS1_PSS_PADDING,
              saltLength: verifySaltLength
            }, sig));
            const saltLengthCorrect = getEffectiveSaltLength(signSaltLength) ===
                                      getEffectiveSaltLength(verifySaltLength);
            assert.strictEqual(verified, saltLengthCorrect);
          });

          // Verification using RSA_PSS_SALTLEN_AUTO should always work
          verified = crypto.createVerify(algo)
                           .update(data)
                           .verify({
                             key: certPem,
                             padding: crypto.constants.RSA_PKCS1_PSS_PADDING,
                             saltLength: crypto.constants.RSA_PSS_SALTLEN_AUTO
                           }, sig);
          assert.strictEqual(verified, true);
          assert.strictEqual(verified, crypto.verify(algo, data, {
            key: certPem,
            padding: crypto.constants.RSA_PKCS1_PSS_PADDING,
            saltLength: crypto.constants.RSA_PSS_SALTLEN_AUTO
          }, sig));

          // Verifying an incorrect message should never work
          const wrongData = Buffer.from('Test1234');
          verified = crypto.createVerify(algo)
                           .update(wrongData)
                           .verify({
                             key: certPem,
                             padding: crypto.constants.RSA_PKCS1_PSS_PADDING,
                             saltLength: crypto.constants.RSA_PSS_SALTLEN_AUTO
                           }, sig);
          assert.strictEqual(verified, false);
          assert.strictEqual(verified, crypto.verify(algo, wrongData, {
            key: certPem,
            padding: crypto.constants.RSA_PKCS1_PSS_PADDING,
            saltLength: crypto.constants.RSA_PSS_SALTLEN_AUTO
          }, sig));
        });
      }
    });
  }

  testPSS('SHA1', 20);
  testPSS('SHA256', 32);
}

// Test vectors for RSA_PKCS1_PSS_PADDING provided by the RSA Laboratories:
// https://www.emc.com/emc-plus/rsa-labs/standards-initiatives/pkcs-rsa-cryptography-standard.htm
{
  // We only test verification as we cannot specify explicit salts when signing
  function testVerify(cert, vector) {
    const verified = crypto.createVerify('SHA1')
                          .update(Buffer.from(vector.message, 'hex'))
                          .verify({
                            key: cert,
                            padding: crypto.constants.RSA_PKCS1_PSS_PADDING,
                            saltLength: vector.salt.length / 2
                          }, vector.signature, 'hex');
    assert.strictEqual(verified, true);
  }

  const examples = JSON.parse(fixtures.readSync('pss-vectors.json', 'utf8'));

  for (const key in examples) {
    const example = examples[key];
    const publicKey = example.publicKey.join('\n');
    example.tests.forEach((test) => testVerify(publicKey, test));
  }
}

// Test exceptions for invalid `padding` and `saltLength` values
{
  [null, NaN, 'boom', {}, [], true, false]
    .forEach((invalidValue) => {
      assert.throws(() => {
        crypto.createSign('SHA256')
          .update('Test123')
          .sign({
            key: keyPem,
            padding: invalidValue
          });
      }, {
        code: 'ERR_INVALID_ARG_VALUE',
        name: 'TypeError'
      });

      assert.throws(() => {
        crypto.createSign('SHA256')
          .update('Test123')
          .sign({
            key: keyPem,
            padding: crypto.constants.RSA_PKCS1_PSS_PADDING,
            saltLength: invalidValue
          });
      }, {
        code: 'ERR_INVALID_ARG_VALUE',
        name: 'TypeError'
      });
    });

  assert.throws(() => {
    crypto.createSign('SHA1')
      .update('Test123')
      .sign({
        key: keyPem,
        padding: crypto.constants.RSA_PKCS1_OAEP_PADDING
      });
  }, hasOpenSSL3 ? {
    code: 'ERR_OSSL_ILLEGAL_OR_UNSUPPORTED_PADDING_MODE',
    message: /illegal or unsupported padding mode/,
  } : {
    code: 'ERR_OSSL_RSA_ILLEGAL_OR_UNSUPPORTED_PADDING_MODE',
    message: /illegal or unsupported padding mode/,
    opensslErrorStack: [
      'error:06089093:digital envelope routines:EVP_PKEY_CTX_ctrl:' +
      'command not supported',
    ],
  });
}

// Test throws exception when key options is null
{
  assert.throws(() => {
    crypto.createSign('SHA1').update('Test123').sign(null, 'base64');
  }, {
    code: 'ERR_CRYPTO_SIGN_KEY_REQUIRED',
    name: 'Error'
  });
}

{
  const sign = crypto.createSign('SHA1');
  const verify = crypto.createVerify('SHA1');

  [1, [], {}, undefined, null, true, Infinity].forEach((input) => {
    const errObj = {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: 'The "algorithm" argument must be of type string.' +
               `${common.invalidArgTypeHelper(input)}`
    };
    assert.throws(() => crypto.createSign(input), errObj);
    assert.throws(() => crypto.createVerify(input), errObj);

    errObj.message = 'The "data" argument must be of type string or an ' +
                     'instance of Buffer, TypedArray, or DataView.' +
                     common.invalidArgTypeHelper(input);
    assert.throws(() => sign.update(input), errObj);
    assert.throws(() => verify.update(input), errObj);
    assert.throws(() => sign._write(input, 'utf8', () => {}), errObj);
    assert.throws(() => verify._write(input, 'utf8', () => {}), errObj);
  });

  [
    Uint8Array, Uint16Array, Uint32Array, Float32Array, Float64Array,
  ].forEach((clazz) => {
    // These should all just work
    sign.update(new clazz());
    verify.update(new clazz());
  });

  [1, {}, [], Infinity].forEach((input) => {
    const errObj = {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
    };
    assert.throws(() => sign.sign(input), errObj);
    assert.throws(() => verify.verify(input), errObj);
    assert.throws(() => verify.verify('test', input), errObj);
  });
}

{
  assert.throws(
    () => crypto.createSign('sha8'),
    /Invalid digest/);
  assert.throws(
    () => crypto.sign('sha8', Buffer.alloc(1), keyPem),
    /Invalid digest/);
}

[
  { private: fixtures.readKey('ed25519_private.pem', 'ascii'),
    public: fixtures.readKey('ed25519_public.pem', 'ascii'),
    algo: null,
    sigLen: 64 },
  { private: fixtures.readKey('ed448_private.pem', 'ascii'),
    public: fixtures.readKey('ed448_public.pem', 'ascii'),
    algo: null,
    sigLen: 114 },
  { private: fixtures.readKey('rsa_private_2048.pem', 'ascii'),
    public: fixtures.readKey('rsa_public_2048.pem', 'ascii'),
    algo: 'sha1',
    sigLen: 256 },
].forEach((pair) => {
  const algo = pair.algo;

  {
    const data = Buffer.from('Hello world');
    const sig = crypto.sign(algo, data, pair.private);
    assert.strictEqual(sig.length, pair.sigLen);

    assert.strictEqual(crypto.verify(algo, data, pair.private, sig),
                       true);
    assert.strictEqual(crypto.verify(algo, data, pair.public, sig),
                       true);
  }

  {
    const data = Buffer.from('Hello world');
    const privKeyObj = crypto.createPrivateKey(pair.private);
    const pubKeyObj = crypto.createPublicKey(pair.public);

    const sig = crypto.sign(algo, data, privKeyObj);
    assert.strictEqual(sig.length, pair.sigLen);

    assert.strictEqual(crypto.verify(algo, data, privKeyObj, sig), true);
    assert.strictEqual(crypto.verify(algo, data, pubKeyObj, sig), true);
  }

  {
    const data = Buffer.from('Hello world');
    const otherData = Buffer.from('Goodbye world');
    const otherSig = crypto.sign(algo, otherData, pair.private);
    assert.strictEqual(crypto.verify(algo, data, pair.private, otherSig),
                       false);
  }

  [
    Uint8Array, Uint16Array, Uint32Array, Float32Array, Float64Array,
  ].forEach((clazz) => {
    const data = new clazz();
    const sig = crypto.sign(algo, data, pair.private);
    assert.strictEqual(crypto.verify(algo, data, pair.private, sig),
                       true);
  });
});

[1, {}, [], true, Infinity].forEach((input) => {
  const data = Buffer.alloc(1);
  const sig = Buffer.alloc(1);
  const errObj = {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
  };

  assert.throws(() => crypto.sign(null, input, 'asdf'), errObj);
  assert.throws(() => crypto.verify(null, input, 'asdf', sig), errObj);

  assert.throws(() => crypto.sign(null, data, input), errObj);
  assert.throws(() => crypto.verify(null, data, input, sig), errObj);

  errObj.message = 'The "signature" argument must be an instance of ' +
                   'Buffer, TypedArray, or DataView.' +
                   common.invalidArgTypeHelper(input);
  assert.throws(() => crypto.verify(null, data, 'test', input), errObj);
});

{
  const data = Buffer.from('Hello world');
  const keys = [['ec-key.pem', 64], ['dsa_private_1025.pem', 40]];

  for (const [file, length] of keys) {
    const privKey = fixtures.readKey(file);
    [
      crypto.createSign('sha1').update(data).sign(privKey),
      crypto.sign('sha1', data, privKey),
      crypto.sign('sha1', data, { key: privKey, dsaEncoding: 'der' }),
    ].forEach((sig) => {
      // Signature length variability due to DER encoding
      assert(sig.length >= length + 4 && sig.length <= length + 8);

      assert.strictEqual(
        crypto.createVerify('sha1').update(data).verify(privKey, sig),
        true
      );
      assert.strictEqual(crypto.verify('sha1', data, privKey, sig), true);
    });

    // Test (EC)DSA signature conversion.
    const opts = { key: privKey, dsaEncoding: 'ieee-p1363' };
    let sig = crypto.sign('sha1', data, opts);
    // Unlike DER signatures, IEEE P1363 signatures have a predictable length.
    assert.strictEqual(sig.length, length);
    assert.strictEqual(crypto.verify('sha1', data, opts, sig), true);
    assert.strictEqual(crypto.createVerify('sha1')
                             .update(data)
                             .verify(opts, sig), true);

    // Test invalid signature lengths.
    for (const i of [-2, -1, 1, 2, 4, 8]) {
      sig = crypto.randomBytes(length + i);
      let result;
      try {
        result = crypto.verify('sha1', data, opts, sig);
      } catch (err) {
        assert.match(err.message, /asn1 encoding/);
        assert.strictEqual(err.library, 'asn1 encoding routines');
        continue;
      }
      assert.strictEqual(result, false);
    }
  }

  // Test verifying externally signed messages.
  const extSig = Buffer.from('494c18ab5c8a62a72aea5041966902bcfa229821af2bf65' +
                             '0b5b4870d1fe6aebeaed9460c62210693b5b0a300033823' +
                             '33d9529c8abd8c5948940af944828be16c', 'hex');
  for (const ok of [true, false]) {
    assert.strictEqual(
      crypto.verify('sha256', data, {
        key: fixtures.readKey('ec-key.pem'),
        dsaEncoding: 'ieee-p1363'
      }, extSig),
      ok
    );

    assert.strictEqual(
      crypto.createVerify('sha256').update(data).verify({
        key: fixtures.readKey('ec-key.pem'),
        dsaEncoding: 'ieee-p1363'
      }, extSig),
      ok
    );

    extSig[Math.floor(Math.random() * extSig.length)] ^= 1;
  }

  // Non-(EC)DSA keys should ignore the option.
  const sig = crypto.sign('sha1', data, {
    key: keyPem,
    dsaEncoding: 'ieee-p1363'
  });
  assert.strictEqual(crypto.verify('sha1', data, certPem, sig), true);
  assert.strictEqual(
    crypto.verify('sha1', data, {
      key: certPem,
      dsaEncoding: 'ieee-p1363'
    }, sig),
    true
  );
  assert.strictEqual(
    crypto.verify('sha1', data, {
      key: certPem,
      dsaEncoding: 'der'
    }, sig),
    true
  );

  for (const dsaEncoding of ['foo', null, {}, 5, true, NaN]) {
    assert.throws(() => {
      crypto.sign('sha1', data, {
        key: certPem,
        dsaEncoding
      });
    }, {
      code: 'ERR_INVALID_ARG_VALUE'
    });
  }
}


// RSA-PSS Sign test by verifying with 'openssl dgst -verify'
// Note: this particular test *must* be the last in this file as it will exit
// early if no openssl binary is found
{
  if (!opensslCli) {
    common.skip('node compiled without OpenSSL CLI.');
  }

  const pubfile = fixtures.path('keys', 'rsa_public_2048.pem');
  const privkey = fixtures.readKey('rsa_private_2048.pem');

  const msg = 'Test123';
  const s5 = crypto.createSign('SHA256')
    .update(msg)
    .sign({
      key: privkey,
      padding: crypto.constants.RSA_PKCS1_PSS_PADDING
    });

  const tmpdir = require('../common/tmpdir');
  tmpdir.refresh();

  const sigfile = tmpdir.resolve('s5.sig');
  fs.writeFileSync(sigfile, s5);
  const msgfile = tmpdir.resolve('s5.msg');
  fs.writeFileSync(msgfile, msg);

  exec(...common.escapePOSIXShell`"${
    opensslCli}" dgst -sha256 -verify "${pubfile}" -signature "${
    sigfile}" -sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:-2 "${msgfile
  }"`, common.mustCall((err, stdout, stderr) => {
    assert(stdout.includes('Verified OK'));
  }));
}

{
  // Test RSA-PSS.
  {
    // This key pair does not restrict the message digest algorithm or salt
    // length.
    const publicPem = fixtures.readKey('rsa_pss_public_2048.pem');
    const privatePem = fixtures.readKey('rsa_pss_private_2048.pem');

    const publicKey = crypto.createPublicKey(publicPem);
    const privateKey = crypto.createPrivateKey(privatePem);

    for (const key of [privatePem, privateKey]) {
      // Any algorithm should work.
      for (const algo of ['sha1', 'sha256']) {
        // Any salt length should work.
        for (const saltLength of [undefined, 8, 10, 12, 16, 18, 20]) {
          const signature = crypto.sign(algo, 'foo', { key, saltLength });

          for (const pkey of [key, publicKey, publicPem]) {
            const okay = crypto.verify(
              algo,
              'foo',
              { key: pkey, saltLength },
              signature
            );

            assert.ok(okay);
          }
        }
      }
    }
  }

  {
    // This key pair enforces sha256 as the message digest and the MGF1
    // message digest and a salt length of at least 16 bytes.
    const publicPem =
      fixtures.readKey('rsa_pss_public_2048_sha256_sha256_16.pem');
    const privatePem =
      fixtures.readKey('rsa_pss_private_2048_sha256_sha256_16.pem');

    const publicKey = crypto.createPublicKey(publicPem);
    const privateKey = crypto.createPrivateKey(privatePem);

    for (const key of [privatePem, privateKey]) {
      // Signing with anything other than sha256 should fail.
      assert.throws(() => {
        crypto.sign('sha1', 'foo', key);
      }, /digest not allowed/);

      // Signing with salt lengths less than 16 bytes should fail.
      for (const saltLength of [8, 10, 12]) {
        assert.throws(() => {
          crypto.sign('sha256', 'foo', { key, saltLength });
        }, /pss saltlen too small/);
      }

      // Signing with sha256 and appropriate salt lengths should work.
      for (const saltLength of [undefined, 16, 18, 20]) {
        const signature = crypto.sign('sha256', 'foo', { key, saltLength });

        for (const pkey of [key, publicKey, publicPem]) {
          const okay = crypto.verify(
            'sha256',
            'foo',
            { key: pkey, saltLength },
            signature
          );

          assert.ok(okay);
        }
      }
    }
  }

  {
    // This key enforces sha512 as the message digest and sha256 as the MGF1
    // message digest.
    const publicPem =
      fixtures.readKey('rsa_pss_public_2048_sha512_sha256_20.pem');
    const privatePem =
      fixtures.readKey('rsa_pss_private_2048_sha512_sha256_20.pem');

    const publicKey = crypto.createPublicKey(publicPem);
    const privateKey = crypto.createPrivateKey(privatePem);

    // Node.js usually uses the same hash function for the message and for MGF1.
    // However, when a different MGF1 message digest algorithm has been
    // specified as part of the key, it should automatically switch to that.
    // This behavior is required by sections 3.1 and 3.3 of RFC4055.
    for (const key of [privatePem, privateKey]) {
      // sha256 matches the MGF1 hash function and should be used internally,
      // but it should not be permitted as the main message digest algorithm.
      for (const algo of ['sha1', 'sha256']) {
        assert.throws(() => {
          crypto.sign(algo, 'foo', key);
        }, /digest not allowed/);
      }

      // sha512 should produce a valid signature.
      const signature = crypto.sign('sha512', 'foo', key);

      for (const pkey of [key, publicKey, publicPem]) {
        const okay = crypto.verify('sha512', 'foo', pkey, signature);

        assert.ok(okay);
      }
    }
  }
}

// The sign function should not swallow OpenSSL errors.
// Regression test for https://github.com/nodejs/node/issues/40794.
{
  assert.throws(() => {
    const { privateKey } = crypto.generateKeyPairSync('rsa', {
      modulusLength: 512
    });
    crypto.sign('sha512', 'message', privateKey);
  }, {
    code: 'ERR_OSSL_RSA_DIGEST_TOO_BIG_FOR_RSA_KEY',
    message: /digest too big for rsa key/
  });
}

{
  // This should not cause a crash: https://github.com/nodejs/node/issues/44471
  for (const key of ['', 'foo', null, undefined, true, Boolean]) {
    assert.throws(() => {
      crypto.verify('sha256', 'foo', { key, format: 'jwk' }, Buffer.alloc(0));
    }, { code: 'ERR_INVALID_ARG_TYPE', message: /The "key\.key" property must be of type object/ });
    assert.throws(() => {
      crypto.createVerify('sha256').verify({ key, format: 'jwk' }, Buffer.alloc(0));
    }, { code: 'ERR_INVALID_ARG_TYPE', message: /The "key\.key" property must be of type object/ });
    assert.throws(() => {
      crypto.sign('sha256', 'foo', { key, format: 'jwk' });
    }, { code: 'ERR_INVALID_ARG_TYPE', message: /The "key\.key" property must be of type object/ });
    assert.throws(() => {
      crypto.createSign('sha256').sign({ key, format: 'jwk' });
    }, { code: 'ERR_INVALID_ARG_TYPE', message: /The "key\.key" property must be of type object/ });
  }
}

{
  // Ed25519 and Ed448 must use the one-shot methods
  const keys = [{ privateKey: fixtures.readKey('ed25519_private.pem', 'ascii'),
                  publicKey: fixtures.readKey('ed25519_public.pem', 'ascii') },
                { privateKey: fixtures.readKey('ed448_private.pem', 'ascii'),
                  publicKey: fixtures.readKey('ed448_public.pem', 'ascii') }];

  for (const { publicKey, privateKey } of keys) {
    assert.throws(() => {
      crypto.createSign('SHA256').update('Test123').sign(privateKey);
    }, { code: 'ERR_CRYPTO_UNSUPPORTED_OPERATION', message: 'Unsupported crypto operation' });
    assert.throws(() => {
      crypto.createVerify('SHA256').update('Test123').verify(privateKey, 'sig');
    }, { code: 'ERR_CRYPTO_UNSUPPORTED_OPERATION', message: 'Unsupported crypto operation' });
    assert.throws(() => {
      crypto.createVerify('SHA256').update('Test123').verify(publicKey, 'sig');
    }, { code: 'ERR_CRYPTO_UNSUPPORTED_OPERATION', message: 'Unsupported crypto operation' });
  }
}

{
  // Dh, x25519 and x448 should not be used for signing/verifying
  // https://github.com/nodejs/node/issues/53742
  for (const algo of ['dh', 'x25519', 'x448']) {
    const privateKey = fixtures.readKey(`${algo}_private.pem`, 'ascii');
    const publicKey = fixtures.readKey(`${algo}_public.pem`, 'ascii');
    assert.throws(() => {
      crypto.createSign('SHA256').update('Test123').sign(privateKey);
    }, { code: 'ERR_OSSL_EVP_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE', message: /operation not supported for this keytype/ });
    assert.throws(() => {
      crypto.createVerify('SHA256').update('Test123').verify(privateKey, 'sig');
    }, { code: 'ERR_OSSL_EVP_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE', message: /operation not supported for this keytype/ });
    assert.throws(() => {
      crypto.createVerify('SHA256').update('Test123').verify(publicKey, 'sig');
    }, { code: 'ERR_OSSL_EVP_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE', message: /operation not supported for this keytype/ });
  }
}
