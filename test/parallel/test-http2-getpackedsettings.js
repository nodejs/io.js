'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const http2 = require('http2');

const check = Buffer.from([0x00, 0x01, 0x00, 0x00, 0x10, 0x00,
                           0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
                           0x00, 0x03, 0xff, 0xff, 0xff, 0xff,
                           0x00, 0x04, 0x00, 0x00, 0xff, 0xff,
                           0x00, 0x05, 0x00, 0x00, 0x40, 0x00,
                           0x00, 0x06, 0x00, 0x00, 0xff, 0xff,
                           0x00, 0x08, 0x00, 0x00, 0x00, 0x00]);
const val = http2.getPackedSettings(http2.getDefaultSettings());
assert.deepStrictEqual(val, check);

[
  ['headerTableSize', 0],
  ['headerTableSize', 2 ** 32 - 1],
  ['initialWindowSize', 0],
  ['initialWindowSize', 2 ** 32 - 1],
  ['maxFrameSize', 16384],
  ['maxFrameSize', 2 ** 24 - 1],
  ['maxConcurrentStreams', 0],
  ['maxConcurrentStreams', 2 ** 31 - 1],
  ['maxHeaderListSize', 0],
  ['maxHeaderListSize', 2 ** 32 - 1],
  ['maxHeaderSize', 0],
  ['maxHeaderSize', 2 ** 32 - 1],
  ['customSettings', { '9999': 301 }],
].forEach((i) => {
  // Valid options should not throw.
  http2.getPackedSettings({ [i[0]]: i[1] });
});

http2.getPackedSettings({ enablePush: true });
http2.getPackedSettings({ enablePush: false });

[
  ['headerTableSize', -1],
  ['headerTableSize', 2 ** 32],
  ['initialWindowSize', -1],
  ['initialWindowSize', 2 ** 32],
  ['maxFrameSize', 16383],
  ['maxFrameSize', 2 ** 24],
  ['maxConcurrentStreams', -1],
  ['maxConcurrentStreams', 2 ** 32],
  ['maxHeaderListSize', -1],
  ['maxHeaderListSize', 2 ** 32],
  ['maxHeaderSize', -1],
  ['maxHeaderSize', 2 ** 32],
].forEach((i) => {
  assert.throws(() => {
    http2.getPackedSettings({ [i[0]]: i[1] });
  }, {
    code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
    name: 'RangeError',
    message: `Invalid value for setting "${i[0]}": ${i[1]}`
  });
});

[
  1, null, '', Infinity, new Date(), {}, NaN, [false],
].forEach((i) => {
  assert.throws(() => {
    http2.getPackedSettings({ enablePush: i });
  }, {
    code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
    name: 'TypeError',
    message: `Invalid value for setting "enablePush": ${i}`
  });
});

[
  1, null, '', Infinity, new Date(), {}, NaN, [false],
].forEach((i) => {
  assert.throws(() => {
    http2.getPackedSettings({ enableConnectProtocol: i });
  }, {
    code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
    name: 'TypeError',
    message: `Invalid value for setting "enableConnectProtocol": ${i}`
  });
});

{
  const check = Buffer.from([
    0x00, 0x01, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x03, 0x00, 0x00, 0x00, 0xc8,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x05, 0x00, 0x00, 0x4e, 0x20,
    0x00, 0x06, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x27, 0x0F, 0x00, 0x00, 0x01, 0x2d,
  ]);

  const packed = http2.getPackedSettings({
    headerTableSize: 100,
    initialWindowSize: 100,
    maxFrameSize: 20000,
    maxConcurrentStreams: 200,
    maxHeaderListSize: 100,
    maxHeaderSize: 100,
    enablePush: true,
    enableConnectProtocol: false,
    foo: 'ignored',
    customSettings: { '9999': 301 }
  });
  assert.strictEqual(packed.length, 48);
  assert.deepStrictEqual(packed, check);
}

// Check if multiple custom settings can be set
{
  const check = Buffer.from([
    0x00, 0x01, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x03, 0x00, 0x00, 0x00, 0xc8,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x05, 0x00, 0x00, 0x4e, 0x20,
    0x00, 0x06, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x03, 0xf3, 0x00, 0x00, 0x07, 0x9F,
    0x0a, 0x2e, 0x00, 0x00, 0x00, 0x58,
  ]);

  const packed = http2.getPackedSettings({
    headerTableSize: 100,
    initialWindowSize: 100,
    maxFrameSize: 20000,
    maxConcurrentStreams: 200,
    maxHeaderListSize: 100,
    maxHeaderSize: 100,
    enablePush: true,
    enableConnectProtocol: false,
    customSettings: { '2606': 88, '1011': 1951 }
  });
  assert.strictEqual(packed.length, 54);
  assert.deepStrictEqual(packed, check);
}

