'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const http2 = require('http2');

const settings = { enableConnectProtocol: true };
const server = http2.createServer({ settings });
server.on('stream', common.mustNotCall());
server.on('session', common.mustCall((session) => {
  // This will force the connection to close because once extended connect
  // is on, it cannot be turned off. The server is behaving badly.
  session.settings({ enableConnectProtocol: false });
}));

server.listen(0, common.mustCall(() => {
  const client = http2.connect(`http://localhost:${server.address().port}`);
  client.on('remoteSettings', common.mustCall((settings) => {
    assert(settings.enableConnectProtocol);
    const req = client.request({
      ':method': 'CONNECT',
      ':protocol': 'foo'
    });
    req.on('error', common.mustCall(() => {
      server.close();
    }));
  }));

  client.on('error', common.expectsError({
    code: 'ERR_HTTP2_ERROR',
    name: 'Error',
    message: 'Protocol error'
  }));
}));
