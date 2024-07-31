'use strict';
const common = require('../common');
const assert = require('assert');
const net = require('net');

if (!common.isLinux) common.skip();

const path = '\0abstract';
const message = /can not set readableAll or writableAllt to true when path is abstract unix socket/;

assert.throws(() => {
  const server = net.createServer(common.mustNotCall());
  server.listen({
    path,
    readableAll: true
  });
}, message);

assert.throws(() => {
  const server = net.createServer(common.mustNotCall());
  server.listen({
    path,
    writableAll: true
  });
}, message);

assert.throws(() => {
  const server = net.createServer(common.mustNotCall());
  server.listen({
    path,
    readableAll: true,
    writableAll: true
  });
}, message);
