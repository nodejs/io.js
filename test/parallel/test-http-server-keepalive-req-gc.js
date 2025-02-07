// Flags: --expose-gc
'use strict';
const common = require('../common');
const { onGC } = require('../common/gc');
const { createServer } = require('http');
const { connect } = require('net');

// Make sure that for HTTP keepalive requests, the req object can be
// garbage collected once the request is finished.
// Refs: https://github.com/nodejs/node/issues/9668

let client;
const server = createServer(common.mustCall((req, res) => {
  onGC(req, { ongc: common.mustCall(() => { server.close(); }) });
  req.resume();
  req.on('end', common.mustCall(() => {
    setImmediate(async () => {
      client.end();
      await globalThis.gc({ type: 'major', execution: 'async' });
      await globalThis.gc({ type: 'major', execution: 'async' });
    });
  }));
  res.end('hello world');
}));

server.listen(0, common.mustCall(() => {
  client = connect(server.address().port);

  const req = [
    'POST / HTTP/1.1',
    `Host: localhost:${server.address().port}`,
    'Connection: keep-alive',
    'Content-Length: 11',
    '',
    'hello world',
    '',
  ].join('\r\n');

  client.write(req);
  client.unref();
}));
