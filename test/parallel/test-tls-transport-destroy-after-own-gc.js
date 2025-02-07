// Flags: --expose-gc
'use strict';

// Regression test for https://github.com/nodejs/node/issues/17475
// Unfortunately, this tests only "works" reliably when checked with valgrind or
// a similar tool.

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const { TLSSocket } = require('tls');
const { duplexPair } = require('stream');

let [ clientSide ] = duplexPair();

let clientTLS = new TLSSocket(clientSide, { isServer: false });
let clientTLSHandle = clientTLS._handle;  // eslint-disable-line no-unused-vars

setImmediate(() => {
  clientTLS = null;
  globalThis.gc();
  clientTLSHandle = null;
  globalThis.gc();
  setImmediate(() => {
    clientSide = null;
    globalThis.gc();
  });
});
