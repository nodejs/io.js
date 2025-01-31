// Flags: --expose-gc
'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const http2 = require('http2');
const { duplexPair } = require('stream');

// Make sure the Http2Stream destructor works, since we don't clean the
// stream up like we would otherwise do.
process.on('exit', globalThis.gc);

{
  const [ clientSide, serverSide ] = duplexPair();

  let serverSideHttp2Stream;
  let serverSideHttp2StreamDestroyed = false;
  const server = http2.createServer();
  server.on('stream', common.mustCall((stream, headers) => {
    serverSideHttp2Stream = stream;
    stream.respond({
      'content-type': 'text/html',
      ':status': 200
    });

    const originalWrite = serverSide._write;
    serverSide._write = (buf, enc, cb) => {
      if (serverSideHttp2StreamDestroyed) {
        serverSide.destroy();
        serverSide.write = () => {};
      } else {
        setImmediate(() => {
          originalWrite.call(serverSide, buf, enc, () => setImmediate(cb));
        });
      }
    };

    // Enough data to fit into a single *session* window,
    // not enough data to fit into a single *stream* window.
    stream.write(Buffer.alloc(40000));
  }));

  server.emit('connection', serverSide);

  const client = http2.connect('http://localhost:80', {
    createConnection: common.mustCall(() => clientSide)
  });

  const req = client.request({ ':path': '/' });

  req.on('response', common.mustCall((headers) => {
    assert.strictEqual(headers[':status'], 200);
  }));

  req.on('data', common.mustCallAtLeast(() => {
    if (!serverSideHttp2StreamDestroyed) {
      serverSideHttp2Stream.destroy();
      serverSideHttp2StreamDestroyed = true;
    }
  }));
}
