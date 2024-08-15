'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const http2 = require('http2');
const { duplexPair } = require('stream');
const { Worker, isMainThread } = require('worker_threads');

// This is a variant of test-http2-generic-streams-sendfile for checking
// that Workers can be terminated during a .respondWithFile() operation.

if (isMainThread) {
  return new Worker(__filename);
}

{
  const server = http2.createServer();
  server.on('stream', common.mustCall((stream, headers) => {
    stream.respondWithFile(process.execPath);  // Use a large-ish file.
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

  req.on('data', common.mustCall(() => process.exit()));
  req.on('end', common.mustNotCall());
  req.end();
}
