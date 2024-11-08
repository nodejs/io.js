'use strict';
const common = require('../common');
const assert = require('assert');
const net = require('net');
const fs = require('fs');
const tmpdir = require('../common/tmpdir');
tmpdir.refresh();

// Test UV_PIPE_NO_TRUNCATE

// See pipe_overlong_path in https://github.com/libuv/libuv/blob/master/test/test-pipe-bind-error.c
if (common.isWindows) {
  common.skip('UV_PIPE_NO_TRUNCATE is not supported on window');
}

// See https://github.com/libuv/libuv/issues/4231
const pipePath = `${tmpdir.path}/${'x'.repeat(10000)}.sock`;

const server = net.createServer()
  .listen(pipePath)
  // It may work on some operating systems
  .on('listening', () => {
    // The socket file must exist
    assert.ok(fs.existsSync(pipePath));
    const socket = net.connect(pipePath, common.mustCall(() => {
      socket.destroy();
      server.close();
    }));
  })
  .on('error', (error) => {
    assert.ok(error.code === 'EINVAL', error.message);
    net.connect(pipePath)
      .on('error', common.mustCall((error) => {
        assert.ok(error.code === 'EINVAL', error.message);
      }));
  });
