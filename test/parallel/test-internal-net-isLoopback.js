// Flags: --expose-internals
'use strict';
require('../common');
const assert = require('assert');
const net = require('internal/net');

const loopback = [
  'localhost',
  '127.0.0.1',
  '127.0.0.255',
  '127.1.2.3',
  '[::1]',
  '[0:0:0:0:0:0:0:1]',
];

const loopbackNot = [
  'example.com',
  '192.168.1.1',
  '10.0.0.1',
  '255.255.255.255',
  '[2001:db8::1]',
  '[fe80::1]',
  '8.8.8.8',
];

for (const address of loopback) {
  assert.strictEqual(net.isLoopback(address), true);
}

for (const address of loopbackNot) {
  assert.strictEqual(net.isLoopback(address), false);
}
