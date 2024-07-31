'use strict';
const common = require('../common');
const net = require('net');
const assert = require('assert');
const socket = net.Stream({ highWaterMark: 0 });

// Make sure that anything besides a buffer or a string throws.
socket.on('error', common.mustNotCall());
assert.throws(() => {
  socket.write(null);
}, {
  code: 'ERR_STREAM_NULL_VALUES',
  name: 'TypeError',
  message: 'May not write null values to stream'
});

[
  true,
  false,
  undefined,
  1,
  1.0,
  +Infinity,
  -Infinity,
  [],
  {},
].forEach((value) => {
  const socket = net.Stream({ highWaterMark: 0 });
  // We need to check the callback since 'error' will only
  // be emitted once per instance.
  assert.throws(() => {
    socket.write(value);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: 'The "chunk" argument must be of type string or an instance of ' +
              `Buffer, TypedArray, or DataView.${common.invalidArgTypeHelper(value)}`
  });
});
