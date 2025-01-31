'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const fixtures = require('../common/fixtures');
const { hasOpenSSL } = require('../common/crypto');

const assert = require('assert');
const { X509Certificate } = require('crypto');
const tls = require('tls');

const key = fixtures.readKey('agent2-key.pem');
const cert = fixtures.readKey('agent2-cert.pem');

// TODO(@sam-github) test works with TLS1.3, rework test to add
//   'ECDH' with 'TLS_AES_128_GCM_SHA256',

function loadDHParam(n) {
  return fixtures.readKey(`dh${n}.pem`);
}

function test(size, type, name, cipher) {
  assert(cipher);

  const options = {
    key: key,
    cert: cert,
    ciphers: cipher,
    maxVersion: 'TLSv1.2',
  };

  if (name) options.ecdhCurve = name;

  if (type === 'DH') {
    if (size === 'auto') {
      options.dhparam = 'auto';
      // The DHE parameters selected by OpenSSL depend on the strength of the
      // certificate's key. For this test, we can assume that the modulus length
      // of the certificate's key is equal to the size of the DHE parameter, but
      // that is really only true for a few modulus lengths.
      ({
        publicKey: { asymmetricKeyDetails: { modulusLength: size } }
      } = new X509Certificate(cert));
    } else {
      options.dhparam = loadDHParam(size);
    }
  }

  const server = tls.createServer(options, common.mustCall((conn) => {
    assert.strictEqual(conn.getEphemeralKeyInfo(), null);
    conn.end();
  }));

  server.on('close', common.mustSucceed());

  server.listen(0, common.mustCall(() => {
    const client = tls.connect({
      port: server.address().port,
      rejectUnauthorized: false
    }, common.mustCall(function() {
      const ekeyinfo = client.getEphemeralKeyInfo();
      assert.strictEqual(ekeyinfo.type, type);
      assert.strictEqual(ekeyinfo.size, size);
      assert.strictEqual(ekeyinfo.name, name);
      server.close();
    }));
    client.on('secureConnect', common.mustCall());
  }));
}

test(undefined, undefined, undefined, 'AES256-SHA256');
test('auto', 'DH', undefined, 'DHE-RSA-AES256-GCM-SHA384');
if (!hasOpenSSL(3, 2)) {
  test(1024, 'DH', undefined, 'DHE-RSA-AES256-GCM-SHA384');
} else {
  test(3072, 'DH', undefined, 'DHE-RSA-AES256-GCM-SHA384');
}
test(2048, 'DH', undefined, 'DHE-RSA-AES256-GCM-SHA384');
test(256, 'ECDH', 'prime256v1', 'ECDHE-RSA-AES256-GCM-SHA384');
test(521, 'ECDH', 'secp521r1', 'ECDHE-RSA-AES256-GCM-SHA384');
test(253, 'ECDH', 'X25519', 'ECDHE-RSA-AES256-GCM-SHA384');
test(448, 'ECDH', 'X448', 'ECDHE-RSA-AES256-GCM-SHA384');
