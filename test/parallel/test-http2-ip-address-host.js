'use strict';

const common = require('../common');
if (!common.hasCrypto) { common.skip('missing crypto'); };
const assert = require('assert');
const fixtures = require('../common/fixtures');
const h2 = require('http2');

function loadKey(keyname) {
  return fixtures.readKey(keyname, 'binary');
}

const key = loadKey('agent8-key.pem');
const cert = fixtures.readKey('agent8-cert.pem');

const server = h2.createSecureServer({ key, cert });
const hasIPv6 = common.hasIPv6;
const testCount = hasIPv6 ? 2 : 1;

server.on('stream', common.mustCall((stream) => {
  const session = stream.session;
  assert.strictEqual(session.servername, undefined);
  stream.respond({ 'content-type': 'application/json' });
  stream.end(JSON.stringify({
    servername: session.servername,
    originSet: session.originSet
  })
  );
}, testCount));

let done = 0;

server.listen(0, common.mustCall(() => {
  function handleRequest(url) {
    const client = h2.connect(url,
                              { rejectUnauthorized: false });
    const req = client.request();
    let data = '';
    req.setEncoding('utf8');
    req.on('data', (d) => data += d);
    req.on('end', common.mustCall(() => {
      const originSet = req.session.originSet;
      assert.strictEqual(originSet[0], url);
      client.close();
      if (++done === testCount) server.close();
    }));
  }

  const ipv4Url = `https://127.0.0.1:${server.address().port}`;
  const ipv6Url = `https://[::1]:${server.address().port}`;
  handleRequest(ipv4Url);
  if (hasIPv6) handleRequest(ipv6Url);
}));
