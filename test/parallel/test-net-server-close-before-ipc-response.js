'use strict';

const common = require('../common');
const net = require('net');
const cluster = require('cluster');

// Process should exit
if (cluster.isPrimary) {
  cluster.fork();
} else {
  const send = process.send;
  process.send = function(message) {
    // listenOnPrimaryHandle in net.js should call handle.close()
    if (message.act === 'close') {
      setImmediate(() => {
        process.disconnect();
      });
    }
    return send.apply(this, arguments);
  };
  net.createServer().listen(0, common.mustNotCall()).close();
}
