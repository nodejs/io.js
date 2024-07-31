// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.
// Flags: --no-warnings
'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const crypto = require('crypto');
const { inspect } = require('util');
const fixtures = require('../common/fixtures');

//
// Test authenticated encryption modes.
//
// !NEVER USE STATIC IVs IN REAL LIFE!
//

const TEST_CASES = require(fixtures.path('aead-vectors.js'));

const errMessages = {
  auth: / auth/,
  state: / state/,
  FIPS: /not supported in FIPS mode/,
  length: /Invalid initialization vector/,
  authTagLength: /Invalid authentication tag length/
};

const ciphers = crypto.getCiphers();

for (const test of TEST_CASES) {
  if (!ciphers.includes(test.algo)) {
    common.printSkipMessage(`unsupported ${test.algo} test`);
    continue;
  }

  if (common.hasFipsCrypto && test.iv.length < 24) {
    common.printSkipMessage('IV len < 12 bytes unsupported in FIPS mode');
    continue;
  }

  const isCCM = /^aes-(128|192|256)-ccm$/.test(test.algo);
  const isOCB = /^aes-(128|192|256)-ocb$/.test(test.algo);

  let options;
  if (isCCM || isOCB)
    options = { authTagLength: test.tag.length / 2 };

  const inputEncoding = test.plainIsHex ? 'hex' : 'ascii';

  let aadOptions;
  if (isCCM) {
    aadOptions = {
      plaintextLength: Buffer.from(test.plain, inputEncoding).length
    };
  }

  {
    const encrypt = crypto.createCipheriv(test.algo,
                                          Buffer.from(test.key, 'hex'),
                                          Buffer.from(test.iv, 'hex'),
                                          options);

    if (test.aad)
      encrypt.setAAD(Buffer.from(test.aad, 'hex'), aadOptions);

    let hex = encrypt.update(test.plain, inputEncoding, 'hex');
    hex += encrypt.final('hex');

    const auth_tag = encrypt.getAuthTag();
    // Only test basic encryption run if output is marked as tampered.
    if (!test.tampered) {
      assert.strictEqual(hex, test.ct);
      assert.strictEqual(auth_tag.toString('hex'), test.tag);
    }
  }

  {
    if (isCCM && common.hasFipsCrypto) {
      assert.throws(() => {
        crypto.createDecipheriv(test.algo,
                                Buffer.from(test.key, 'hex'),
                                Buffer.from(test.iv, 'hex'),
                                options);
      }, errMessages.FIPS);
    } else {
      const decrypt = crypto.createDecipheriv(test.algo,
                                              Buffer.from(test.key, 'hex'),
                                              Buffer.from(test.iv, 'hex'),
                                              options);
      decrypt.setAuthTag(Buffer.from(test.tag, 'hex'));
      if (test.aad)
        decrypt.setAAD(Buffer.from(test.aad, 'hex'), aadOptions);

      const outputEncoding = test.plainIsHex ? 'hex' : 'ascii';

      let msg = decrypt.update(test.ct, 'hex', outputEncoding);
      if (!test.tampered) {
        msg += decrypt.final(outputEncoding);
        assert.strictEqual(msg, test.plain);
      } else {
        // Assert that final throws if input data could not be verified!
        assert.throws(function() { decrypt.final('hex'); }, errMessages.auth);
      }
    }
  }

  {
    // Trying to get tag before inputting all data:
    const encrypt = crypto.createCipheriv(test.algo,
                                          Buffer.from(test.key, 'hex'),
                                          Buffer.from(test.iv, 'hex'),
                                          options);
    encrypt.update('blah', 'ascii');
    assert.throws(function() { encrypt.getAuthTag(); }, errMessages.state);
  }

  {
    // Trying to create cipher with incorrect IV length
    assert.throws(function() {
      crypto.createCipheriv(
        test.algo,
        Buffer.from(test.key, 'hex'),
        Buffer.alloc(0)
      );
    }, errMessages.length);
  }
}

