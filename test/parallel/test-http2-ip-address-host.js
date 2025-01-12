'use strict';

const common = require('../common'); if (!common.hasCrypto) { common.skip('missing crypto'); };
const assert = require('assert');
const fixtures = require('../common/fixtures');
const h2 = require('http2');

function loadKey(keyname) {
  return fixtures.readKey(keyname, 'binary');
}

const key = loadKey('agent8-key.pem');
const cert = fixtures.readKey('agent8-cert.pem');

const server = h2.createSecureServer({ key, cert });
server.on('stream', common.mustCall((stream) => {
  const session = stream.session;
  assert.strictEqual(session.servername, undefined);
  stream.respond({ 'content-type': 'application/json' });
  stream.end(JSON.stringify({
    servername: session.servername,
    originSet: session.originSet
  })
  );
}, 2));
server.on('close', common.mustCall());
server.listen(0, common.mustCall(async () => {
  await new Promise((resolve) => {
    const client = h2.connect(`https://127.0.0.1:${server.address().port}`,
                              { rejectUnauthorized: false });
    const req = client.request();
    let data = '';
    req.setEncoding('utf8');
    req.on('data', (d) => data += d);
    req.on('end', common.mustCall(() => {
      const originSet = req.session.originSet;
      assert.strictEqual(originSet[0], `https://127.0.0.1:${server.address().port}`);
      client.close();
      resolve();
    }));
  });

  await new Promise((resolve) => {
    // Test with IPv6 address
    const client = h2.connect(`https://[::1]:${server.address().port}`,
                              { rejectUnauthorized: false });
    const req = client.request();
    let data = '';
    req.setEncoding('utf8');
    req.on('data', (d) => data += d);
    req.on('end', common.mustCall(() => {
      const originSet = req.session.originSet;
      assert.strictEqual(originSet[0], `https://[::1]:${server.address().port}`);
      client.close();
      resolve();
    }));
  });
  server.close();
}));
