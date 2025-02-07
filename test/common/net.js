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

function hasMultiLocalhost() {
  const { internalBinding } = require('internal/test/binding');
  const { TCP, constants: TCPConstants } = internalBinding('tcp_wrap');
  const t = new TCP(TCPConstants.SOCKET);
  const ret = t.bind('127.0.0.2', 0);
  t.close();
  return ret === 0;
}

module.exports = {
  checkSupportReusePort,
  hasMultiLocalhost,
  options,
};
