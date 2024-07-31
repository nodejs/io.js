'use strict';
const common = require('../common');
const http = require('http');
const assert = require('assert');
const { kConnectionsCheckingInterval } = require('_http_server');

let i = 0;
let timer;
const server = http.createServer();
server.on('listening', common.mustCall(() => {
  // If there was a timer, it must be destroyed
  if (timer) {
    assert.ok(timer._destroyed);
  }
  // Save the last timer
  timer = server[kConnectionsCheckingInterval];
  if (++i === 2) {
    server.close(() => {
      assert.ok(timer._destroyed);
    });
  }
}, 2));
server.emit('listening');
server.emit('listening');
