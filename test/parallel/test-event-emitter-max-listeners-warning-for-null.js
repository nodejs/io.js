// Flags: --no-warnings
// The flag suppresses stderr output but the warning event will still emit
'use strict';

const common = require('../common');
const events = require('events');
const assert = require('assert');

const e = new events.EventEmitter();
e.setMaxListeners(1);

process.on('warning', common.mustCall((warning) => {
  assert.ok(warning instanceof Error);
  assert.strictEqual(warning.name, 'MaxListenersExceededWarning');
  assert.strictEqual(warning.emitter, e);
  assert.strictEqual(warning.count, 2);
  assert.strictEqual(warning.type, null);
  assert.ok(warning.message.includes(
    '2 null listeners added to [EventEmitter]. MaxListeners is 1.'));
}));

e.on(null, () => {});
e.on(null, () => {});