// Non-authenticating mode:
{
  const encrypt =
      crypto.createCipheriv('aes-128-cbc',
                            'ipxp9a6i1Mb4USb4',
                            '6fKjEjR3Vl30EUYC');
  encrypt.update('blah', 'ascii');
  encrypt.final();
  assert.throws(() => encrypt.getAuthTag(), errMessages.state);
  assert.throws(() => encrypt.setAAD(Buffer.from('123', 'ascii')),
                errMessages.state);
}

// GCM only supports specific authentication tag lengths, invalid lengths should
// throw.
{
  for (const length of [0, 1, 2, 6, 9, 10, 11, 17]) {
    assert.throws(() => {
      const decrypt = crypto.createDecipheriv('aes-128-gcm',
                                              'FxLKsqdmv0E9xrQh',
                                              'qkuZpJWCewa6Szih');
      decrypt.setAuthTag(Buffer.from('1'.repeat(length)));
    }, {
      name: 'TypeError',
      message: /Invalid authentication tag length/
    });

    assert.throws(() => {
      crypto.createCipheriv('aes-256-gcm',
                            'FxLKsqdmv0E9xrQhp0b1ZgI0K7JFZJM8',
                            'qkuZpJWCewa6Szih',
                            {
                              authTagLength: length
                            });
    }, {
      name: 'TypeError',
      message: /Invalid authentication tag length/
    });

    assert.throws(() => {
      crypto.createDecipheriv('aes-256-gcm',
                              'FxLKsqdmv0E9xrQhp0b1ZgI0K7JFZJM8',
                              'qkuZpJWCewa6Szih',
                              {
                                authTagLength: length
                              });
    }, {
      name: 'TypeError',
      message: /Invalid authentication tag length/
    });
  }
}

// Test that GCM can produce shorter authentication tags than 16 bytes.
{
  const fullTag = '1debb47b2c91ba2cea16fad021703070';
  for (const [authTagLength, e] of [[undefined, 16], [12, 12], [4, 4]]) {
    const cipher = crypto.createCipheriv('aes-256-gcm',
                                         'FxLKsqdmv0E9xrQhp0b1ZgI0K7JFZJM8',
                                         'qkuZpJWCewa6Szih', {
                                           authTagLength
                                         });
    cipher.setAAD(Buffer.from('abcd'));
    cipher.update('01234567', 'hex');
    cipher.final();
    const tag = cipher.getAuthTag();
    assert.strictEqual(tag.toString('hex'), fullTag.slice(0, 2 * e));
  }
}

// Test that users can manually restrict the GCM tag length to a single value.
{
  const decipher = crypto.createDecipheriv('aes-256-gcm',
                                           'FxLKsqdmv0E9xrQhp0b1ZgI0K7JFZJM8',
                                           'qkuZpJWCewa6Szih', {
                                             authTagLength: 8
                                           });

  assert.throws(() => {
    // This tag would normally be allowed.
    decipher.setAuthTag(Buffer.from('1'.repeat(12)));
  }, {
    name: 'TypeError',
    message: /Invalid authentication tag length/
  });

  // The Decipher object should be left intact.
  decipher.setAuthTag(Buffer.from('445352d3ff85cf94', 'hex'));
  const text = Buffer.concat([
    decipher.update('3a2a3647', 'hex'),
    decipher.final(),
  ]);
  assert.strictEqual(text.toString('utf8'), 'node');
}

