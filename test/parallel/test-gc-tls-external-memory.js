'use strict';
// Flags: --expose-gc

// Tests that memoryUsage().external doesn't go negative
// when a lot tls connections are opened and closed

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const { duplexPair } = require('stream');
const onGC = require('../common/ongc');
const assert = require('assert');
const tls = require('tls');

// Payload doesn't matter. We just need to have the tls
// connection try and connect somewhere.
const dummyPayload = Buffer.alloc(10000, 'yolo');

let runs = 0;

// Count garbage-collected TLS sockets.
let gced = 0;
function ongc() { gced++; }

connect();

function connect() {
  if (runs % 64 === 0)
    global.gc();
  const externalMemoryUsage = process.memoryUsage().external;
  assert(externalMemoryUsage >= 0, `${externalMemoryUsage} < 0`);
  if (runs++ === 512) {
    // Make sure at least half the TLS sockets have been garbage collected
    // (so that this test can actually check what it's testing):
    assert(gced >= 256, `${gced} < 256`);
    return;
  }

  const [ clientSide, serverSide ] = duplexPair();

  const tlsSocket = tls.connect({ socket: clientSide });
  tlsSocket.on('error', common.mustCall(connect));
  onGC(tlsSocket, { ongc });

  // Use setImmediate so that we don't trigger the error within the same
  // event loop tick.
  setImmediate(() => serverSide.write(dummyPayload));
}
