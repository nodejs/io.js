'use strict';

// This test ensures that the Http2SecureServer and Http2Server
// settings are updated when the setting object is valid.
// When the setting object is invalid, this test ensures that
// updateSettings throws an exception.

const common = require('../common');
if (!common.hasCrypto) { common.skip('missing crypto'); }
const assert = require('assert');
const http2 = require('http2');

testUpdateSettingsWith({
  server: http2.createSecureServer(),
  newServerSettings: {
    'headerTableSize': 1,
    'initialWindowSize': 1,
    'maxConcurrentStreams': 1,
    'maxHeaderListSize': 1,
    'maxFrameSize': 16385,
    'enablePush': false,
    'enableConnectProtocol': true,
    'customSettings': { '9999': 301 }
  }
});
testUpdateSettingsWith({
  server: http2.createServer(),
  newServerSettings: {
    'enablePush': false
  }
});

function testUpdateSettingsWith({ server, newServerSettings }) {
  const oldServerSettings = getServerSettings(server);
  assert.notDeepStrictEqual(oldServerSettings, newServerSettings);
  server.updateSettings(newServerSettings);
  const updatedServerSettings = getServerSettings(server);
  assert.deepStrictEqual(updatedServerSettings, { ...oldServerSettings,
                                                  ...newServerSettings });
  assert.throws(() => server.updateSettings(''), {
    message: 'The "settings" argument must be of type object. ' +
    'Received type string (\'\')',
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError'
  });
  assert.throws(() => server.updateSettings({
    'maxHeaderListSize': 'foo'
  }), {
    message: 'Invalid value for setting "maxHeaderListSize": foo',
    code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
    name: 'RangeError'
  });
}

function getServerSettings(server) {
  const options = Object
                  .getOwnPropertySymbols(server)
                  .find((s) => s.toString() === 'Symbol(options)');
  return server[options].settings;
}
