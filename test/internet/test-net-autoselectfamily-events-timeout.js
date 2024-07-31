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
// Depending on the network, it might be impossible to obtain a timeout in 10ms,
// which is the minimum value allowed by network family autoselection.
// At the time of writing (Dec 2023), the network times out on local machine and in the Node CI,
// but it does not on GitHub actions runner.
// Therefore, after five seconds we just consider this test as passed.

// Test that if a connection attempt times out and the socket is destroyed before the
// next attempt starts then the process does not crash
{
  const connection = createConnection({
    host: 'example.org',
    port: 443,
    lookup: createMockedLookup(INET4_IP, INET6_IP),
    autoSelectFamily: true,
    autoSelectFamilyAttemptTimeout: 10,
  });

  const pass = common.mustCall();

  connection.on('connectionAttemptTimeout', (address, port, family) => {
    assert.strictEqual(address, INET4_IP);
    assert.strictEqual(port, 443);
    assert.strictEqual(family, 4);
    connection.destroy();
    pass();
  });

  connection.on('ready', () => {
    pass();
    connection.destroy();
  });

  setTimeout(() => {
    pass();
    process.exit(0);
  }, 5000).unref();
}
