'use strict';
const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const { X509Certificate } = require('crypto');
const tls = require('tls');
const fixtures = require('../common/fixtures');

const altKeyCert = {
  key: fixtures.readKey('agent2-key.pem'),
  cert: fixtures.readKey('agent2-cert.pem'),
  minVersion: 'TLSv1.2',
};

const altKeyCertVals = [
  altKeyCert,
  tls.createSecureContext(altKeyCert),
];

(function next() {
  if (!altKeyCertVals.length)
    return;
  const altKeyCertVal = altKeyCertVals.shift();
  const options = {
    key: fixtures.readKey('agent1-key.pem'),
    cert: fixtures.readKey('agent1-cert.pem'),
    minVersion: 'TLSv1.3',
    ALPNCallback: common.mustCall(function({ servername, protocols }) {
      this.setKeyCert(altKeyCertVal);
      assert.deepStrictEqual(protocols, ['acme-tls/1']);
      return protocols[0];
    }),
  };

  tls.createServer(options, (s) => s.end()).listen(0, function() {
    this.on('connection', common.mustCall((socket) => this.close()));

    tls.connect({
      port: this.address().port,
      rejectUnauthorized: false,
      ALPNProtocols: ['acme-tls/1'],
    }, common.mustCall(function() {
      assert.strictEqual(this.getProtocol(), 'TLSv1.3');
      const altCert = new X509Certificate(altKeyCert.cert);
      assert.strictEqual(
        this.getPeerX509Certificate().raw.equals(altCert.raw),
        true
      );
      this.end();
      next();
    }));
  });
})();
