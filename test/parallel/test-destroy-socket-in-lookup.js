'use strict';
const common = require('../common');
const net = require('net');

// Test that the process does not crash.
const socket = net.connect({
  port: 12345,
  host: 'localhost',
  // Make sure autoSelectFamily is true
  // so that lookupAndConnectMultiple is called.
  autoSelectFamily: true,
});
// DNS resolution fails or succeeds
socket.on('lookup', common.mustCall(() => {
  socket.destroy();
}));
