'use strict';
const common = require('../common');
if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const { opensslCli } = require('../common/crypto');

if (opensslCli === false) {
  common.skip('node compiled without OpenSSL CLI.');
}

const assert = require('assert');
const tls = require('tls');
const spawn = require('child_process').spawn;
const fixtures = require('../common/fixtures');

const cert = fixtures.readKey('rsa_cert.crt');
const key = fixtures.readKey('rsa_private.pem');
const server = tls.createServer({ cert, key }, common.mustNotCall());
const errors = [];
let stderr = '';

server.listen(0, '127.0.0.1', function() {
  const address = `${this.address().address}:${this.address().port}`;
  const args = ['s_client',
                '-ssl3',
                '-connect', address];

  const client = spawn(opensslCli, args, { stdio: 'pipe' });
  client.stdout.pipe(process.stdout);
  client.stderr.pipe(process.stderr);
  client.stderr.setEncoding('utf8');
  client.stderr.on('data', (data) => stderr += data);

  client.once('exit', common.mustCall(function(exitCode) {
    assert.strictEqual(exitCode, 1);
    server.close();
  }));
});

server.on('tlsClientError', (err) => errors.push(err));

process.on('exit', function() {
  if (/[Uu]nknown option:? -ssl3/.test(stderr)) {
    common.printSkipMessage('`openssl s_client -ssl3` not supported.');
  } else {
    assert.strictEqual(errors.length, 1);
    assert(/:version too low/.test(errors[0].message));
  }
});
