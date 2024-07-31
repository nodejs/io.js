// Flags: --expose-internals
'use strict';

const common = require('../common');
if (!common.hasQuic)
  common.skip('missing quic');
const {
  throws,
} = require('node:assert');

const { internalBinding } = require('internal/test/binding');
const quic = internalBinding('quic');

quic.setCallbacks({
  onEndpointClose() {},
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

throws(() => new quic.Endpoint(), {
  code: 'ERR_INVALID_ARG_TYPE',
  message: 'options must be an object'
});

throws(() => new quic.Endpoint('a'), {
  code: 'ERR_INVALID_ARG_TYPE',
  message: 'options must be an object'
});

throws(() => new quic.Endpoint(null), {
  code: 'ERR_INVALID_ARG_TYPE',
  message: 'options must be an object'
});

throws(() => new quic.Endpoint(false), {
  code: 'ERR_INVALID_ARG_TYPE',
  message: 'options must be an object'
});

{
  // Just Works... using all defaults
  new quic.Endpoint({});
}

const cases = [
  {
    key: 'retryTokenExpiration',
    valid: [
      1, 10, 100, 1000, 10000, 10000n,
    ],
    invalid: [-1, -1n, 'a', null, false, true, {}, [], () => {}]
  },
  {
    key: 'tokenExpiration',
    valid: [
      1, 10, 100, 1000, 10000, 10000n,
    ],
    invalid: [-1, -1n, 'a', null, false, true, {}, [], () => {}]
  },
  {
    key: 'maxConnectionsPerHost',
    valid: [
      1, 10, 100, 1000, 10000, 10000n,
    ],
    invalid: [-1, -1n, 'a', null, false, true, {}, [], () => {}]
  },
  {
    key: 'maxConnectionsTotal',
    valid: [
      1, 10, 100, 1000, 10000, 10000n,
    ],
    invalid: [-1, -1n, 'a', null, false, true, {}, [], () => {}]
  },
  {
    key: 'maxStatelessResetsPerHost',
    valid: [
      1, 10, 100, 1000, 10000, 10000n,
    ],
    invalid: [-1, -1n, 'a', null, false, true, {}, [], () => {}]
  },
  {
    key: 'addressLRUSize',
    valid: [
      1, 10, 100, 1000, 10000, 10000n,
    ],
    invalid: [-1, -1n, 'a', null, false, true, {}, [], () => {}]
  },
  {
    key: 'maxRetries',
    valid: [
      1, 10, 100, 1000, 10000, 10000n,
    ],
    invalid: [-1, -1n, 'a', null, false, true, {}, [], () => {}]
  },
  {
    key: 'maxPayloadSize',
    valid: [
      1, 10, 100, 1000, 10000, 10000n,
    ],
    invalid: [-1, -1n, 'a', null, false, true, {}, [], () => {}]
  },
  {
    key: 'unacknowledgedPacketThreshold',
    valid: [
      1, 10, 100, 1000, 10000, 10000n,
    ],
    invalid: [-1, -1n, 'a', null, false, true, {}, [], () => {}]
  },
  {
    key: 'validateAddress',
    valid: [true, false, 0, 1, 'a'],
    invalid: [],
  },
  {
    key: 'disableStatelessReset',
    valid: [true, false, 0, 1, 'a'],
    invalid: [],
  },
  {
    key: 'ipv6Only',
    valid: [true, false, 0, 1, 'a'],
    invalid: [],
  },
  {
    key: 'cc',
    valid: [
      quic.CC_ALGO_RENO,
      quic.CC_ALGO_CUBIC,
      quic.CC_ALGO_BBR,
      quic.CC_ALGO_BBR2,
      quic.CC_ALGO_RENO_STR,
      quic.CC_ALGO_CUBIC_STR,
      quic.CC_ALGO_BBR_STR,
      quic.CC_ALGO_BBR2_STR,
    ],
    invalid: [-1, 4, 1n, 'a', null, false, true, {}, [], () => {}],
  },
  {
    key: 'udpReceiveBufferSize',
    valid: [0, 1, 2, 3, 4, 1000],
    invalid: [-1, 'a', null, false, true, {}, [], () => {}],
  },
  {
    key: 'udpSendBufferSize',
    valid: [0, 1, 2, 3, 4, 1000],
    invalid: [-1, 'a', null, false, true, {}, [], () => {}],
  },
  {
    key: 'udpTTL',
    valid: [0, 1, 2, 3, 4, 255],
    invalid: [-1, 256, 'a', null, false, true, {}, [], () => {}],
  },
  {
    key: 'resetTokenSecret',
    valid: [
      new Uint8Array(16),
      new Uint16Array(8),
      new Uint32Array(4),
    ],
    invalid: [
      'a', null, false, true, {}, [], () => {},
      new Uint8Array(15),
      new Uint8Array(17),
      new ArrayBuffer(16),
    ],
  },
  {
    key: 'tokenSecret',
    valid: [
      new Uint8Array(16),
      new Uint16Array(8),
      new Uint32Array(4),
    ],
    invalid: [
      'a', null, false, true, {}, [], () => {},
      new Uint8Array(15),
      new Uint8Array(17),
      new ArrayBuffer(16),
    ],
  },
  {
    // Unknown options are ignored entirely for any value type
    key: 'ignored',
    valid: ['a', null, false, true, {}, [], () => {}],
    invalid: [],
  },
];

for (const { key, valid, invalid } of cases) {
  for (const value of valid) {
    const options = {};
    options[key] = value;
    new quic.Endpoint(options);
  }

  for (const value of invalid) {
    const options = {};
    options[key] = value;
    throws(() => new quic.Endpoint(options), {
      code: 'ERR_INVALID_ARG_VALUE',
    });
  }
}
