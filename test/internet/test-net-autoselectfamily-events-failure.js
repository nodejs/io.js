'use strict';

const common = require('../common');
const { addresses: { INET6_IP, INET4_IP } } = require('../common/internet');
const { createMockedLookup } = require('../common/dns');

const assert = require('assert');
const { createConnection } = require('net');

//
// When testing this is MacOS, remember that the last connection will have no timeout at Node.js
// level but only at operating system one.
//
// The default for MacOS is 75 seconds. It can be changed by doing:
//
// sudo sysctl net.inet.tcp.keepinit=VALUE_IN_MS
//

// Test that all failure events are emitted when trying a single IP (which means autoselectfamily is bypassed)
{
  const pass = common.mustCallAtLeast(1);

  const connection = createConnection({
    host: 'example.org',
    port: 10,
    lookup: createMockedLookup(INET4_IP),
    autoSelectFamily: true,
    autoSelectFamilyAttemptTimeout: 10,
  });

  connection.on('connectionAttempt', (address, port, family) => {
    assert.strictEqual(address, INET4_IP);
    assert.strictEqual(port, 10);
    assert.strictEqual(family, 4);

    pass();
  });

  connection.on('connectionAttemptFailed', (address, port, family, error) => {
    assert.strictEqual(address, INET4_IP);
    assert.strictEqual(port, 10);
    assert.strictEqual(family, 4);

    assert.ok(
      error.code.match(/ECONNREFUSED|ENETUNREACH|EHOSTUNREACH|ETIMEDOUT/),
      `Received unexpected error code ${error.code}`,
    );

    pass();
  });

  connection.on('ready', () => {
    pass();
    connection.destroy();
  });

  connection.on('error', () => {
    pass();
    connection.destroy();
  });

  setTimeout(() => {
    pass();
    process.exit(0);
  }, 5000).unref();

}

// Test that all events are emitted when trying multiple IPs
{
  const pass = common.mustCallAtLeast(1);

  const connection = createConnection({
    host: 'example.org',
    port: 10,
    lookup: createMockedLookup(INET6_IP, INET4_IP),
    autoSelectFamily: true,
    autoSelectFamilyAttemptTimeout: 10,
  });

  const addresses = [
    { address: INET6_IP, port: 10, family: 6 },
    { address: INET6_IP, port: 10, family: 6 },
    { address: INET4_IP, port: 10, family: 4 },
    { address: INET4_IP, port: 10, family: 4 },
  ];

  connection.on('connectionAttempt', (address, port, family) => {
    const expected = addresses.shift();

    assert.strictEqual(address, expected.address);
    assert.strictEqual(port, expected.port);
    assert.strictEqual(family, expected.family);

    pass();
  });

  connection.on('connectionAttemptFailed', (address, port, family, error) => {
    const expected = addresses.shift();

    assert.strictEqual(address, expected.address);
    assert.strictEqual(port, expected.port);
    assert.strictEqual(family, expected.family);

    assert.ok(
      error.code.match(/ECONNREFUSED|ENETUNREACH|EHOSTUNREACH|ETIMEDOUT/),
      `Received unexpected error code ${error.code}`,
    );

    pass();
  });

  connection.on('ready', () => {
    pass();
    connection.destroy();
  });

  connection.on('error', () => {
    pass();
    connection.destroy();
  });

  setTimeout(() => {
    pass();
    process.exit(0);
  }, 5000).unref();

}