// Test that create(De|C)ipher(iv)? throws if the mode is CCM and an invalid
// authentication tag length has been specified.
{
  for (const authTagLength of [-1, true, false, NaN, 5.5]) {
    assert.throws(() => {
      crypto.createCipheriv('aes-256-ccm',
                            'FxLKsqdmv0E9xrQhp0b1ZgI0K7JFZJM8',
                            'qkuZpJWCewa6S',
                            {
                              authTagLength
                            });
    }, {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_VALUE',
      message: "The property 'options.authTagLength' is invalid. " +
               `Received ${inspect(authTagLength)}`
    });

    assert.throws(() => {
      crypto.createDecipheriv('aes-256-ccm',
                              'FxLKsqdmv0E9xrQhp0b1ZgI0K7JFZJM8',
                              'qkuZpJWCewa6S',
                              {
                                authTagLength
                              });
    }, {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_VALUE',
      message: "The property 'options.authTagLength' is invalid. " +
        `Received ${inspect(authTagLength)}`
    });
  }

  // The following values will not be caught by the JS layer and thus will not
  // use the default error codes.
  for (const authTagLength of [0, 1, 2, 3, 5, 7, 9, 11, 13, 15, 17, 18]) {
    assert.throws(() => {
      crypto.createCipheriv('aes-256-ccm',
                            'FxLKsqdmv0E9xrQhp0b1ZgI0K7JFZJM8',
                            'qkuZpJWCewa6S',
                            {
                              authTagLength
                            });
    }, errMessages.authTagLength);

    if (!common.hasFipsCrypto) {
      assert.throws(() => {
        crypto.createDecipheriv('aes-256-ccm',
                                'FxLKsqdmv0E9xrQhp0b1ZgI0K7JFZJM8',
                                'qkuZpJWCewa6S',
                                {
                                  authTagLength
                                });
      }, errMessages.authTagLength);
    }
  }
}

// Test that create(De|C)ipher(iv)? throws if the mode is CCM or OCB and no
// authentication tag has been specified.
{
  for (const mode of ['ccm', 'ocb']) {
    assert.throws(() => {
      crypto.createCipheriv(`aes-256-${mode}`,
                            'FxLKsqdmv0E9xrQhp0b1ZgI0K7JFZJM8',
                            'qkuZpJWCewa6S');
    }, {
      message: `authTagLength required for aes-256-${mode}`
    });

    // CCM decryption and create(De|C)ipher are unsupported in FIPS mode.
    if (!common.hasFipsCrypto) {
      assert.throws(() => {
        crypto.createDecipheriv(`aes-256-${mode}`,
                                'FxLKsqdmv0E9xrQhp0b1ZgI0K7JFZJM8',
                                'qkuZpJWCewa6S');
      }, {
        message: `authTagLength required for aes-256-${mode}`
      });
    }
  }
}

// Test that setAAD throws if an invalid plaintext length has been specified.
{
  const cipher = crypto.createCipheriv('aes-256-ccm',
                                       'FxLKsqdmv0E9xrQhp0b1ZgI0K7JFZJM8',
                                       'qkuZpJWCewa6S',
                                       {
                                         authTagLength: 10
                                       });

  for (const plaintextLength of [-1, true, false, NaN, 5.5]) {
    assert.throws(() => {
      cipher.setAAD(Buffer.from('0123456789', 'hex'), { plaintextLength });
    }, {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_VALUE',
      message: "The property 'options.plaintextLength' is invalid. " +
        `Received ${inspect(plaintextLength)}`
    });
  }
}

// Test that setAAD and update throw if the plaintext is too long.
{
  for (const ivLength of [13, 12]) {
    const maxMessageSize = (1 << (8 * (15 - ivLength))) - 1;
    const key = 'FxLKsqdmv0E9xrQhp0b1ZgI0K7JFZJM8';
    const cipher = () => crypto.createCipheriv('aes-256-ccm', key,
                                               '0'.repeat(ivLength),
                                               {
                                                 authTagLength: 10
                                               });

    assert.throws(() => {
      cipher().setAAD(Buffer.alloc(0), {
        plaintextLength: maxMessageSize + 1
      });
    }, /Invalid message length$/);

    const msg = Buffer.alloc(maxMessageSize + 1);
    assert.throws(() => {
      cipher().update(msg);
    }, /Invalid message length/);

    const c = cipher();
    c.setAAD(Buffer.alloc(0), {
      plaintextLength: maxMessageSize
    });
    c.update(msg.slice(1));
  }
}

// Test that setAAD throws if the mode is CCM and the plaintext length has not
// been specified.
{
  assert.throws(() => {
    const cipher = crypto.createCipheriv('aes-256-ccm',
                                         'FxLKsqdmv0E9xrQhp0b1ZgI0K7JFZJM8',
                                         'qkuZpJWCewa6S',
                                         {
                                           authTagLength: 10
                                         });
    cipher.setAAD(Buffer.from('0123456789', 'hex'));
  }, /options\.plaintextLength required for CCM mode with AAD/);

  if (!common.hasFipsCrypto) {
    assert.throws(() => {
      const cipher = crypto.createDecipheriv('aes-256-ccm',
                                             'FxLKsqdmv0E9xrQhp0b1ZgI0K7JFZJM8',
                                             'qkuZpJWCewa6S',
                                             {
                                               authTagLength: 10
                                             });
      cipher.setAAD(Buffer.from('0123456789', 'hex'));
    }, /options\.plaintextLength required for CCM mode with AAD/);
  }
}

