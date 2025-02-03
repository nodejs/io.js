'use strict';
const common = require('../common');
const cluster = require('cluster');
const http = require('http');

if (cluster.isPrimary) {
  cluster.fork();
} else {
  const server = http.createServer();
  server.maxConnections = 0;
  server.dropMaxConnection = true;
  // When dropMaxConnection is false, the main process will continue to
  // distribute the request to the child process, if true, the child will
  // close the connection directly and emit drop event.
  server.on('drop', common.mustCall((a) => {
    process.exit();
  }));
  server.listen(common.mustCall(() => {
    http.get(`http://localhost:${server.address().port}`).on('error', console.error);
  }));
}
