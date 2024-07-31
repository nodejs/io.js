'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const http2 = require('http2');
const { duplexPair } = require('stream');

{
  const testData = '<h1>Hello World</h1>';
  const server = http2.createServer();
  server.on('stream', common.mustCall((stream, headers) => {
    stream.respond({
      'content-type': 'text/html',
      ':status': 200,
      'cookie': 'donotindex',
      'not-sensitive': 'foo',
      'sensitive': 'bar',
      // sensitiveHeaders entries are case-insensitive
      [http2.sensitiveHeaders]: ['Sensitive']
    });
    stream.end(testData);
  }));

  const [ clientSide, serverSide ] = duplexPair();
  server.emit('connection', serverSide);

  const client = http2.connect('http://localhost:80', {
    createConnection: common.mustCall(() => clientSide)
  });

  const req = client.request({ ':path': '/' });

  req.on('response', common.mustCall((headers) => {
    assert.strictEqual(headers[':status'], 200);
    assert.strictEqual(headers.cookie, 'donotindex');
    assert.deepStrictEqual(headers[http2.sensitiveHeaders],
                           ['cookie', 'sensitive']);
  }));

  req.on('end', common.mustCall(() => {
    clientSide.destroy();
    clientSide.end();
  }));
  req.resume();
  req.end();
}
