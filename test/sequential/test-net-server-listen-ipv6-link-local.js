'use strict';
const common = require('../common');
const assert = require('assert');
const net = require('net');
const dns = require('dns');
const { mock } = require('node:test');

if (!common.hasIPv6) {
  common.printSkipMessage('IPv6 support is required for this test');
  return;
}

// Test on IPv6 Server, dns.lookup throws an error
{
  mock.method(dns, 'lookup', (hostname, options, callback) => {
    callback(new Error('Mocked error'));
  });
  const host = 'ipv6-link-local';

  const server = net.createServer();

  server.on('error', common.mustCall((e) => {
    assert.strictEqual(e.message, 'Mocked error');
  }));

  server.listen(common.PORT + 2, host);
}


// Test on IPv6 Server, server.listen throws an error
{
  mock.method(dns, 'lookup', (hostname, options, callback) => {
    if (hostname === 'ipv6-link-local') {
      callback(null, [{ address: 'fe80::1', family: 6 }]);
    } else {
      dns.lookup.wrappedMethod(hostname, options, callback);
    }
  });
  const host = 'ipv6-link-local';

  const server = net.createServer();

  server.on('error', common.mustCall((e) => {
    assert.strictEqual(e.address, 'fe80::1');
    assert.strictEqual(e.syscall, 'listen');
  }));

  server.listen(common.PORT + 2, host);
}

// Test on IPv6 Server, picks 127.0.0.1 between that and a bunch of link-local addresses
{

  mock.method(dns, 'lookup', (hostname, options, callback) => {
    if (hostname === 'ipv6-link-local-with-many-entries') {
      callback(null, [
        { address: 'fe80::1', family: 6 },
        { address: 'fe80::abcd:1234', family: 6 },
        { address: 'fe80::1ff:fe23:4567:890a', family: 6 },
        { address: 'fe80::200:5aee:feaa:20a2', family: 6 },
        { address: 'fe80::f2de:f1ff:fe2b:3c4b', family: 6 },
        { address: 'fe81::1', family: 6 },
        { address: 'fe82::abcd:1234', family: 6 },
        { address: 'fe83::1ff:fe23:4567:890a', family: 6 },
        { address: 'fe84::200:5aee:feaa:20a2', family: 6 },
        { address: 'fe85::f2de:f1ff:fe2b:3c4b', family: 6 },
        { address: 'fe86::1', family: 6 },
        { address: 'fe87::abcd:1234', family: 6 },
        { address: 'fe88::1ff:fe23:4567:890a', family: 6 },
        { address: 'fe89::200:5aee:feaa:20a2', family: 6 },
        { address: 'fe8a::f2de:f1ff:fe2b:3c4b', family: 6 },
        { address: 'fe8b::1', family: 6 },
        { address: 'fe8c::abcd:1234', family: 6 },
        { address: 'fe8d::1ff:fe23:4567:890a', family: 6 },
        { address: 'fe8e::200:5aee:feaa:20a2', family: 6 },
        { address: 'fe8f::f2de:f1ff:fe2b:3c4b', family: 6 },
        { address: 'fea0::1', family: 6 },
        { address: 'febf::abcd:1234', family: 6 },
        { address: 'febf:ffff:ffff:ffff:ffff:ffff:ffff:ffff', family: 6 },
        { address: '127.0.0.1', family: 4 },
      ]);
    } else {
      dns.lookup.wrappedMethod(hostname, options, callback);
    }
  });

  const host = 'ipv6-link-local-with-many-entries';

  const server = net.createServer();

  server.on('error', common.mustNotCall());

  server.listen(common.PORT + 3, host, common.mustCall(() => {
    const address = server.address();
    assert.strictEqual(address.address, '127.0.0.1');
    assert.strictEqual(address.port, common.PORT + 3);
    assert.strictEqual(address.family, 'IPv4');
    server.close();
  }));
}


// Test on IPv6 Server, picks ::1 because the other address is a link-local address
{

  const host = 'ipv6-link-local-with-double-entry';
  const validIpv6Address = '::1';

  mock.method(dns, 'lookup', (hostname, options, callback) => {
    if (hostname === 'ipv6-link-local-with-double-entry') {
      callback(null, [
        { address: 'fe80::1', family: 6 },
        { address: validIpv6Address, family: 6 },
      ]);
    } else {
      dns.lookup.wrappedMethod(hostname, options, callback);
    }
  });

  const server = net.createServer();

  server.on('error', common.mustNotCall());

  server.listen(common.PORT + 4, host, common.mustCall(() => {
    const address = server.address();
    assert.strictEqual(address.address, validIpv6Address);
    assert.strictEqual(address.port, common.PORT + 4);
    assert.strictEqual(address.family, 'IPv6');
    server.close();
  }));
}
