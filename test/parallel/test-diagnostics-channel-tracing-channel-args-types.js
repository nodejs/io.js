'use strict';

require('../common');
const dc = require('diagnostics_channel');
const assert = require('assert');

let channel;

// tracingChannel creating with name
channel = dc.tracingChannel('test');
assert.strictEqual(channel.start.name, 'tracing:test:start');

// tracingChannel creating with channels
channel = dc.tracingChannel({
  start: dc.channel('tracing:test:start'),
  end: dc.channel('tracing:test:end'),
  asyncStart: dc.channel('tracing:test:asyncStart'),
  asyncEnd: dc.channel('tracing:test:asyncEnd'),
  error: dc.channel('tracing:test:error'),
});

// tracingChannel creating without nameOrChannels must throw TypeError
assert.throws(() => (channel = dc.tracingChannel(0)), {
  code: 'ERR_INVALID_ARG_TYPE',
  name: 'TypeError',
  message:
    /The "nameOrChannels" argument must be of type string or an instance of TracingChannel or Object/,
});

// tracingChannel creating without instance of Channel must throw error
assert.throws(() => (channel = dc.tracingChannel({ start: '' })), {
  code: 'ERR_INVALID_ARG_TYPE',
  message: /The "nameOrChannels\.start" property must be an instance of Channel/,
});

// tracingChannel creating with empty nameOrChannels must throw error
assert.throws(() => (channel = dc.tracingChannel({})), {
  message: /Cannot convert undefined or null to object/,
});
