'use strict';
const common = require('../common');
if (common.isWindows)
  common.skip('dgram clustering is currently not supported on windows.');

const { checkSupportReusePort, options } = require('../common/udp');
const assert = require('assert');
const cluster = require('cluster');
const dgram = require('dgram');

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

// Test if the worker requests the main process to create a socket
cluster._getServer = common.mustNotCall();

const socket1 = dgram.createSocket(options);
const socket2 = dgram.createSocket(options);

socket1.bind(0, () => {
  socket2.bind(socket1.address().port, () => {
    socket1.close(close);
    socket2.close(close);
  });
});
