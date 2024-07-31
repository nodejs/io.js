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

'use strict';
const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const crypto = require('crypto');
const tls = require('tls');
const fixtures = require('../common/fixtures');

// Test Certificates
const certPfx = fixtures.readKey('rsa_cert.pfx');

// 'this' safety
// https://github.com/joyent/node/issues/6690
assert.throws(() => {
  const credentials = tls.createSecureContext();
  const context = credentials.context;
  const notcontext = { setOptions: context.setOptions };

  // Methods of native objects should not segfault when reassigned to a new
  // object and called illegally. This core dumped in 0.10 and was fixed in
  // 0.11.
  notcontext.setOptions();
}, (err) => {
  // Throws TypeError, so there is no opensslErrorStack property.
  return err instanceof TypeError &&
         err.name === 'TypeError' &&
         /^TypeError: Illegal invocation$/.test(err) &&
         !('opensslErrorStack' in err);
});

// PFX tests
tls.createSecureContext({ pfx: certPfx, passphrase: 'sample' });

assert.throws(() => {
  tls.createSecureContext({ pfx: certPfx });
}, (err) => {
  // Throws general Error, so there is no opensslErrorStack property.
  return err instanceof Error &&
         err.name === 'Error' &&
         /^Error: mac verify failure$/.test(err) &&
         !('opensslErrorStack' in err);
});

assert.throws(() => {
  tls.createSecureContext({ pfx: certPfx, passphrase: 'test' });
}, (err) => {
  // Throws general Error, so there is no opensslErrorStack property.
  return err instanceof Error &&
         err.name === 'Error' &&
         /^Error: mac verify failure$/.test(err) &&
         !('opensslErrorStack' in err);
});

assert.throws(() => {
  tls.createSecureContext({ pfx: 'sample', passphrase: 'test' });
}, (err) => {
  // Throws general Error, so there is no opensslErrorStack property.
  return err instanceof Error &&
         err.name === 'Error' &&
         /^Error: not enough data$/.test(err) &&
         !('opensslErrorStack' in err);
});


// update() should only take buffers / strings
assert.throws(
  () => crypto.createHash('sha1').update({ foo: 'bar' }),
  {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError'
  });


function validateList(list) {
  // The list must not be empty
  assert(list.length > 0);

  // The list should be sorted.
  // Array#sort() modifies the list in place so make a copy.
  const sorted = [...list].sort();
  assert.deepStrictEqual(list, sorted);

  // Each element should be unique.
  assert.strictEqual([...new Set(list)].length, list.length);

  // Each element should be a string.
  assert(list.every((value) => typeof value === 'string'));
}

// Assume that we have at least AES-128-CBC.
const cryptoCiphers = crypto.getCiphers();
assert(crypto.getCiphers().includes('aes-128-cbc'));
validateList(cryptoCiphers);
// Make sure all of the ciphers are supported by OpenSSL
for (const algo of cryptoCiphers) {
  const { ivLength, keyLength, mode } = crypto.getCipherInfo(algo);
  let options;
  if (mode === 'ccm')
    options = { authTagLength: 8 };
  else if (mode === 'ocb' || algo === 'chacha20-poly1305')
    options = { authTagLength: 16 };
  crypto.createCipheriv(algo,
                        crypto.randomBytes(keyLength),
                        crypto.randomBytes(ivLength || 0),
                        options);
}

// Assume that we have at least AES256-SHA.
const tlsCiphers = tls.getCiphers();
assert(tls.getCiphers().includes('aes256-sha'));
assert(tls.getCiphers().includes('tls_aes_128_ccm_8_sha256'));
// There should be no capital letters in any element.
const noCapitals = /^[^A-Z]+$/;
assert(tlsCiphers.every((value) => noCapitals.test(value)));
validateList(tlsCiphers);

// Assert that we have sha1 and sha256 but not SHA1 and SHA256.
assert.notStrictEqual(crypto.getHashes().length, 0);
assert(crypto.getHashes().includes('sha1'));
assert(crypto.getHashes().includes('sha256'));
assert(!crypto.getHashes().includes('SHA1'));
assert(!crypto.getHashes().includes('SHA256'));
assert(crypto.getHashes().includes('RSA-SHA1'));
assert(!crypto.getHashes().includes('rsa-sha1'));
validateList(crypto.getHashes());
// Make sure all of the hashes are supported by OpenSSL
for (const algo of crypto.getHashes())
  crypto.createHash(algo);

// Assume that we have at least secp384r1.
assert.notStrictEqual(crypto.getCurves().length, 0);
assert(crypto.getCurves().includes('secp384r1'));
assert(!crypto.getCurves().includes('SECP384R1'));
validateList(crypto.getCurves());

// Modifying return value from get* functions should not mutate subsequent
// return values.
function testImmutability(fn) {
  const list = fn();
  const copy = [...list];
  list.push('some-arbitrary-value');
  assert.deepStrictEqual(fn(), copy);
}

testImmutability(crypto.getCiphers);
testImmutability(tls.getCiphers);
testImmutability(crypto.getHashes);
testImmutability(crypto.getCurves);

