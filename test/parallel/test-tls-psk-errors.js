'use strict';
const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const tls = require('tls');

{
  // Check tlsClientError on invalid pskIdentityHint.

  const server = tls.createServer({
    ciphers: 'PSK+HIGH',
    pskCallback: () => {},
    pskIdentityHint: 'a'.repeat(512), // Too long identity hint.
  });
  server.on('tlsClientError', (err) => {
    assert.ok(err instanceof Error);
    assert.strictEqual(err.code, 'ERR_TLS_PSK_SET_IDENTITY_HINT_FAILED');
    server.close();
  });
  server.listen(0, () => {
    const client = tls.connect({
      port: server.address().port,
      ciphers: 'PSK+HIGH',
      checkServerIdentity: () => {},
      pskCallback: () => {},
    }, () => {});
    client.on('error', common.expectsError({ code: 'ECONNRESET' }));
  });
}
