'use strict';

// Flags: --no-network-family-autoselection

const common = require('../common');
const { createMockedLookup } = require('../common/dns');

const assert = require('assert');
const { createConnection, createServer } = require('net');

// Test that IPV4 is NOT reached if IPV6 is not reachable and the option has been disabled via command line
{
  const ipv4Server = createServer((socket) => {
    socket.on('data', common.mustCall(() => {
      socket.write('response-ipv4');
      socket.end();
    }));
  });

  ipv4Server.listen(0, '127.0.0.1', common.mustCall(() => {
    const port = ipv4Server.address().port;

    const connection = createConnection({
      host: 'example.org',
      port,
      lookup: createMockedLookup('::1', '127.0.0.1'),
    });

    connection.on('ready', common.mustNotCall());
    connection.on('error', common.mustCall((error) => {
      assert.strictEqual(connection.autoSelectFamilyAttemptedAddresses, undefined);

      if (common.hasIPv6) {
        assert.strictEqual(error.code, 'ECONNREFUSED');
        assert.strictEqual(error.message, `connect ECONNREFUSED ::1:${port}`);
      } else if (error.code === 'EAFNOSUPPORT') {
        assert.strictEqual(error.message, `connect EAFNOSUPPORT ::1:${port} - Local (undefined:undefined)`);
      } else if (error.code === 'EUNATCH') {
        assert.strictEqual(error.message, `connect EUNATCH ::1:${port} - Local (:::0)`);
      } else {
        assert.strictEqual(error.code, 'EADDRNOTAVAIL');
        assert.strictEqual(error.message, `connect EADDRNOTAVAIL ::1:${port} - Local (:::0)`);
      }

      ipv4Server.close();
    }));
  }));
}
