'use strict';
const common = require('../common');
const { checkSupportReusePort, options } = require('../common/udp');
const assert = require('assert');
const child_process = require('child_process');
const dgram = require('dgram');

if (!process.env.isWorker) {
  checkSupportReusePort().then(() => {
    const socket = dgram.createSocket(options);
    socket.bind(0, common.mustCall(() => {
      const port = socket.address().port;
      const workerOptions = { env: { ...process.env, isWorker: 1, port } };
      let count = 2;
      for (let i = 0; i < 2; i++) {
        const worker = child_process.fork(__filename, workerOptions);
        worker.on('exit', common.mustCall((code) => {
          assert.strictEqual(code, 0);
          if (--count === 0) {
            socket.close();
          }
        }));
      }
    }));
  }, () => {
    common.skip('The `reusePort` is not supported');
  });
  return;
}

const socket = dgram.createSocket(options);

socket.bind(+process.env.port, common.mustCall(() => {
  socket.close();
}));
