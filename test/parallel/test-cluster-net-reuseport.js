'use strict';
const common = require('../common');

const { checkSupportReusePort, options } = require('../common/net');
const assert = require('assert');
const cluster = require('cluster');
const net = require('net');

if (cluster.isPrimary) {
  checkSupportReusePort().then(() => {
    cluster.fork().on('exit', common.mustCall((code) => {
      assert.strictEqual(code, 0);
    }));
  }, () => {
    common.skip('The `reusePort` option is not supported');
  });
  return;
}

let waiting = 2;
function close() {
  if (--waiting === 0)
    cluster.worker.disconnect();
}

const server1 = net.createServer();
const server2 = net.createServer();

// Test if the worker requests the main process to create a socket
cluster._getServer = common.mustNotCall();

server1.listen(options, common.mustCall(() => {
  const port = server1.address().port;
  server2.listen({ ...options, port }, common.mustCall(() => {
    server1.close(close);
    server2.close(close);
  }));
}));
