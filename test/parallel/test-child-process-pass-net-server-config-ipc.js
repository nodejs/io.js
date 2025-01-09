'use strict';
const { mustCall } = require('../common');
const net = require('net');
const child_process = require('child_process');
const assert = require('assert');
// preset server config
const netServerConfig = {
  allowHalfOpen: true,
  pauseOnConnect: false,
  noDelay: false,
  keepAliveInitialDelay: 1000,
  keepAlive: true,
};

if (process.argv[2] !== 'child') {
  const childProcess = child_process.fork(__filename, ['child']);
  const server = net.createServer(netServerConfig);
  server.listen(0, mustCall(() => {
    childProcess.send(null, server);
  }),
  );
  childProcess.on('exit', mustCall(() => {
    process.exit();
  }));
} else {
  process.on(
    'message',
    mustCall((msg, handle) => {
      // Pass through all of net.Server config from parent process
      assert.strictEqual(handle.allowHalfOpen, netServerConfig.allowHalfOpen);
      assert.strictEqual(handle.pauseOnConnect, netServerConfig.pauseOnConnect);
      assert.strictEqual(handle.noDelay, netServerConfig.noDelay);
      assert.strictEqual(
        handle.keepAliveInitialDelay,
        ~~(netServerConfig.keepAliveInitialDelay / 1000),
      );
      assert.strictEqual(handle.keepAlive, netServerConfig.keepAlive);
      process.exit();
    }),
  );
}
