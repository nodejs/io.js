'use strict';
const common = require('../common');
const dgram = require('dgram');
const cluster = require('cluster');

if (cluster.isPrimary) {
  cluster.fork();
} else {
  // When the socket attempts to bind, it requests a handle from the cluster.
  // Force the cluster to send back an error code.
  const socket = dgram.createSocket('udp4');
  cluster._getServer = function(self, options, callback) {
    socket.close(() => { cluster.worker.disconnect(); });
    callback(-1);
  };
  socket.on('error', common.mustNotCall());
  socket.bind();
}
