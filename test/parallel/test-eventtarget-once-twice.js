'use strict';
require('../common');
const { once } = require('node:events');
const { test } = require('node:test');
const { setImmediate } = require('node:timers/promises');

test('should resolve `once` twice', async () => {

  const et = new EventTarget();

  await Promise.all([
    (async () => {
      await once(et, 'foo');
      await once(et, 'foo');
    })(),

    (async () => {
      et.dispatchEvent(new Event('foo'));
      await setImmediate();
      et.dispatchEvent(new Event('foo'));
    })(),
  ]);

});
