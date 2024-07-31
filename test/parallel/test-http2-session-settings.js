'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const h2 = require('http2');

const server = h2.createServer({
  remoteCustomSettings: [
    55,
  ],
  settings: {
    customSettings: {
      1244: 456
    }
  }
}
);

server.on(
  'stream',
  common.mustCall((stream) => {
    const assertSettings = (settings) => {
      assert.strictEqual(typeof settings, 'object');
      assert.strictEqual(typeof settings.headerTableSize, 'number');
      assert.strictEqual(typeof settings.enablePush, 'boolean');
      assert.strictEqual(typeof settings.initialWindowSize, 'number');
      assert.strictEqual(typeof settings.maxFrameSize, 'number');
      assert.strictEqual(typeof settings.maxConcurrentStreams, 'number');
      assert.strictEqual(typeof settings.maxHeaderListSize, 'number');
      assert.strictEqual(typeof settings.maxHeaderSize, 'number');
      assert.strictEqual(typeof settings.customSettings, 'object');
      let countCustom = 0;
      if (settings.customSettings[55]) {
        assert.strictEqual(typeof settings.customSettings[55], 'number');
        assert.strictEqual(settings.customSettings[55], 12);
        countCustom++;
      }
      if (settings.customSettings[155]) {
        // Should not happen actually
        assert.strictEqual(typeof settings.customSettings[155], 'number');
        countCustom++;
      }
      if (settings.customSettings[1244]) {
        assert.strictEqual(typeof settings.customSettings[1244], 'number');
        assert.strictEqual(settings.customSettings[1244], 456);
        countCustom++;
      }
      assert.strictEqual(countCustom, 1);
    };

    const localSettings = stream.session.localSettings;
    const remoteSettings = stream.session.remoteSettings;
    assertSettings(localSettings);
    assertSettings(remoteSettings);

    // Test that stored settings are returned when called for second time
    assert.strictEqual(stream.session.localSettings, localSettings);
    assert.strictEqual(stream.session.remoteSettings, remoteSettings);

    stream.respond({
      'content-type': 'text/html',
      ':status': 200
    });
    stream.end('hello world');
  })
);

server.on('session', (session) => {
  session.settings({
    maxConcurrentStreams: 2
  });
});

server.listen(
  0,
  common.mustCall(() => {
    const client = h2.connect(`http://localhost:${server.address().port}`, {
      settings: {
        enablePush: false,
        initialWindowSize: 123456,
        customSettings: {
          55: 12,
          155: 144 // should not arrive
        },
      },
      remoteCustomSettings: [
        1244,
      ]
    });

    client.on(
      'localSettings',
      common.mustCall((settings) => {
        assert(settings);
        assert.strictEqual(settings.enablePush, false);
        assert.strictEqual(settings.initialWindowSize, 123456);
        assert.strictEqual(settings.maxFrameSize, 16384);
        assert.strictEqual(settings.customSettings[55], 12);
      }, 2)
    );

    let calledOnce = false;
    client.on(
      'remoteSettings',
      common.mustCall((settings) => {
        assert(settings);
        assert.strictEqual(
          settings.maxConcurrentStreams,
          calledOnce ? 2 : (2 ** 32) - 1
        );
        calledOnce = true;
      }, 2)
    );

    const headers = { ':path': '/' };

    const req = client.request(headers);

    req.on('ready', common.mustCall(() => {
      // pendingSettingsAck will be true if a SETTINGS frame
      // has been sent but we are still waiting for an acknowledgement
      assert(client.pendingSettingsAck);
    }));

    // State will only be valid after connect event is emitted
    req.on('ready', common.mustCall(() => {
      client.settings({ maxHeaderListSize: 1 }, common.mustCall());

      // Verify valid error ranges
      [
        ['headerTableSize', -1],
        ['headerTableSize', 2 ** 32],
        ['initialWindowSize', -1],
        ['initialWindowSize', 2 ** 32],
        ['maxFrameSize', 16383],
        ['maxFrameSize', 2 ** 24],
        ['maxHeaderListSize', -1],
        ['maxHeaderListSize', 2 ** 32],
        ['maxHeaderSize', -1],
        ['maxHeaderSize', 2 ** 32],
      ].forEach(([key, value]) => {
        const settings = {};
        settings[key] = value;
        assert.throws(
          () => client.settings(settings),
          {
            name: 'RangeError',
            code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
            message: `Invalid value for setting "${key}": ${value}`
          }
        );
      });

      // Same tests as for the client on customSettings
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
          55: 0x100000000,
        } }),
        {
          code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
          name: 'RangeError'
        }
      );

      assert.throws(
        () => client.settings({ customSettings: {
          55: -1,
        } }),
        {
          code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
          name: 'RangeError'
        }
      );

      // Error checks for enablePush
      [1, {}, 'test', [], null, Infinity, NaN].forEach((i) => {
        assert.throws(
          () => client.settings({ enablePush: i }),
          {
            name: 'TypeError',
            code: 'ERR_HTTP2_INVALID_SETTING_VALUE',
            message: `Invalid value for setting "enablePush": ${i}`
          }
        );
      });
    }));

    req.on('response', common.mustCall());
    req.resume();
    req.on('end', common.mustCall(() => {
      server.close();
      client.close();
    }));
    req.end();
  })
);
