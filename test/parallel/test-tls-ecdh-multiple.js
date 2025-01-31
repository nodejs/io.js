'use strict';
const common = require('../common');

// This test ensures that ecdhCurve option of TLS server supports colon
// separated ECDH curve names as value.

if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const { opensslCli } = require('../common/crypto');
const crypto = require('crypto');

if (!opensslCli) {
  common.skip('missing openssl-cli');
}

const assert = require('assert');
const tls = require('tls');
const { execFile } = require('child_process');
const fixtures = require('../common/fixtures');

function loadPEM(n) {
  return fixtures.readKey(`${n}.pem`);
}

const options = {
  key: loadPEM('agent2-key'),
  cert: loadPEM('agent2-cert'),
  ciphers: '-ALL:ECDHE-RSA-AES128-SHA256',
  ecdhCurve: 'secp256k1:prime256v1:secp521r1',
  maxVersion: 'TLSv1.2',
};

const reply = 'I AM THE WALRUS'; // Something recognizable

const server = tls.createServer(options, (conn) => {
  conn.end(reply);
}).listen(0, common.mustCall(() => {
  const args = ['s_client',
                '-cipher', `${options.ciphers}`,
                '-connect', `127.0.0.1:${server.address().port}`];

  execFile(opensslCli, args, common.mustSucceed((stdout) => {
    assert(stdout.includes(reply));
    server.close();
  }));
}));

{
  // Some unsupported curves.
  const unsupportedCurves = [
    'wap-wsg-idm-ecid-wtls1',
    'c2pnb163v1',
    'prime192v3',
  ];

  // Brainpool is not supported in FIPS mode.
  if (crypto.getFips()) {
    unsupportedCurves.push('brainpoolP256r1');
  }

  unsupportedCurves.forEach((ecdhCurve) => {
    assert.throws(() => tls.createServer({ ecdhCurve }),
                  /Error: Failed to set ECDH curve/);
  });
}