const encodingError = {
  code: 'ERR_INVALID_ARG_VALUE',
  name: 'TypeError',
  message: "The argument 'encoding' is invalid for data of length 1." +
           " Received 'hex'",
};

assert.throws(
  () => crypto.createHash('sha1').update('0', 'hex'),
  (error) => {
    assert.ok(!('opensslErrorStack' in error));
    assert.throws(() => { throw error; }, encodingError);
    return true;
  }
);

assert.throws(
  () => crypto.createHmac('sha256', 'a secret').update('0', 'hex'),
  (error) => {
    assert.ok(!('opensslErrorStack' in error));
    assert.throws(() => { throw error; }, encodingError);
    return true;
  }
);

assert.throws(() => {
  const priv = [
    '-----BEGIN RSA PRIVATE KEY-----',
    'MIGrAgEAAiEA+3z+1QNF2/unumadiwEr+C5vfhezsb3hp4jAnCNRpPcCAwEAAQIgQNriSQK4',
    'EFwczDhMZp2dvbcz7OUUyt36z3S4usFPHSECEQD/41K7SujrstBfoCPzwC1xAhEA+5kt4BJy',
    'eKN7LggbF3Dk5wIQN6SL+fQ5H/+7NgARsVBp0QIRANxYRukavs4QvuyNhMx+vrkCEQCbf6j/',
    'Ig6/HueCK/0Jkmp+',
    '-----END RSA PRIVATE KEY-----',
    '',
  ].join('\n');
  crypto.createSign('SHA256').update('test').sign(priv);
}, (err) => {
  if (!common.hasOpenSSL3)
    assert.ok(!('opensslErrorStack' in err));
  assert.throws(() => { throw err; }, common.hasOpenSSL3 ? {
    name: 'Error',
    message: 'error:02000070:rsa routines::digest too big for rsa key',
    library: 'rsa routines',
  } : {
    name: 'Error',
    message: /routines:RSA_sign:digest too big for rsa key$/,
    library: 'rsa routines',
    function: 'RSA_sign',
    reason: 'digest too big for rsa key',
    code: 'ERR_OSSL_RSA_DIGEST_TOO_BIG_FOR_RSA_KEY'
  });
  return true;
});

if (!common.hasOpenSSL3) {
  assert.throws(() => {
    // The correct header inside `rsa_private_pkcs8_bad.pem` should have been
    // -----BEGIN PRIVATE KEY----- and -----END PRIVATE KEY-----
    // instead of
    // -----BEGIN RSA PRIVATE KEY----- and -----END RSA PRIVATE KEY-----
    const sha1_privateKey = fixtures.readKey('rsa_private_pkcs8_bad.pem',
                                             'ascii');
    // This would inject errors onto OpenSSL's error stack
    crypto.createSign('sha1').sign(sha1_privateKey);
  }, (err) => {
    // Do the standard checks, but then do some custom checks afterwards.
    assert.throws(() => { throw err; }, {
      message: 'error:0D0680A8:asn1 encoding routines:asn1_check_tlen:' +
               'wrong tag',
      library: 'asn1 encoding routines',
      function: 'asn1_check_tlen',
      reason: 'wrong tag',
      code: 'ERR_OSSL_ASN1_WRONG_TAG',
    });
    // Throws crypto error, so there is an opensslErrorStack property.
    // The openSSL stack should have content.
    assert(Array.isArray(err.opensslErrorStack));
    assert(err.opensslErrorStack.length > 0);
    return true;
  });
}

// Make sure memory isn't released before being returned
console.log(crypto.randomBytes(16));

assert.throws(() => {
  tls.createSecureContext({ crl: 'not a CRL' });
}, (err) => {
  // Throws general error, so there is no opensslErrorStack property.
  return err instanceof Error &&
         /^Error: Failed to parse CRL$/.test(err) &&
         !('opensslErrorStack' in err);
});

/**
 * Check if the stream function uses utf8 as a default encoding.
 */

function testEncoding(options, assertionHash) {
  const hash = crypto.createHash('sha256', options);
  let hashValue = '';

  hash.on('data', (data) => {
    // The defaultEncoding has no effect on the hash value. It only affects data
    // consumed by the Hash transform stream.
    assert(Buffer.isBuffer(data));
    hashValue += data.toString('hex');
  });

  hash.on('end', common.mustCall(() => {
    assert.strictEqual(hashValue, assertionHash);
  }));

  hash.write('öäü');
  hash.end();

  assert.strictEqual(hash._writableState.defaultEncoding, options?.defaultEncoding ?? 'utf8');
}

// Hash of "öäü" in utf8 format
const assertionHashUtf8 =
  '4f53d15bee524f082380e6d7247cc541e7cb0d10c64efdcc935ceeb1e7ea345c';

// Hash of "öäü" in latin1 format
const assertionHashLatin1 =
  'cd37bccd5786e2e76d9b18c871e919e6eb11cc12d868f5ae41c40ccff8e44830';

testEncoding(undefined, assertionHashUtf8);
testEncoding({}, assertionHashUtf8);

testEncoding({
  defaultEncoding: 'utf8'
}, assertionHashUtf8);

testEncoding({
  defaultEncoding: 'latin1'
}, assertionHashLatin1);
