'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');
if (!common.hasCrypto) common.skip('missing crypto');

const assert = require('assert');
const https = require('https');
const http2 = require('http2');

(async function main() {
  const server = http2.createSecureServer({
    key: fixtures.readKey('agent1-key.pem'),
    cert: fixtures.readKey('agent1-cert.pem'),
    allowHTTP1: true,
  });

  server.on(
    'request',
    common.mustCall((req, res) => {
      res.writeHead(200);
      res.end();
    })
  );

  server.on(
    'close',
    common.mustCall()
  );

  await new Promise((resolve) => server.listen(0, resolve));

  await new Promise((resolve) =>
    https.get(
      `https://localhost:${server.address().port}`,
      {
        rejectUnauthorized: false,
        headers: { connection: 'keep-alive' },
      },
      resolve
    )
  );

  let serverClosed = false;
  setImmediate(
    common.mustCall(() => {
      assert.ok(serverClosed, 'server should been closed immediately');
    })
  );
  server.close(
    common.mustSucceed(() => {
      serverClosed = true;
    })
  );
})().then(common.mustCall());
