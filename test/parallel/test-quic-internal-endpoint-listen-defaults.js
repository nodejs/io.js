// Flags: --expose-internals --no-warnings
'use strict';

const { hasQuic } = require('../common');

const {
  describe,
  it,
} = require('node:test');

describe('quic internal endpoint listen defaults', { skip: !hasQuic }, async () => {
  const {
    ok,
    strictEqual,
    throws,
  } = require('node:assert');

  const {
    SocketAddress,
  } = require('net');

  const {
    Endpoint,
  } = require('internal/quic/quic');

  it('are reasonable and work as expected', async () => {
    const endpoint = new Endpoint({
      onsession() {},
      session: {},
      stream: {},
    }, {});

    ok(!endpoint.state.isBound);
    ok(!endpoint.state.isReceiving);
    ok(!endpoint.state.isListening);

    strictEqual(endpoint.address, undefined);

    throws(() => endpoint.listen(123), {
      code: 'ERR_INVALID_ARG_TYPE',
    });

    endpoint.listen();
    throws(() => endpoint.listen(), {
      code: 'ERR_INVALID_STATE',
    });

    ok(endpoint.state.isBound);
    ok(endpoint.state.isReceiving);
    ok(endpoint.state.isListening);

    const address = endpoint.address;
    ok(address instanceof SocketAddress);

    strictEqual(address.address, '127.0.0.1');
    strictEqual(address.family, 'ipv4');
    strictEqual(address.flowlabel, 0);
    ok(address.port !== 0);

    ok(!endpoint.destroyed);
    endpoint.destroy();
    strictEqual(endpoint.closed, endpoint.close());
    await endpoint.closed;
    ok(endpoint.destroyed);

    throws(() => endpoint.listen(), {
      code: 'ERR_INVALID_STATE',
    });
    throws(() => { endpoint.busy = true; }, {
      code: 'ERR_INVALID_STATE',
    });
    await endpoint[Symbol.asyncDispose]();

    strictEqual(endpoint.address, undefined);
  });
});
