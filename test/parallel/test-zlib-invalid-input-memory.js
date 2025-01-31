// Flags: --expose-gc
'use strict';
const common = require('../common');
const { onGC } = require('../common/gc');
const assert = require('assert');
const zlib = require('zlib');

// Checks that, if a zlib context fails with an error, it can still be GC'ed:
// Refs: https://github.com/nodejs/node/issues/22705

const ongc = common.mustCall();

{
  const input = Buffer.from('foobar');
  const strm = zlib.createInflate();
  strm.end(input);
  strm.once('error', common.mustCall((err) => {
    assert(err);
    setImmediate(() => {
      globalThis.gc();
      // Keep the event loop alive for seeing the async_hooks destroy hook
      // we use for GC tracking...
      // TODO(addaleax): This should maybe not be necessary?
      setImmediate(() => {});
    });
  }));
  onGC(strm, { ongc });
}
