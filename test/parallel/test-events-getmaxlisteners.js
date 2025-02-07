'use strict';

require('../common');
const assert = require('node:assert');
const { getMaxListeners, EventEmitter, defaultMaxListeners, setMaxListeners } = require('node:events');

{
  const ee = new EventEmitter();
  assert.strictEqual(getMaxListeners(ee), defaultMaxListeners);
  setMaxListeners(101, ee);
  assert.strictEqual(getMaxListeners(ee), 101);
}

{
  const et = new EventTarget();
  assert.strictEqual(getMaxListeners(et), defaultMaxListeners);
  setMaxListeners(101, et);
  assert.strictEqual(getMaxListeners(et), 101);
}

{
  const ac = new AbortController();
  assert.strictEqual(getMaxListeners(ac.signal), 0);
}
