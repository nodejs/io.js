'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const http2 = require('http2');
const fs = require('fs');
const { duplexPair } = require('stream');

{
  const server = http2.createServer();
  server.on('stream', common.mustCall((stream, headers) => {
    stream.respondWithFile(__filename);
  }));

  const [ clientSide, serverSide ] = duplexPair();
  server.emit('connection', serverSide);

  const client = http2.connect('http://localhost:80', {
    createConnection: common.mustCall(() => clientSide)
  });

  const req = client.request();

  req.on('response', common.mustCall((headers) => {
    assert.strictEqual(headers[':status'], 200);
  }));

  req.setEncoding('utf8');
  let data = '';
  req.on('data', (chunk) => data += chunk);
  req.on('end', common.mustCall(() => {
    assert.strictEqual(data, fs.readFileSync(__filename, 'utf8'));
    clientSide.destroy();
    clientSide.end();
  }));
  req.end();
}
