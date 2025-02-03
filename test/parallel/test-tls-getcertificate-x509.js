'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const tls = require('tls');
const fixtures = require('../common/fixtures');
const { X509Certificate } = require('crypto');

const options = {
  key: fixtures.readKey('agent6-key.pem'),
  cert: fixtures.readKey('agent6-cert.pem')
};

const server = tls.createServer(options, function(cleartext) {
  cleartext.end('World');
});

server.once('secureConnection', common.mustCall(function(socket) {
  const cert = socket.getX509Certificate();
  assert(cert instanceof X509Certificate);
  assert.match(cert.serialNumber, /5B75D77EDC7FB5B7FA9F1424DA4C64FB815DCBDE/i);
}));

server.listen(0, common.mustCall(function() {
  const socket = tls.connect({
    port: this.address().port,
    rejectUnauthorized: false
  }, common.mustCall(function() {
    const peerCert = socket.getPeerX509Certificate();
    assert(peerCert.issuerCertificate instanceof X509Certificate);
    assert.strictEqual(peerCert.issuerCertificate.issuerCertificate, undefined);
    assert.match(peerCert.issuerCertificate.serialNumber, /147D36C1C2F74206DE9FAB5F2226D78ADB00A425/i);
    server.close();
  }));
  socket.end('Hello');
}));