{
  // Check if wrong custom settings cause an error

  assert.throws(() => {
    http2.getPackedSettings({
      customSettings: { '-1': 659685 }
    });
  }, {
    code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
    name: 'RangeError'
  });

  assert.throws(() => {
    http2.getPackedSettings({
      customSettings: { '10': 34577577777 }
    });
  }, {
    code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
    name: 'RangeError'
  });

  assert.throws(() => {
    http2.getPackedSettings({
      customSettings: { 'notvalid': -777 }
    });
  }, {
    code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
    name: 'RangeError'
  });

  assert.throws(() => {
    http2.getPackedSettings({
      customSettings: { '11': 11, '12': 12, '13': 13, '14': 14, '15': 15, '16': 16,
                        '17': 17, '18': 18, '19': 19, '20': 20, '21': 21 }
    });
  }, {
    code: 'ERR_HTTP2_TOO_MANY_CUSTOM_SETTINGS'
  });
  assert.throws(() => {
    http2.getPackedSettings({
      customSettings: { '11': 11, '12': 12, '13': 13, '14': 14, '15': 15, '16': 16,
                        '17': 17, '18': 18, '19': 19, '20': 20, '21': 21, '22': 22 }
    });
  }, {
    code: 'ERR_HTTP2_TOO_MANY_CUSTOM_SETTINGS'
  });
}

// Check for not passing settings.
{
  const packed = http2.getPackedSettings();
  assert.strictEqual(packed.length, 0);
}

{
  const packed = Buffer.from([
    0x00, 0x01, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x03, 0x00, 0x00, 0x00, 0xc8,
    0x00, 0x05, 0x00, 0x00, 0x4e, 0x20,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x06, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x27, 0x0F, 0x00, 0x00, 0x01, 0x2d]);

  [1, true, '', [], {}, NaN].forEach((input) => {
    assert.throws(() => {
      http2.getUnpackedSettings(input);
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message:
        'The "buf" argument must be an instance of Buffer or TypedArray.' +
        common.invalidArgTypeHelper(input)
    });
  });

  assert.throws(() => {
    http2.getUnpackedSettings(packed.slice(5));
  }, {
    code: 'ERR_HTTP2_INVALID_PACKED_SETTINGS_LENGTH',
    name: 'RangeError',
    message: 'Packed settings length must be a multiple of six'
  });

  const settings = http2.getUnpackedSettings(packed);

  assert(settings);
  assert.strictEqual(settings.headerTableSize, 100);
  assert.strictEqual(settings.initialWindowSize, 100);
  assert.strictEqual(settings.maxFrameSize, 20000);
  assert.strictEqual(settings.maxConcurrentStreams, 200);
  assert.strictEqual(settings.maxHeaderListSize, 100);
  assert.strictEqual(settings.maxHeaderSize, 100);
  assert.strictEqual(settings.enablePush, true);
  assert.strictEqual(settings.enableConnectProtocol, false);
  assert.deepStrictEqual(settings.customSettings, { '9999': 301 });
}

{
  const packed = new Uint16Array([
    0x00, 0x01, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x03, 0x00, 0x00, 0x00, 0xc8,
    0x00, 0x05, 0x00, 0x00, 0x4e, 0x20,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x06, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x08, 0x00, 0x00, 0x00, 0x00]);

  assert.throws(() => {
    http2.getUnpackedSettings(packed.slice(5));
  }, {
    code: 'ERR_HTTP2_INVALID_PACKED_SETTINGS_LENGTH',
    name: 'RangeError',
    message: 'Packed settings length must be a multiple of six'
  });

  const settings = http2.getUnpackedSettings(packed);

  assert(settings);
  assert.strictEqual(settings.headerTableSize, 100);
  assert.strictEqual(settings.initialWindowSize, 100);
  assert.strictEqual(settings.maxFrameSize, 20000);
  assert.strictEqual(settings.maxConcurrentStreams, 200);
  assert.strictEqual(settings.maxHeaderListSize, 100);
  assert.strictEqual(settings.maxHeaderSize, 100);
  assert.strictEqual(settings.enablePush, true);
  assert.strictEqual(settings.enableConnectProtocol, false);
}

{
  const packed = new DataView(Buffer.from([
    0x00, 0x01, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x03, 0x00, 0x00, 0x00, 0xc8,
    0x00, 0x05, 0x00, 0x00, 0x4e, 0x20,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x06, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x08, 0x00, 0x00, 0x00, 0x00]).buffer);

  assert.throws(() => {
    http2.getUnpackedSettings(packed);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message:
        'The "buf" argument must be an instance of Buffer or TypedArray.' +
        common.invalidArgTypeHelper(packed)
  });
}

{
  const packed = Buffer.from([
    0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x08, 0x00, 0x00, 0x00, 0x00]);

  const settings = http2.getUnpackedSettings(packed, { validate: true });
  assert.strictEqual(settings.enablePush, false);
  assert.strictEqual(settings.enableConnectProtocol, false);
}
{
  const packed = Buffer.from([
    0x00, 0x02, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x08, 0x00, 0x00, 0x00, 0x64]);

  const settings = http2.getUnpackedSettings(packed, { validate: true });
  assert.strictEqual(settings.enablePush, true);
  assert.strictEqual(settings.enableConnectProtocol, true);
}

// Verify that passing {validate: true} does not throw.
{
  const packed = Buffer.from([
    0x00, 0x01, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x03, 0x00, 0x00, 0x00, 0xc8,
    0x00, 0x05, 0x00, 0x00, 0x4e, 0x20,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x06, 0x00, 0x00, 0x00, 0x64,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x08, 0x00, 0x00, 0x00, 0x00]);

  http2.getUnpackedSettings(packed, { validate: true });
}

// Check for maxFrameSize failing the max number.
{
  const packed = Buffer.from([0x00, 0x05, 0x01, 0x00, 0x00, 0x00]);

  assert.throws(() => {
    http2.getUnpackedSettings(packed, { validate: true });
  }, {
    code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
    name: 'RangeError',
    message: 'Invalid value for setting "maxFrameSize": 16777216'
  });
}
