'use strict';

const { PIPE, mustCall } = require('../common');
const tmpdir = require('../common/tmpdir');
const { test } = require('node:test');
const fs = require('node:fs');
const net = require('node:net');

test('readdir should not recurse into Unix domain sockets', (t, done) => {
  tmpdir.refresh();
  const server = net.createServer().listen(PIPE, mustCall(() => {
    // The process should not crash
    // See https://github.com/nodejs/node/issues/52159
    fs.readdirSync(tmpdir.path, { recursive: true });
    server.close();
    done();
  }));
});
