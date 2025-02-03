'use strict';
const common = require('../common');

const assert = require('assert');
const stream = require('stream');
const { inspect } = require('util');

{
  // This test ensures that the stream implementation correctly handles values
  // for highWaterMark which exceed the range of signed 32 bit integers and
  // rejects invalid values.

  // This number exceeds the range of 32 bit integer arithmetic but should still
  // be handled correctly.
  const ovfl = Number.MAX_SAFE_INTEGER;

  const readable = stream.Readable({ highWaterMark: ovfl });
  assert.strictEqual(readable._readableState.highWaterMark, ovfl);

  const writable = stream.Writable({ highWaterMark: ovfl });
  assert.strictEqual(writable._writableState.highWaterMark, ovfl);

  for (const invalidHwm of [true, false, '5', {}, -5, NaN]) {
    for (const type of [stream.Readable, stream.Writable]) {
      assert.throws(() => {
        type({ highWaterMark: invalidHwm });
      }, {
        name: 'TypeError',
        code: 'ERR_INVALID_ARG_VALUE',
        message: "The property 'options.highWaterMark' is invalid. " +
          `Received ${inspect(invalidHwm)}`
      });
    }
  }
}

{
  // This test ensures that the push method's implementation
  // correctly handles the edge case where the highWaterMark and
  // the state.length are both zero

  const readable = stream.Readable({ highWaterMark: 0 });

  for (let i = 0; i < 3; i++) {
    const needMoreData = readable.push();
    assert.strictEqual(needMoreData, true);
  }
}

{
  // This test ensures that the read(n) method's implementation
  // correctly handles the edge case where the highWaterMark, state.length
  // and n are all zero

  const readable = stream.Readable({ highWaterMark: 0 });

  readable._read = common.mustCall();
  readable.read(0);
}

{
  // Parse size as decimal integer
  ['1', '1.0', 1].forEach((size) => {
    const readable = new stream.Readable({
      read: common.mustCall(),
      highWaterMark: 0,
    });
    readable.read(size);

    assert.strictEqual(readable._readableState.highWaterMark, Number(size));
  });
}

{
  // Test highwatermark limit
  const hwm = 0x40000000 + 1;
  const readable = stream.Readable({
    read() {},
  });

  assert.throws(() => readable.read(hwm), common.expectsError({
    code: 'ERR_OUT_OF_RANGE',
    message: 'The value of "size" is out of range.' +
             ' It must be <= 1GiB. Received ' +
             hwm,
  }));
}

{
  const res = [];
  const r = new stream.Readable({
    read() {},
  });
  const w = new stream.Writable({
    highWaterMark: 0,
    write(chunk, encoding, callback) {
      res.push(chunk.toString());
      callback();
    },
  });

  r.pipe(w);
  r.push('a');
  r.push('b');
  r.push('c');
  r.push(null);

  r.on('end', common.mustCall(() => {
    assert.deepStrictEqual(res, ['a', 'b', 'c']);
  }));
}
