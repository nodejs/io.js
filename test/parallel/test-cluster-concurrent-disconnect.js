'use strict';

// Ref: https://github.com/nodejs/node/issues/32106

const common = require('../common');

const assert = require('assert');
const cluster = require('cluster');
const os = require('os');

if (cluster.isPrimary) {
  const workers = [];
  const numCPUs = os.availableParallelism();
  let waitOnline = numCPUs;
  for (let i = 0; i < numCPUs; i++) {
    const worker = cluster.fork();
    workers[i] = worker;
    worker.once('online', common.mustCall(() => {
      if (--waitOnline === 0)
        for (const worker of workers)
          if (worker.isConnected())
            worker.send(i % 2 ? 'disconnect' : 'destroy');
    }));

    // These errors can occur due to the nature of the test, we might be trying
    // to send messages when the worker is disconnecting.
    worker.on('error', (err) => {
      assert.strictEqual(err.syscall, 'write');
      if (common.isMacOS) {
        assert(['EPIPE', 'ENOTCONN'].includes(err.code), err);
      } else {
        assert(['EPIPE', 'ECONNRESET'].includes(err.code), err);
      }
    });

    worker.once('disconnect', common.mustCall(() => {
      for (const worker of workers)
        if (worker.isConnected())
          worker.send('disconnect');
    }));

    worker.once('exit', common.mustCall((code, signal) => {
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    }));
  }
} else {
  process.on('message', (msg) => {
    if (cluster.worker.isConnected())
      cluster.worker[msg]();
  });
}