// Test that final() throws in CCM mode when no authentication tag is provided.
{
  if (!common.hasFipsCrypto) {
    const key = Buffer.from('1ed2233fa2223ef5d7df08546049406c', 'hex');
    const iv = Buffer.from('7305220bca40d4c90e1791e9', 'hex');
    const ct = Buffer.from('8beba09d4d4d861f957d51c0794f4abf8030848e', 'hex');
    const decrypt = crypto.createDecipheriv('aes-128-ccm', key, iv, {
      authTagLength: 10
    });
    // Normally, we would do this:
    // decrypt.setAuthTag(Buffer.from('0d9bcd142a94caf3d1dd', 'hex'));
    assert.throws(() => {
      decrypt.setAAD(Buffer.from('63616c76696e', 'hex'), {
        plaintextLength: ct.length
      });
      decrypt.update(ct);
      decrypt.final();
    }, errMessages.state);
  }
}

// Test that setAuthTag does not throw in GCM mode when called after setAAD.
{
  const key = Buffer.from('1ed2233fa2223ef5d7df08546049406c', 'hex');
  const iv = Buffer.from('579d9dfde9cd93d743da1ceaeebb86e4', 'hex');
  const decrypt = crypto.createDecipheriv('aes-128-gcm', key, iv);
  decrypt.setAAD(Buffer.from('0123456789', 'hex'));
  decrypt.setAuthTag(Buffer.from('1bb9253e250b8069cde97151d7ef32d9', 'hex'));
  assert.strictEqual(decrypt.update('807022', 'hex', 'hex'), 'abcdef');
  assert.strictEqual(decrypt.final('hex'), '');
}

// Test that an IV length of 11 does not overflow max_message_size_.
{
  const key = 'x'.repeat(16);
  const iv = Buffer.from('112233445566778899aabb', 'hex');
  const options = { authTagLength: 8 };
  const encrypt = crypto.createCipheriv('aes-128-ccm', key, iv, options);
  encrypt.update('boom');  // Should not throw 'Message exceeds maximum size'.
  encrypt.final();
}

// Test that the authentication tag can be set at any point before calling
// final() in GCM or OCB mode.
{
  const plain = Buffer.from('Hello world', 'utf8');
  const key = Buffer.from('0123456789abcdef', 'utf8');
  const iv = Buffer.from('0123456789ab', 'utf8');

  for (const mode of ['gcm', 'ocb']) {
    for (const authTagLength of mode === 'gcm' ? [undefined, 8] : [8]) {
      const cipher = crypto.createCipheriv(`aes-128-${mode}`, key, iv, {
        authTagLength
      });
      const ciphertext = Buffer.concat([cipher.update(plain), cipher.final()]);
      const authTag = cipher.getAuthTag();

      for (const authTagBeforeUpdate of [true, false]) {
        const decipher = crypto.createDecipheriv(`aes-128-${mode}`, key, iv, {
          authTagLength
        });
        if (authTagBeforeUpdate) {
          decipher.setAuthTag(authTag);
        }
        const resultUpdate = decipher.update(ciphertext);
        if (!authTagBeforeUpdate) {
          decipher.setAuthTag(authTag);
        }
        const resultFinal = decipher.final();
        const result = Buffer.concat([resultUpdate, resultFinal]);
        assert(result.equals(plain));
      }
    }
  }
}

