'use strict';
const common = require('../common');
const assert = require('assert');
const net = require('net');

function close() { this.close(); }

{
  // Test listen()
  net.createServer().listen().on('listening', common.mustCall(close));
  // Test listen(cb)
  net.createServer().listen(common.mustCall(close));
  // Test listen(port)
  net.createServer().listen(0).on('listening', common.mustCall(close));
  // Test listen({port})
  net.createServer().listen({ port: 0 })
    .on('listening', common.mustCall(close));
}

// Test listen(port, cb) and listen({ port }, cb) combinations
const listenOnPort = [
  (port, cb) => net.createServer().listen({ port }, cb),
  (port, cb) => net.createServer().listen(port, cb),
];

{
  const assertPort = () => {
    return common.expectsError({
      code: 'ERR_SOCKET_BAD_PORT',
      name: 'RangeError'
    });
  };

  for (const listen of listenOnPort) {
    // Arbitrary unused ports
    listen('0', common.mustCall(close));
    listen(0, common.mustCall(close));
    listen(undefined, common.mustCall(close));
    listen(null, common.mustCall(close));
    // Test invalid ports
    assert.throws(() => listen(-1, common.mustNotCall()), assertPort());
    assert.throws(() => listen(NaN, common.mustNotCall()), assertPort());
    assert.throws(() => listen(123.456, common.mustNotCall()), assertPort());
    assert.throws(() => listen(65536, common.mustNotCall()), assertPort());
    assert.throws(() => listen(1 / 0, common.mustNotCall()), assertPort());
    assert.throws(() => listen(-1 / 0, common.mustNotCall()), assertPort());
  }
  // In listen(options, cb), port takes precedence over path
  assert.throws(() => {
    net.createServer().listen({ port: -1, path: common.PIPE },
                              common.mustNotCall());
  }, assertPort());
}

{
  function shouldFailToListen(options) {
    const fn = () => {
      net.createServer().listen(options, common.mustNotCall());
    };

    if (typeof options === 'object' &&
      !(('port' in options) || ('path' in options))) {
      assert.throws(fn,
                    {
                      code: 'ERR_INVALID_ARG_VALUE',
                      name: 'TypeError',
                      message: /^The argument 'options' must have the property "port" or "path"\. Received .+$/,
                    });
    } else {
      assert.throws(fn,
                    {
                      code: 'ERR_INVALID_ARG_VALUE',
                      name: 'TypeError',
                      message: /^The argument 'options' is invalid\. Received .+$/,
                    });
    }
  }

  shouldFailToListen(false, { port: false });
  shouldFailToListen({ port: false });
  shouldFailToListen(true);
  shouldFailToListen({ port: true });
  // Invalid fd as listen(handle)
  shouldFailToListen({ fd: -1 });
  // Invalid path in listen(options)
  shouldFailToListen({ path: -1 });

  // Neither port or path are specified in options
  shouldFailToListen({});
  shouldFailToListen({ host: 'localhost' });
  shouldFailToListen({ host: { port: 3000 } });
  shouldFailToListen({ exclusive: true });
}
