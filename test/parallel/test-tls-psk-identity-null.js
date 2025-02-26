'use strict';
const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const tls = require('tls');

{
  const pskCallback = () => undefined;
  const server = tls.createServer({ pskCallback });

  server.listen(0, common.mustCall(() => {
    const client = tls.connect({
      port: server.address().port,
      pskIdentity: 'test',
      pskCallback: () => {
        return undefined;
      }
    });

    client.on('error', (err) => {
      assert.strictEqual(err.code, 'ERR_SSL_SSLV3_ALERT_HANDSHAKE_FAILURE');
      server.close();
    });
  }));
}

{
  const pskCallback = () => null;
  const server = tls.createServer({ pskCallback });

  server.listen(0, common.mustCall(() => {
    const client = tls.connect({
      port: server.address().port,
      pskIdentity: 'test',
      pskCallback: () => {
        return null;
      }
    });

    client.on('error', (err) => {
      assert.strictEqual(err.code, 'ERR_SSL_SSLV3_ALERT_HANDSHAKE_FAILURE');
      server.close();
    });
  }));
}
