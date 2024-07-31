'use strict';
const common = require('../common');
const net = require('net');
const cluster = require('cluster');
const assert = require('assert');

if (cluster.isPrimary) {
  const worker = cluster.fork();
  worker.on('exit', common.mustCall((code) => {
    assert.ok(code === 0);
  }));
} else {
  const server = net.createServer();
  server.listen();
  try {
    // Currently, we can call `listen` twice in cluster worker,
    // if we can not call `listen` twice in the futrue,
    // just skip this test.
    server.listen();
  } catch (e) {
    console.error(e);
    process.exit(0);
  }
  let i = 0;
  process.on('internalMessage', (msg) => {
    if (msg.cmd === 'NODE_CLUSTER') {
      if (++i === 2) {
        setImmediate(() => {
          server.close(() => {
            process.disconnect();
          });
        });
      }
    }
  });
  // Must only call once
  server.on('listening', common.mustCall());
}
