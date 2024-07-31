'use strict';

const common = require('../common');
const { createMockedLookup } = require('../common/dns');

const assert = require('assert');
const { createConnection, createServer, setDefaultAutoSelectFamily } = require('net');

const autoSelectFamilyAttemptTimeout = common.defaultAutoSelectFamilyAttemptTimeout;

// Test that IPV4 is reached by default if IPV6 is not reachable and the default is enabled
{
  const ipv4Server = createServer((socket) => {
    socket.on('data', common.mustCall(() => {
      socket.write('response-ipv4');
      socket.end();
    }));
  });

  ipv4Server.listen(0, '127.0.0.1', common.mustCall(() => {
    setDefaultAutoSelectFamily(true);

    const connection = createConnection({
      host: 'example.org',
      port: ipv4Server.address().port,
      lookup: createMockedLookup('::1', '127.0.0.1'),
      autoSelectFamilyAttemptTimeout,
    });

    let response = '';
    connection.setEncoding('utf-8');

    connection.on('data', (chunk) => {
      response += chunk;
    });

    connection.on('end', common.mustCall(() => {
      assert.strictEqual(response, 'response-ipv4');
      ipv4Server.close();
    }));

    connection.write('request');
  }));
}

// Test that IPV4 is not reached by default if IPV6 is not reachable and the default is disabled
{
  const ipv4Server = createServer((socket) => {
    socket.on('data', common.mustCall(() => {
      socket.write('response-ipv4');
      socket.end();
    }));
  });

  ipv4Server.listen(0, '127.0.0.1', common.mustCall(() => {
    setDefaultAutoSelectFamily(false);

    const port = ipv4Server.address().port;

    const connection = createConnection({
      host: 'example.org',
      port,
      lookup: createMockedLookup('::1', '127.0.0.1'),
    });

    connection.on('ready', common.mustNotCall());
    connection.on('error', common.mustCall((error) => {
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
