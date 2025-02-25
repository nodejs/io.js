// Flags: --expose-internals
import { mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import process from 'process';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';

const hooks = initHooks();

hooks.enable();

import internalTestBinding from 'internal/test/binding';
const { internalBinding } = internalTestBinding;
const { Zlib } = internalBinding('zlib');
import { constants } from 'zlib';

const handle = new Zlib(constants.DEFLATE);

const as = hooks.activitiesOfTypes('ZLIB');
strictEqual(as.length, 1);
const hdl = as[0];
strictEqual(hdl.type, 'ZLIB');
strictEqual(typeof hdl.uid, 'number');
strictEqual(typeof hdl.triggerAsyncId, 'number');
checkInvocations(hdl, { init: 1 }, 'when created handle');

// Store all buffers together so that they do not get
// garbage collected.
const buffers = {
  writeResult: new Uint32Array(2),
  dictionary: new Uint8Array(0),
  inBuf: new Uint8Array([0x78]),
  outBuf: new Uint8Array(1)
};

handle.init(
  constants.Z_DEFAULT_WINDOWBITS,
  constants.Z_MIN_LEVEL,
  constants.Z_DEFAULT_MEMLEVEL,
  constants.Z_DEFAULT_STRATEGY,
  buffers.writeResult,
  function processCallback() { this.cb(); },
  buffers.dictionary
);
checkInvocations(hdl, { init: 1 }, 'when initialized handle');

let count = 2;
handle.cb = mustCall(onwritten, 2);
handle.write(true, buffers.inBuf, 0, 1, buffers.outBuf, 0, 1);
checkInvocations(hdl, { init: 1 }, 'when invoked write() on handle');

function onwritten() {
  if (--count) {
    // first write
    checkInvocations(hdl, { init: 1, before: 1 },
                     'when wrote to handle the first time');
    handle.write(true, buffers.inBuf, 0, 1, buffers.outBuf, 0, 1);
  } else {
    // second write
    checkInvocations(hdl, { init: 1, before: 2, after: 1 },
                     'when wrote to handle the second time');
  }
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('ZLIB');
  // TODO: destroy never called here even with large amounts of ticks
  // is that correct?
  checkInvocations(hdl, { init: 1, before: 2, after: 2 }, 'when process exits');

  // Do something with `buffers` to keep them alive until here.
  buffers.buffers = buffers;
}
