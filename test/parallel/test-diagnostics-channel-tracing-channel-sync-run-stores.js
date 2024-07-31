'use strict';

const common = require('../common');
const { AsyncLocalStorage } = require('async_hooks');
const dc = require('diagnostics_channel');
const assert = require('assert');

const channel = dc.tracingChannel('test');
const store = new AsyncLocalStorage();

const context = { foo: 'bar' };

channel.start.bindStore(store, common.mustCall(() => {
  return context;
}));

assert.strictEqual(store.getStore(), undefined);
channel.traceSync(common.mustCall(() => {
  assert.deepStrictEqual(store.getStore(), context);
}));
assert.strictEqual(store.getStore(), undefined);
