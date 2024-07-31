// Flags: --expose-internals
'use strict';
const common = require('../common');
const assert = require('assert');
const net = require('net');
const cluster = require('cluster');
const { internalBinding } = require('internal/test/binding');
const { TCP, constants: TCPConstants } = internalBinding('tcp_wrap');

// Test if the worker can listen with handle successfully
if (cluster.isPrimary) {
  cluster.fork();
} else {
  const handle = new TCP(TCPConstants.SOCKET);
  const errno = handle.bind('0.0.0.0', 0);
  assert.strictEqual(errno, 0);
  // Execute _listen2 instead of cluster._getServer in listenInCluster
  net.createServer().listen(handle, common.mustCall(() => {
    process.exit(0);
  }));
}