// Test that setAuthTag can only be called once.
{
  const plain = Buffer.from('Hello world', 'utf8');
  const key = Buffer.from('0123456789abcdef', 'utf8');
  const iv = Buffer.from('0123456789ab', 'utf8');
  const opts = { authTagLength: 8 };

  for (const mode of ['gcm', 'ccm', 'ocb']) {
    const cipher = crypto.createCipheriv(`aes-128-${mode}`, key, iv, opts);
    const ciphertext = Buffer.concat([cipher.update(plain), cipher.final()]);
    const tag = cipher.getAuthTag();

    const decipher = crypto.createDecipheriv(`aes-128-${mode}`, key, iv, opts);
    decipher.setAuthTag(tag);
    assert.throws(() => {
      decipher.setAuthTag(tag);
    }, errMessages.state);
    // Decryption should still work.
    const plaintext = Buffer.concat([
      decipher.update(ciphertext),
      decipher.final(),
    ]);
    assert(plain.equals(plaintext));
  }
}


// Test chacha20-poly1305 rejects invalid IV lengths of 13, 14, 15, and 16 (a
// length of 17 or greater was already rejected).
// - https://www.openssl.org/news/secadv/20190306.txt
{
  // Valid extracted from TEST_CASES, check that it detects IV tampering.
  const valid = {
    algo: 'chacha20-poly1305',
    key: '808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f',
    iv: '070000004041424344454647',
    plain: '4c616469657320616e642047656e746c656d656e206f662074686520636c6173' +
           '73206f66202739393a204966204920636f756c64206f6666657220796f75206f' +
           '6e6c79206f6e652074697020666f7220746865206675747572652c2073756e73' +
           '637265656e20776f756c642062652069742e',
    plainIsHex: true,
    aad: '50515253c0c1c2c3c4c5c6c7',
    ct: 'd31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5' +
        'a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de0a9e06' +
        '0b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fa' +
        'b324e4fad675945585808b4831d7bc3ff4def08e4b7a9de576d265' +
        '86cec64b6116',
    tag: '1ae10b594f09e26a7e902ecbd0600691',
    tampered: false,
  };

  // Invalid IV lengths should be detected:
  // - 12 and below are valid.
  // - 13-16 are not detected as invalid by some OpenSSL versions.
  check(13);
  check(14);
  check(15);
  check(16);
  // - 17 and above were always detected as invalid by OpenSSL.
  check(17);

  function check(ivLength) {
    const prefix = ivLength - valid.iv.length / 2;
    assert.throws(() => crypto.createCipheriv(
      valid.algo,
      Buffer.from(valid.key, 'hex'),
      Buffer.from(H(prefix) + valid.iv, 'hex')
    ), errMessages.length, `iv length ${ivLength} was not rejected`);

    function H(length) { return '00'.repeat(length); }
  }
}

{
  // CCM cipher without data should not crash, see https://github.com/nodejs/node/issues/38035.
  const algo = 'aes-128-ccm';
  const key = Buffer.alloc(16);
  const iv = Buffer.alloc(12);
  const opts = { authTagLength: 10 };

  for (const cipher of [
    crypto.createCipheriv(algo, key, iv, opts),
  ]) {
    assert.throws(() => {
      cipher.final();
    }, common.hasOpenSSL3 ? {
      code: 'ERR_OSSL_TAG_NOT_SET'
    } : {
      message: /Unsupported state/
    });
  }
}

{
  const key = Buffer.alloc(32);
  const iv = Buffer.alloc(12);

  for (const authTagLength of [0, 17]) {
    assert.throws(() => {
      crypto.createCipheriv('chacha20-poly1305', key, iv, { authTagLength });
    }, {
      code: 'ERR_CRYPTO_INVALID_AUTH_TAG',
      message: errMessages.authTagLength
    });
  }
}

