'use strict';
require('../common');
const assert = require('assert');

const { Transform, Readable, Writable, getDefaultHighWaterMark } = require('stream');

const DEFAULT = getDefaultHighWaterMark();

function testTransform(expectedReadableHwm, expectedWritableHwm, options) {
  const t = new Transform(options);
  assert.strictEqual(t._readableState.highWaterMark, expectedReadableHwm);
  assert.strictEqual(t._writableState.highWaterMark, expectedWritableHwm);
}

// Test overriding defaultHwm
testTransform(666, DEFAULT, { readableHighWaterMark: 666 });
testTransform(DEFAULT, 777, { writableHighWaterMark: 777 });
testTransform(666, 777, {
  readableHighWaterMark: 666,
  writableHighWaterMark: 777,
});

// Test highWaterMark overriding
testTransform(555, 555, {
  highWaterMark: 555,
  readableHighWaterMark: 666,
});
testTransform(555, 555, {
  highWaterMark: 555,
  writableHighWaterMark: 777,
});
testTransform(555, 555, {
  highWaterMark: 555,
  readableHighWaterMark: 666,
  writableHighWaterMark: 777,
});

// Test undefined, null
[undefined, null].forEach((v) => {
  testTransform(DEFAULT, DEFAULT, { readableHighWaterMark: v });
  testTransform(DEFAULT, DEFAULT, { writableHighWaterMark: v });
  testTransform(666, DEFAULT, { highWaterMark: v, readableHighWaterMark: 666 });
  testTransform(DEFAULT, 777, { highWaterMark: v, writableHighWaterMark: 777 });
});

// test NaN
{
  assert.throws(() => {
    new Transform({ readableHighWaterMark: NaN });
  }, {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_VALUE',
    message: "The property 'options.readableHighWaterMark' is invalid. " +
      'Received NaN'
  });

  assert.throws(() => {
    new Transform({ writableHighWaterMark: NaN });
  }, {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_VALUE',
    message: "The property 'options.writableHighWaterMark' is invalid. " +
      'Received NaN'
  });
}

// Test non Duplex streams ignore the options
{
  const r = new Readable({ readableHighWaterMark: 666 });
  assert.strictEqual(r._readableState.highWaterMark, DEFAULT);
  const w = new Writable({ writableHighWaterMark: 777 });
  assert.strictEqual(w._writableState.highWaterMark, DEFAULT);
}
