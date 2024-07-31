'use strict';
const common = require('../common');
const fs = require('fs');
const net = require('net');

const tmpdir = require('../common/tmpdir');
tmpdir.refresh();

const server = net.createServer().listen(common.PIPE, common.mustCall(() => {
  // The process should not crash
  // See https://github.com/nodejs/node/issues/52159
  fs.readdirSync(tmpdir.path, { recursive: true });
  server.close();
}));
