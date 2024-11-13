// Flags: --no-warnings
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

if (!common.opensslCli)
  common.skip('missing openssl-cli');

const assert = require('assert');
const { X509Certificate } = require('crypto');
const { once } = require('events');
const tls = require('tls');
const { execFile } = require('child_process');
const fixtures = require('../common/fixtures');

const key = fixtures.readKey('agent2-key.pem');
const cert = fixtures.readKey('agent2-cert.pem');

// Prefer DHE over ECDHE when possible.
const dheCipher = 'DHE-RSA-AES128-SHA256';
const ecdheCipher = 'ECDHE-RSA-AES128-SHA256';
const ciphers = `${dheCipher}:${ecdheCipher}`;

if (!common.hasOpenSSL(3, 2)) {
  // Test will emit a warning because the DH parameter size is < 2048 bits
  // when the test is run on versions lower than OpenSSL32
  common.expectWarning('SecurityWarning',
                       'DH parameter is less than 2048 bits');
}

function loadDHParam(n) {
  const keyname = `dh${n}.pem`;
  return fixtures.readKey(keyname);
}

function test(dhparam, keylen, expectedCipher) {
  const options = {
    key,
    cert,
    ciphers,
    dhparam,
    maxVersion: 'TLSv1.2',
  };

  const server = tls.createServer(options, (conn) => conn.end());

  server.listen(0, '127.0.0.1', common.mustCall(() => {
    const args = ['s_client', '-connect', `127.0.0.1:${server.address().port}`,
                  '-cipher', `${ciphers}:@SECLEVEL=1`];

    execFile(common.opensslCli, args, common.mustSucceed((stdout) => {
      assert(keylen === null ||
             stdout.includes(`Server Temp Key: DH, ${keylen} bits`));
      assert(stdout.includes(`Cipher    : ${expectedCipher}`));
      server.close();
    }));
  }));

  return once(server, 'close');
}

function testCustomParam(keylen, expectedCipher) {
  const dhparam = loadDHParam(keylen);
  if (keylen === 'error') keylen = null;
  return test(dhparam, keylen, expectedCipher);
}

(async () => {
  // By default, DHE is disabled while ECDHE is enabled.
  for (const dhparam of [undefined, null]) {
    await test(dhparam, null, ecdheCipher);
  }

  // The DHE parameters selected by OpenSSL depend on the strength of the
  // certificate's key. For this test, we can assume that the modulus length
  // of the certificate's key is equal to the size of the DHE parameter, but
  // that is really only true for a few modulus lengths.
  const {
    publicKey: { asymmetricKeyDetails: { modulusLength } }
  } = new X509Certificate(cert);
  await test('auto', modulusLength, dheCipher);

  assert.throws(() => {
    testCustomParam(512);
  }, /DH parameter is less than 1024 bits/);

  // Custom DHE parameters are supported (but discouraged).
  if (!common.hasOpenSSL(3, 2)) {
    await testCustomParam(1024, dheCipher);
  } else {
    await testCustomParam(3072, dheCipher);
  }
  await testCustomParam(2048, dheCipher);

  // Invalid DHE parameters are discarded. ECDHE remains enabled.
  await testCustomParam('error', ecdheCipher);
})().then(common.mustCall());
