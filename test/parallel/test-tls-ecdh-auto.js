'use strict';
const common = require('../common');

// This test ensures that the value "auto" on ecdhCurve option is
// supported to enable automatic curve selection in TLS server.

if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const { opensslCli } = require('../common/crypto');

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
  ecdhCurve: 'auto',
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
