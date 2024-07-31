'use strict';

require('../common');
const assert = require('assert');

const eventPhases = {
  'NONE': 0,
  'CAPTURING_PHASE': 1,
  'AT_TARGET': 2,
  'BUBBLING_PHASE': 3
};

for (const [prop, value] of Object.entries(eventPhases)) {
  // Check if the value of the property matches the expected value
  assert.strictEqual(Event[prop], value, `Expected Event.${prop} to be ${value}, but got ${Event[prop]}`);

  const desc = Object.getOwnPropertyDescriptor(Event, prop);
  assert.strictEqual(desc.writable, false, `${prop} should not be writable`);
  assert.strictEqual(desc.configurable, false, `${prop} should not be configurable`);
  assert.strictEqual(desc.enumerable, true, `${prop} should be enumerable`);
}
