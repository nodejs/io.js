'use strict';
const net = require('net');

const options = { port: 0, reusePort: true };

function checkSupportReusePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer().listen(options);
    server.on('listening', () => {
      server.close(resolve);
    });
    server.on('error', (err) => {
      console.log('The `reusePort` option is not supported:', err.message);
      server.close();
      reject(err);
    });
  });
}

module.exports = {
  checkSupportReusePort,
  options,
};
