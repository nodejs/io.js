'use strict';
const common = require('../common');
const dgram = require('dgram');

// Do not emit error event in callback which is called by lookup when socket is closed
const socket = dgram.createSocket({
  type: 'udp4',
  lookup: (...args) => {
    // Call lookup callback after 1s
    setTimeout(() => {
      args.at(-1)(new Error('an error'));
    }, 1000);
  }
});

socket.on('error', common.mustNotCall());
socket.bind(12345, 'localhost');
// Close the socket before calling DNS lookup callback
socket.close();
