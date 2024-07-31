// Flags: --max-old-space-size=16
'use strict';

// This test ensures that diagnostic channel references aren't leaked.

const common = require('../common');

const { subscribe, unsubscribe, Channel } = require('diagnostics_channel');
const { checkIfCollectableByCounting } = require('../common/gc');

function noop() {}

const outer = 64;
const inner = 256;
checkIfCollectableByCounting((i) => {
  for (let j = 0; j < inner; j++) {
    const key = String(i * inner + j);
    subscribe(key, noop);
    unsubscribe(key, noop);
  }
  return inner;
}, Channel, outer).then(common.mustCall());
