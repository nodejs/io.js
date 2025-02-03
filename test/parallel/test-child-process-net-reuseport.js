'use strict';
const common = require('../common');
const { checkSupportReusePort, options } = require('../common/net');
const assert = require('assert');
const child_process = require('child_process');
const net = require('net');

if (!process.env.isWorker) {
  checkSupportReusePort().then(() => {
    const server = net.createServer();
    server.listen(options, common.mustCall(() => {
      const port = server.address().port;
      const workerOptions = { env: { ...process.env, isWorker: 1, port } };
      let count = 2;
      for (let i = 0; i < 2; i++) {
        const worker = child_process.fork(__filename, workerOptions);
        worker.on('exit', common.mustCall((code) => {
          assert.strictEqual(code, 0);
          if (--count === 0) {
            server.close();
          }
        }));
      }
    }));
  }, () => {
    common.skip('The `reusePort` option is not supported');
  });
  return;
}

const server = net.createServer();

server.listen({ ...options, port: +process.env.port }, common.mustCall(() => {
  server.close();
}));
