// Flags: --expose-internals --no-warnings
'use strict';

const { hasQuic } = require('../common');

const {
  describe,
  it,
} = require('node:test');

describe('quic internal setCallbacks', { skip: !hasQuic }, () => {
  const { internalBinding } = require('internal/test/binding');
  const quic = internalBinding('quic');

  it('require all callbacks to be set', (t) => {
    const callbacks = {
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
    };
    // Fail if any callback is missing
    for (const fn of Object.keys(callbacks)) {
      // eslint-disable-next-line no-unused-vars
      const { [fn]: _, ...rest } = callbacks;
      t.assert.throws(() => quic.setCallbacks(rest), {
        code: 'ERR_MISSING_ARGS',
      });
    }
    // If all callbacks are present it should work
    quic.setCallbacks(callbacks);

    // Multiple calls should just be ignored.
    quic.setCallbacks(callbacks);
  });
});
