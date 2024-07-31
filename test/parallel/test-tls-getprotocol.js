'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

// This test ensures that `getProtocol` returns the right protocol
// from a TLS connection

const assert = require('assert');
const tls = require('tls');
const fixtures = require('../common/fixtures');

const clientConfigs = [
  {
    secureProtocol: 'TLSv1_method',
    version: 'TLSv1',
    ciphers: (common.hasOpenSSL31 ? 'DEFAULT:@SECLEVEL=0' : 'DEFAULT')
  }, {
    secureProtocol: 'TLSv1_1_method',
    version: 'TLSv1.1',
    ciphers: (common.hasOpenSSL31 ? 'DEFAULT:@SECLEVEL=0' : 'DEFAULT')
  }, {
    secureProtocol: 'TLSv1_2_method',
    version: 'TLSv1.2'
  },
];

const serverConfig = {
  secureProtocol: 'TLS_method',
  ciphers: 'RSA@SECLEVEL=0',
  key: fixtures.readKey('agent2-key.pem'),
  cert: fixtures.readKey('agent2-cert.pem')
};

const server = tls.createServer(serverConfig, common.mustCall(clientConfigs.length))
.listen(0, common.localhostIPv4, function() {
  let connected = 0;
  for (const v of clientConfigs) {
    tls.connect({
      host: common.localhostIPv4,
      port: server.address().port,
      ciphers: v.ciphers,
      rejectUnauthorized: false,
      secureProtocol: v.secureProtocol
    }, common.mustCall(function() {
      assert.strictEqual(this.getProtocol(), v.version);
      this.on('end', common.mustCall());
      this.on('close', common.mustCall(function() {
        assert.strictEqual(this.getProtocol(), null);
      })).end();
      if (++connected === clientConfigs.length)
        server.close();
    }));
  }
});
