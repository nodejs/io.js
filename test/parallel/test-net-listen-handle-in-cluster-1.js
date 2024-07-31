'use strict';
const common = require('../common');
const assert = require('assert');
const net = require('net');
const cluster = require('cluster');

// Test if the worker can listen with handle successfully
if (cluster.isPrimary) {
  const worker = cluster.fork();
  const server = net.createServer();
  worker.on('online', common.mustCall(() => {
    server.listen(common.mustCall(() => {
      // Send the server to worker
      worker.send(null, server);
    }));
  }));
  worker.on('exit', common.mustCall(() => {
    server.close();
  }));
} else {
  // The `got` function of net.Server will create a TCP server by listen(handle)
  // See lib/internal/child_process.js
  process.on('message', common.mustCall((_, server) => {
    assert.strictEqual(server instanceof net.Server, true);
    process.exit(0);
  }));
}
