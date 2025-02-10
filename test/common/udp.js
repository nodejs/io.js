'use strict';
const { createSocket } = require('dgram');

const options = { type: 'udp4', reusePort: true };

function checkSupportReusePort() {
  const { promise, resolve, reject } = Promise.withResolvers();
  const socket = createSocket(options);
  socket.bind(0);
  socket.on('listening', () => {
    socket.close(resolve);
  });
  socket.on('error', (err) => {
    console.log('The `reusePort` option is not supported:', err.message);
    socket.close();
    reject(err);
  });
  return promise;
}

module.exports = {
  checkSupportReusePort,
  options,
};
