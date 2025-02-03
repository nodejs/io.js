'use strict';
const { suite, test } = require('node:test');

suite('imported suite', () => {
  test('imported test', (t) => {
    t.assert.snapshot({ foo: 1, bar: 2 });
  });
});
