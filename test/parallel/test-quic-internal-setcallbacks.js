// Flags: --expose-internals
'use strict';

const common = require('../common');
if (!common.hasQuic)
  common.skip('missing quic');
const { internalBinding } = require('internal/test/binding');
const quic = internalBinding('quic');

const { throws } = require('assert');

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
  throws(() => quic.setCallbacks(rest), {
    code: 'ERR_MISSING_ARGS',
  });
}
// If all callbacks are present it should work
quic.setCallbacks(callbacks);
