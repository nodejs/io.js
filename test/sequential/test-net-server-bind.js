'use strict';
const common = require('../common');
const assert = require('assert');
const net = require('net');


// With only a callback, server should get a port assigned by the OS
{
  const server = net.createServer(common.mustNotCall());

  server.listen(common.mustCall(function() {
    assert.ok(server.address().port > 100);
    server.close();
  }));
}

// No callback to listen(), assume we can bind in 100 ms
{
  const server = net.createServer(common.mustNotCall());

  server.listen(common.PORT);

  setTimeout(function() {
    const address = server.address();
    assert.strictEqual(address.port, common.PORT);

    if (address.family === 'IPv6')
      assert.strictEqual(server._connectionKey, `6::::${address.port}`);
    else
      assert.strictEqual(server._connectionKey, `4:0.0.0.0:${address.port}`);

    server.close();
  }, 100);
}

// Callback to listen()
{
  const server = net.createServer(common.mustNotCall());

  server.listen(common.PORT + 1, common.mustCall(function() {
    assert.strictEqual(server.address().port, common.PORT + 1);
    server.close();
  }));
}

// Backlog argument
{
  const server = net.createServer(common.mustNotCall());

  server.listen(common.PORT + 2, '0.0.0.0', 127, common.mustCall(function() {
    assert.strictEqual(server.address().port, common.PORT + 2);
    server.close();
  }));
}

// Backlog argument without host argument
{
  const server = net.createServer(common.mustNotCall());

  server.listen(common.PORT + 3, 127, common.mustCall(function() {
    assert.strictEqual(server.address().port, common.PORT + 3);
    server.close();
  }));
}

// Test host option
{
  let port = common.PORT + 3;

  // Host is "textual" hostname + port
  {
    const testPort = ++port;
    const host = `localhost:${port}`;

    const server = net.createServer();
    server.listen({ host }, common.mustCall(() => {
      const { address, port: listenPort } = server.address();
      assert.strictEqual(address, '127.0.0.1');
      assert.strictEqual(listenPort, testPort);

      server.close();
    }));
  }
  // Host with IPv4 hostname + port
  {
    const testPort = ++port;
    const host = `127.0.0.1:${port}`;

    const server = net.createServer();
    server.listen({ host }, common.mustCall(() => {
      const { address, port: listenPort } = server.address();
      assert.strictEqual(address, '127.0.0.1');
      assert.strictEqual(listenPort, testPort);

      server.close();
    }));
  }
  // Host with IPv6 hostname + port
  ipv6: {
    if (!common.hasIPv6) {
      break ipv6;
    }

    const testPort = ++port;
    const host = `[::1]:${port}`;

    const server = net.createServer();
    server.listen({ host }, common.mustCall(() => {
      const { address, port: listenPort } = server.address();
      assert.strictEqual(address, '::1');
      assert.strictEqual(listenPort, testPort);

      server.close();
    }));
  }
  // Host is a weird IPv6 address
  ipv6: {
    if (!common.hasIPv6) {
      break ipv6;
    }

    const testPort = ++port;
    const host = `[::1]:${port}`;

    const server = net.createServer();
    server.listen({ host }, common.mustCall(() => {
      const { address, port: listenPort } = server.address();
      assert.strictEqual(address, '::1');
      assert.strictEqual(listenPort, testPort);

      server.close();
    }));
  }

  // TODO host with invalid port throws
  // currently the URL constructor throws this error
  // common.expectsError(() => {
  //   net.createServer.listen({
  //     host: '127.0.0.1:99999999999999',
  //   }, common.mustNotCall());
  // }, {
  //   code: 'ERR_SOCKET_BAD_POT',
  //   type: RangeError,
  // });

  // Host without port AND without port option throws
  common.expectsError(() => {
    net.createServer().listen({
      host: '127.0.0.1',
    }, common.mustNotCall());
  }, {
    code: 'ERR_INVALID_ARG_VALUE',
    type: TypeError,
    message: /^The argument 'options' must have the property "port" or "path"\. Received .+$/,
  });

  // Host with port and port option
  {
    const testPort = ++port;
    const host = `127.0.0.1:${port}`;

    const server = net.createServer();
    server.listen({ host, port: testPort }, common.mustCall(() => {
      const { address, port: listenPort } = server.address();
      assert.strictEqual(address, '127.0.0.1');
      assert.strictEqual(listenPort, testPort);

      server.close();
    }));
  }

  // TODO host with port AND port option *as a string*
  // This errors out as converting the port to a number happens really
  // late, only after we parse the host option
  // {
  //   const testPort = ++port;
  //   const host = `127.0.0.1:${port}`;

  //   const server = net.createServer();
  //   server.listen({ host, port: String(testPort) }, common.mustCall(() => {
  //     const { address, port: listenPort } = server.address();
  //     assert.strictEqual(address, '127.0.0.1');
  //     assert.strictEqual(listenPort, testPort);

  //     server.close();
  //   }));
  // }

  // Host with port and a conflicting port option throws
  {
    common.expectsError(() => {
      net.createServer().listen({
        host: '127.0.0.1:1234',
        port: 4321,
      }, common.mustNotCall());
    }, {
      type: TypeError,
      message: /^Mismatched port in "host" and "port" options$/,
    });
  }

  // Host with with hostname option
  {
    const server = net.createServer();
    server.listen({
      host: '127.0.0.1',
      hostname: '127.0.0.1',
      port: 0,
    }, common.mustCall(() => {
      const { address } = server.address();
      assert.strictEqual(address, '127.0.0.1');

      server.close();
    }));
  }

  // Host with hostname and a conflicting hostname option throws
  {
    common.expectsError(() => {
      net.createServer().listen({
        host: '127.0.0.1',
        hostname: '127.1.1.0',
        port: 0,
      }, common.mustNotCall());
    }, {
      type: TypeError,
      message: /^Mismatched hostname in "host" and "hostname" options$/,
    });
  }

  // A parsed URL
  // TODO this currently errors out, see the port-as-string mentioned above
  // {
  //   const testPort = ++port;
  //   const url = new URL(`http://127.0.0.1:${testPort}`);

  //   const server = net.createServer();
  //   server.listen(url, common.mustCall(() => {
  //     const { address, port: listenPort } = server.address();
  //     assert.strictEqual(address, '127.0.0.1');
  //     assert.strictEqual(listenPort, testPort);

  //     server.close();
  //   }));
  // }
}
