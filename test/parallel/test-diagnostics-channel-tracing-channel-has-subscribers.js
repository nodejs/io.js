'use strict';

const common = require('../common');
const dc = require('diagnostics_channel');
const assert = require('assert');

const handler = common.mustNotCall();

{
  const handlers = {
    start: common.mustNotCall()
  };

  const channel = dc.tracingChannel('test');

  assert.strictEqual(channel.hasSubscribers, false);

  channel.subscribe(handlers);
  assert.strictEqual(channel.hasSubscribers, true);

  channel.unsubscribe(handlers);
  assert.strictEqual(channel.hasSubscribers, false);

  channel.start.subscribe(handler);
  assert.strictEqual(channel.hasSubscribers, true);

  channel.start.unsubscribe(handler);
  assert.strictEqual(channel.hasSubscribers, false);
}

{
  const handlers = {
    asyncEnd: common.mustNotCall()
  };

  const channel = dc.tracingChannel('test');

  assert.strictEqual(channel.hasSubscribers, false);

  channel.subscribe(handlers);
  assert.strictEqual(channel.hasSubscribers, true);

  channel.unsubscribe(handlers);
  assert.strictEqual(channel.hasSubscribers, false);

  channel.asyncEnd.subscribe(handler);
  assert.strictEqual(channel.hasSubscribers, true);

  channel.asyncEnd.unsubscribe(handler);
  assert.strictEqual(channel.hasSubscribers, false);
}
