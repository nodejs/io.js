'use strict';
// Tests that calling unref() on Http2Session:
// (1) Prevents it from keeping the process alive
// (2) Doesn't crash

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const http2 = require('http2');
const Countdown = require('../common/countdown');
const { duplexPair } = require('stream');

const server = http2.createServer();
const [ clientSide, serverSide ] = duplexPair();

const counter = new Countdown(3, () => server.unref());

// 'session' event should be emitted 3 times:
// - the vanilla client
// - the destroyed client
// - manual 'connection' event emission with generic Duplex stream
server.on('session', common.mustCallAtLeast((session) => {
  counter.dec();
  session.unref();
}, 3));

server.listen(0, common.mustCall(() => {
  const port = server.address().port;

  // unref new client
  {
    const client = http2.connect(`http://localhost:${port}`);
    client.unref();
  }

  // Unref destroyed client
  {
    const client = http2.connect(`http://localhost:${port}`);

    client.on('connect', common.mustCall(() => {
      client.destroy();
      client.unref();
    }));
  }

  // Unref destroyed client
  {
    const client = http2.connect(`http://localhost:${port}`, {
      createConnection: common.mustCall(() => clientSide)
    });

    client.on('connect', common.mustCall(() => {
      client.destroy();
      client.unref();
    }));
  }
}));
server.emit('connection', serverSide);
