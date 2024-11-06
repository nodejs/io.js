'use strict';
const common = require('../common');
const assert = require('node:assert/strict');
const { once } = require('node:events');
const { test } = require('node:test');
const { setImmediate } = require('node:timers/promises');

test('should resolve `once` twice', (t, done) => {

  const et = new EventTarget();

  (async () => {
    await once(et, 'foo');
    await once(et, 'foo');
    done();
  })();

  (async () => {
    et.dispatchEvent(new Event('foo'));
    await setImmediate();
    et.dispatchEvent(new Event('foo'));
  })();

});