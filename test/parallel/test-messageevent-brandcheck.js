'use strict';

require('../common');
const assert = require('assert');

[
  'data',
  'origin',
  'lastEventId',
  'source',
  'ports',
].forEach((i) => {
  assert.throws(() => Reflect.get(MessageEvent.prototype, i, {}), TypeError);
});
