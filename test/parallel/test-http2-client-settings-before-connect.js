'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const h2 = require('http2');

const server = h2.createServer();

// We use the lower-level API here
server.on('stream', common.mustCall((stream, headers, flags) => {
  stream.respond();
  stream.end('ok');
}));
server.on('session', common.mustCall((session) => {
  session.on('remoteSettings', common.mustCall(2));
}));

server.listen(0, common.mustCall(() => {
  const client = h2.connect(`http://localhost:${server.address().port}`);

  [
    ['headerTableSize', -1, RangeError],
    ['headerTableSize', 2 ** 32, RangeError],
    ['initialWindowSize', -1, RangeError],
    ['initialWindowSize', 2 ** 32, RangeError],
    ['maxFrameSize', 1, RangeError],
    ['maxFrameSize', 2 ** 24, RangeError],
    ['maxConcurrentStreams', -1, RangeError],
    ['maxConcurrentStreams', 2 ** 32, RangeError],
    ['maxHeaderListSize', -1, RangeError],
    ['maxHeaderListSize', 2 ** 32, RangeError],
    ['maxHeaderSize', -1, RangeError],
    ['maxHeaderSize', 2 ** 32, RangeError],
    ['enablePush', 'a', TypeError],
    ['enablePush', 1, TypeError],
    ['enablePush', 0, TypeError],
    ['enablePush', null, TypeError],
    ['enablePush', {}, TypeError],
  ].forEach(([name, value, errorType]) =>
    assert.throws(
      () => client.settings({ [name]: value }),
      {
        code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
        name: errorType.name
      }
    )
  );

  assert.throws(
    () => client.settings({ customSettings: {
      0x11: 5,
      0x12: 5,
      0x13: 5,
      0x14: 5,
      0x15: 5,
      0x16: 5,
      0x17: 5,
      0x18: 5,
      0x19: 5,
      0x1A: 5, // more than 10
      0x1B: 5
    } }),
    {
      code: 'ERR_HTTP2_TOO_MANY_CUSTOM_SETTINGS',
      name: 'Error'
    }
  );

  assert.throws(
    () => client.settings({ customSettings: {
      0x10000: 5,
    } }),
    {
      code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
      name: 'RangeError'
    }
  );

  assert.throws(
    () => client.settings({ customSettings: {
      0x55: 0x100000000,
    } }),
    {
      code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
      name: 'RangeError'
    }
  );

  assert.throws(
    () => client.settings({ customSettings: {
      0x55: -1,
    } }),
    {
      code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
      name: 'RangeError'
    }
  );

  [1, true, {}, []].forEach((invalidCallback) =>
    assert.throws(
      () => client.settings({}, invalidCallback),
      {
        name: 'TypeError',
        code: 'ERR_INVALID_ARG_TYPE',
      }
    )
  );

  client.settings({ maxFrameSize: 1234567, customSettings: { 0xbf: 12 } });

  const req = client.request();
  req.on('response', common.mustCall());
  req.resume();
  req.on('close', common.mustCall(() => {
    server.close();
    client.close();
  }));
}));
