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
    rejects,
    strictEqual,
    throws,
  } = require('node:assert');

  const {
    kState,
  } = require('internal/quic/symbols');

  const { createPrivateKey } = require('node:crypto');
  const fixtures = require('../common/fixtures');
  const keys = createPrivateKey(fixtures.readKey('agent1-key.pem'));
  const certs = fixtures.readKey('agent1-cert.pem');

  const {
    SocketAddress,
  } = require('net');

  const {
    QuicEndpoint,
    listen,
  } = require('internal/quic/quic');

  it('are reasonable and work as expected', async () => {
    const endpoint = new QuicEndpoint();

    ok(!endpoint[kState].isBound);
    ok(!endpoint[kState].isReceiving);
    ok(!endpoint[kState].isListening);

    strictEqual(endpoint.address, undefined);

    await rejects(listen(123, { keys, certs, endpoint }), {
      code: 'ERR_INVALID_ARG_TYPE',
    });

    await rejects(listen(() => {}, 123), {
      code: 'ERR_INVALID_ARG_TYPE',
    });

    await listen(() => {}, { keys, certs, endpoint });
    await rejects(listen(() => {}, { keys, certs, endpoint }), {
      code: 'ERR_INVALID_STATE',
    });

    ok(endpoint[kState].isBound);
    ok(endpoint[kState].isReceiving);
    ok(endpoint[kState].isListening);

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

    await rejects(listen(() => {}, { keys, certs, endpoint }), {
      code: 'ERR_INVALID_STATE',
    });
    throws(() => { endpoint.busy = true; }, {
      code: 'ERR_INVALID_STATE',
    });
    await endpoint[Symbol.asyncDispose]();

    strictEqual(endpoint.address, undefined);
  });
});
