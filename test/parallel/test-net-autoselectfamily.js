'use strict';

const common = require('../common');
const { createMockedLookup } = require('../common/dns');

const assert = require('assert');
const { createConnection, createServer } = require('net');

// Test that happy eyeballs algorithm is properly implemented.

// Purposely not using setDefaultAutoSelectFamilyAttemptTimeout here to test the
// parameter is correctly used in options.

// Some of the machines in the CI need more time to establish connection
const autoSelectFamilyAttemptTimeout = common.defaultAutoSelectFamilyAttemptTimeout;

// Test that IPV4 is reached if IPV6 is not reachable
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
      port: port,
      lookup: createMockedLookup('::1', '127.0.0.1'),
      autoSelectFamily: true,
      autoSelectFamilyAttemptTimeout,
    });

    let response = '';
    connection.setEncoding('utf-8');

    connection.on('ready', common.mustCall(() => {
      assert.deepStrictEqual(connection.autoSelectFamilyAttemptedAddresses, [`::1:${port}`, `127.0.0.1:${port}`]);
    }));

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

// Test that only the last successful connection is established.
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
      port: port,
      lookup: createMockedLookup(
        '2606:4700::6810:85e5', '2606:4700::6810:84e5', '::1',
        '104.20.22.46', '104.20.23.46', '127.0.0.1',
      ),
      autoSelectFamily: true,
      autoSelectFamilyAttemptTimeout,
    });

    let response = '';
    connection.setEncoding('utf-8');

    connection.on('ready', common.mustCall(() => {
      assert.deepStrictEqual(
        connection.autoSelectFamilyAttemptedAddresses,
        [
          `2606:4700::6810:85e5:${port}`,
          `104.20.22.46:${port}`,
          `2606:4700::6810:84e5:${port}`,
          `104.20.23.46:${port}`,
          `::1:${port}`,
          `127.0.0.1:${port}`,
        ]
      );
    }));

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

// Test that IPV4 is NOT reached if IPV6 is reachable
if (common.hasIPv6) {
  const ipv4Server = createServer((socket) => {
    socket.on('data', common.mustNotCall(() => {
      socket.write('response-ipv4');
      socket.end();
    }));
  });

  const ipv6Server = createServer((socket) => {
    socket.on('data', common.mustCall(() => {
      socket.write('response-ipv6');
      socket.end();
    }));
  });

  ipv4Server.listen(0, '127.0.0.1', common.mustCall(() => {
    const port = ipv4Server.address().port;

    ipv6Server.listen(port, '::1', common.mustCall(() => {
      const connection = createConnection({
        host: 'example.org',
        port,
        lookup: createMockedLookup('::1', '127.0.0.1'),
        autoSelectFamily: true,
        autoSelectFamilyAttemptTimeout,
      });

      let response = '';
      connection.setEncoding('utf-8');

      connection.on('ready', common.mustCall(() => {
        assert.deepStrictEqual(connection.autoSelectFamilyAttemptedAddresses, [`::1:${port}`]);
      }));

      connection.on('data', (chunk) => {
        response += chunk;
      });

      connection.on('end', common.mustCall(() => {
        assert.strictEqual(response, 'response-ipv6');
        ipv4Server.close();
        ipv6Server.close();
      }));

      connection.write('request');
    }));
  }));
}

// Test that when all errors are returned when no connections succeeded
{
  const connection = createConnection({
    host: 'example.org',
    port: 10,
    lookup: createMockedLookup('::1', '127.0.0.1'),
    autoSelectFamily: true,
    autoSelectFamilyAttemptTimeout,
  });

  connection.on('ready', common.mustNotCall());
  connection.on('error', common.mustCall((error) => {
    assert.deepStrictEqual(connection.autoSelectFamilyAttemptedAddresses, ['::1:10', '127.0.0.1:10']);
    assert.strictEqual(error.constructor.name, 'AggregateError');
    assert.strictEqual(error.errors.length, 2);

    const errors = error.errors.map((e) => e.message);
    assert.ok(errors.includes('connect ECONNREFUSED 127.0.0.1:10'));

    if (common.hasIPv6) {
      assert.ok(errors.includes('connect ECONNREFUSED ::1:10'));
    }
  }));
}

// Test that the option can be disabled
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
      autoSelectFamily: false,
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
