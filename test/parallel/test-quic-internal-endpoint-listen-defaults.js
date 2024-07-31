// Flags: --expose-internals
'use strict';

const common = require('../common');
if (!common.hasQuic)
  common.skip('missing quic');

const { internalBinding } = require('internal/test/binding');
const {
  ok,
  strictEqual,
  deepStrictEqual,
} = require('node:assert');

const {
  SocketAddress: _SocketAddress,
  AF_INET,
} = internalBinding('block_list');
const quic = internalBinding('quic');

quic.setCallbacks({
  onEndpointClose: common.mustCall((...args) => {
    deepStrictEqual(args, [0, 0]);
  }),

  // The following are unused in this test
  onSessionNew() {},
  onSessionClose() {},
  onSessionDatagram() {},
  onSessionDatagramStatus() {},
  onSessionHandshake() {},
  onSessionPathValidation() {},
  onSessionTicket() {},
  onSessionVersionNegotiation() {},
  onStreamCreated() {},
  onStreamBlocked() {},
  onStreamClose() {},
  onStreamReset() {},
  onStreamHeaders() {},
  onStreamTrailers() {},
});

const endpoint = new quic.Endpoint({});

const state = new DataView(endpoint.state);
ok(!state.getUint8(quic.IDX_STATE_ENDPOINT_LISTENING));
ok(!state.getUint8(quic.IDX_STATE_ENDPOINT_RECEIVING));
ok(!state.getUint8(quic.IDX_STATE_ENDPOINT_BOUND));
strictEqual(endpoint.address(), undefined);

endpoint.listen({});

ok(state.getUint8(quic.IDX_STATE_ENDPOINT_LISTENING));
ok(state.getUint8(quic.IDX_STATE_ENDPOINT_RECEIVING));
ok(state.getUint8(quic.IDX_STATE_ENDPOINT_BOUND));
const address = endpoint.address();
ok(address instanceof _SocketAddress);

const detail = address.detail({
  address: undefined,
  port: undefined,
  family: undefined,
  flowlabel: undefined,
});

strictEqual(detail.address, '127.0.0.1');
strictEqual(detail.family, AF_INET);
strictEqual(detail.flowlabel, 0);
ok(detail.port !== 0);

endpoint.closeGracefully();

ok(!state.getUint8(quic.IDX_STATE_ENDPOINT_LISTENING));
ok(!state.getUint8(quic.IDX_STATE_ENDPOINT_RECEIVING));
ok(!state.getUint8(quic.IDX_STATE_ENDPOINT_BOUND));
strictEqual(endpoint.address(), undefined);
