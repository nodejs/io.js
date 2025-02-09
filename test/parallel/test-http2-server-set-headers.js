'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const http2 = require('http2');
const body =
  '<html><head></head><body><h1>this is some data</h2></body></html>';

const server = http2.createServer((req, res) => {
  res.setHeaders(new Map([
    ['foobar', 'baz'],
    ['X-POWERED-BY', 'node-test'],
    ['connection', 'connection-test'],
  ]));
  res.end(body);
});

server.listen(0, common.mustCall(() => {
  const client = http2.connect(`http://localhost:${server.address().port}`);
  const headers = { ':path': '/' };
  const req = client.request(headers);
  req.setEncoding('utf8');
  req.on('response', common.mustCall(function(headers) {
    assert.strictEqual(headers.foobar, 'baz');
    assert.strictEqual(headers['x-powered-by'], 'node-test');
  }));

  let data = '';
  req.on('data', (d) => data += d);
  req.on('end', () => {
    assert.strictEqual(body, data);
    server.close();
    client.close();
  });
  req.end();
}));

const compatMsg = 'The provided connection header is not valid, ' +
                  'the value will be dropped from the header and ' +
                  'will never be in use.';

common.expectWarning('UnsupportedWarning', compatMsg);

server.on('error', common.mustNotCall());