// ChaCha20-Poly1305 should respect the authTagLength option and should not
// require the authentication tag before calls to update() during decryption.
{
  const key = Buffer.alloc(32);
  const iv = Buffer.alloc(12);

  for (let authTagLength = 1; authTagLength <= 16; authTagLength++) {
    const cipher =
        crypto.createCipheriv('chacha20-poly1305', key, iv, { authTagLength });
    const ciphertext = Buffer.concat([cipher.update('foo'), cipher.final()]);
    const authTag = cipher.getAuthTag();
    assert.strictEqual(authTag.length, authTagLength);

    // The decipher operation should reject all authentication tags other than
    // that of the expected length.
    for (let other = 1; other <= 16; other++) {
      const decipher = crypto.createDecipheriv('chacha20-poly1305', key, iv, {
        authTagLength: other
      });
      // ChaCha20 is a stream cipher so we do not need to call final() to obtain
      // the full plaintext.
      const plaintext = decipher.update(ciphertext);
      assert.strictEqual(plaintext.toString(), 'foo');
      if (other === authTagLength) {
        // The authentication tag length is as expected and the tag itself is
        // correct, so this should work.
        decipher.setAuthTag(authTag);
        decipher.final();
      } else {
        // The authentication tag that we are going to pass to setAuthTag is
        // either too short or too long. If other < authTagLength, the
        // authentication tag is still correct, but it should still be rejected
        // because its security assurance is lower than expected.
        assert.throws(() => {
          decipher.setAuthTag(authTag);
        }, {
          code: 'ERR_CRYPTO_INVALID_AUTH_TAG',
          message: `Invalid authentication tag length: ${authTagLength}`
        });
      }
    }
  }
}

// ChaCha20-Poly1305 should default to an authTagLength of 16. When encrypting,
// this matches the behavior of GCM ciphers. When decrypting, however, it is
// stricter than GCM in that it only allows authentication tags that are exactly
// 16 bytes long, whereas, when no authTagLength was specified, GCM would accept
// shorter tags as long as their length was valid according to NIST SP 800-38D.
// For ChaCha20-Poly1305, we intentionally deviate from that because there are
// no recommended or approved authentication tag lengths below 16 bytes.
{
  const rfcTestCases = TEST_CASES.filter(({ algo, tampered }) => {
    return algo === 'chacha20-poly1305' && tampered === false;
  });
  assert.strictEqual(rfcTestCases.length, 1);

  const [testCase] = rfcTestCases;
  const key = Buffer.from(testCase.key, 'hex');
  const iv = Buffer.from(testCase.iv, 'hex');
  const aad = Buffer.from(testCase.aad, 'hex');

  for (const opt of [
    undefined,
    { authTagLength: undefined },
    { authTagLength: 16 },
  ]) {
    const cipher = crypto.createCipheriv('chacha20-poly1305', key, iv, opt);
    const ciphertext = Buffer.concat([
      cipher.setAAD(aad).update(testCase.plain, 'hex'),
      cipher.final(),
    ]);
    const authTag = cipher.getAuthTag();

    assert.strictEqual(ciphertext.toString('hex'), testCase.ct);
    assert.strictEqual(authTag.toString('hex'), testCase.tag);

    const decipher = crypto.createDecipheriv('chacha20-poly1305', key, iv, opt);
    const plaintext = Buffer.concat([
      decipher.setAAD(aad).update(ciphertext),
      decipher.setAuthTag(authTag).final(),
    ]);

    assert.strictEqual(plaintext.toString('hex'), testCase.plain);
  }
}

// https://github.com/nodejs/node/issues/45874
{
  const rfcTestCases = TEST_CASES.filter(({ algo, tampered }) => {
    return algo === 'chacha20-poly1305' && tampered === false;
  });
  assert.strictEqual(rfcTestCases.length, 1);

  const [testCase] = rfcTestCases;
  const key = Buffer.from(testCase.key, 'hex');
  const iv = Buffer.from(testCase.iv, 'hex');
  const aad = Buffer.from(testCase.aad, 'hex');
  const opt = { authTagLength: 16 };

  const cipher = crypto.createCipheriv('chacha20-poly1305', key, iv, opt);
  const ciphertext = Buffer.concat([
    cipher.setAAD(aad).update(testCase.plain, 'hex'),
    cipher.final(),
  ]);
  const authTag = cipher.getAuthTag();

  assert.strictEqual(ciphertext.toString('hex'), testCase.ct);
  assert.strictEqual(authTag.toString('hex'), testCase.tag);

  const decipher = crypto.createDecipheriv('chacha20-poly1305', key, iv, opt);
  decipher.setAAD(aad).update(ciphertext);

  assert.throws(() => {
    decipher.final();
  }, /Unsupported state or unable to authenticate data/);
}
