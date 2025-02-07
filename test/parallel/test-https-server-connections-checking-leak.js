'use strict';

// Flags: --expose-gc

// Check that creating a server without listening does not leak resources.

const common = require('../common');

if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const { onGC } = require('../common/gc');
const Countdown = require('../common/countdown');

const https = require('https');
const max = 100;

// Note that Countdown internally calls common.mustCall, that's why it's not done here.
const countdown = new Countdown(max, () => {});

for (let i = 0; i < max; i++) {
  const server = https.createServer((req, res) => {});
  onGC(server, { ongc: countdown.dec.bind(countdown) });
}

setImmediate(() => {
  globalThis.gc();
});
