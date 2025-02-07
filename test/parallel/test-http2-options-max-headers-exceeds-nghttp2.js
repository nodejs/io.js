// Flags: --expose-internals
'use strict';

const common = require('../common');
if (!common.hasCrypto) common.skip('missing crypto');
const h2 = require('http2');
const assert = require('assert');
const { ServerHttp2Session } = require('internal/http2/core');

const server = h2.createServer();

server.on('stream', common.mustNotCall());
server.on('error', common.mustNotCall());

server.listen(0, common.mustCall(() => {

  // Setting the maxSendHeaderBlockLength > nghttp2 threshold
  // cause a 'sessionError' and no memory leak when session destroy
  const options = {
    maxSendHeaderBlockLength: 100000
  };

  const client = h2.connect(`http://localhost:${server.address().port}`,
                            options);
  client.on('error', common.expectsError({
    code: 'ERR_HTTP2_SESSION_ERROR',
    name: 'Error',
    message: 'Session closed with error code 9'
  }));

  const req = client.request({
    // Greater than 65536 bytes
    'test-header': 'A'.repeat(90000)
  });
  req.on('response', common.mustNotCall());

  req.on('close', common.mustCall(() => {
    client.close();
    server.close();
  }));

  req.on('error', common.expectsError({
    code: 'ERR_HTTP2_SESSION_ERROR',
    name: 'Error',
    message: 'Session closed with error code 9'
  }));
  req.end();
}));

{
  const options = {
    maxSendHeaderBlockLength: 100000,
  };

  const server = h2.createServer(options);

  server.on('error', common.mustNotCall());
  server.on(
    'session',
    common.mustCall((session) => {
      assert.strictEqual(session instanceof ServerHttp2Session, true);
      session.on('close', common.mustCall(() => {
        server.close();
      }));
    }),
  );
  server.on(
    'stream',
    common.mustCall((stream) => {
      stream.additionalHeaders({
        // Greater than 65536 bytes
        'test-header': 'A'.repeat(90000),
      });
      stream.respond();
      stream.end();
    }),
  );

  server.on(
    'sessionError',
    common.mustCall((err, session) => {
      assert.strictEqual(err.code, 'ERR_HTTP2_SESSION_ERROR');
      assert.strictEqual(err.name, 'Error');
      assert.strictEqual(err.message, 'Session closed with error code 9');
      assert.strictEqual(session instanceof ServerHttp2Session, true);
    }),
  );

  server.listen(
    0,
    common.mustCall(() => {
      const client = h2.connect(`http://localhost:${server.address().port}`);
      client.on('error', common.mustNotCall());

      const req = client.request();
      req.on('response', common.mustNotCall());
      req.on('error', common.mustNotCall());
      req.end();
    }),
  );
}
