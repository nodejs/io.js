'use strict';
const common = require('../common');
const assert = require('assert');
const dgram = require('dgram');
const invalidTypes = [
  'test',
  ['udp4'],
  new String('udp4'),
  1,
  {},
  true,
  false,
  null,
  undefined,
];
const validTypes = [
  'udp4',
  'udp6',
  { type: 'udp4' },
  { type: 'udp6' },
];
const errMessage = /^Bad socket type specified\. Valid types are: udp4, udp6$/;

// Error must be thrown with invalid types
invalidTypes.forEach((invalidType) => {
  assert.throws(() => {
    dgram.createSocket(invalidType);
  }, {
    code: 'ERR_SOCKET_BAD_TYPE',
    name: 'TypeError',
    message: errMessage
  });
});

// Error must not be thrown with valid types
validTypes.forEach((validType) => {
  const socket = dgram.createSocket(validType);
  socket.close();
});

// Ensure buffer sizes can be set
{
  const socket = dgram.createSocket({
    type: 'udp4',
    recvBufferSize: 10000,
    sendBufferSize: 15000
  });

  socket.bind(common.mustCall(() => {
    // note: linux will double the buffer size
    assert.ok(socket.getRecvBufferSize() === 10000 ||
              socket.getRecvBufferSize() === 20000,
              'SO_RCVBUF not 10000 or 20000, ' +
                `was ${socket.getRecvBufferSize()}`);
    assert.ok(socket.getSendBufferSize() === 15000 ||
              socket.getSendBufferSize() === 30000,
              'SO_SNDBUF not 15000 or 30000, ' +
                `was ${socket.getRecvBufferSize()}`);
    socket.close();
  }));
}

{
  [
    { type: 'udp4', recvBufferSize: 'invalid' },
    { type: 'udp4', sendBufferSize: 'invalid' },
  ].forEach((options) => {
    assert.throws(() => {
      dgram.createSocket(options);
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
    });
  });
}
