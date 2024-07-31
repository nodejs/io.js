'use strict';

const common = require('../common');
const { createMockedLookup } = require('../common/dns');

const assert = require('assert');
const { createConnection, createServer } = require('net');

// Test that happy eyeballs algorithm is properly implemented when a A record is returned first.
if (common.hasIPv6) {
  const ipv4Server = createServer((socket) => {
    socket.on('data', common.mustCall(() => {
      socket.write('response-ipv4');
      socket.end();
    }));
  });

  const ipv6Server = createServer((socket) => {
    socket.on('data', common.mustNotCall(() => {
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
        lookup: createMockedLookup('127.0.0.1', '::1'),
        autoSelectFamily: true,
      });

      let response = '';
      connection.setEncoding('utf-8');

      connection.on('data', (chunk) => {
        response += chunk;
      });

      connection.on('end', common.mustCall(() => {
        assert.strictEqual(response, 'response-ipv4');
        ipv4Server.close();
        ipv6Server.close();
      }));

      connection.write('request');
    }));
  }));
}
