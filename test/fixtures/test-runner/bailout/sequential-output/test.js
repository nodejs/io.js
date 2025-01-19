'use strict';

const common = require('../../../../common');
const test = require('node:test');

test('nested', (t) => {
  t.test('first', () => {});
  t.test('second', () => {
    throw new Error();
  });
  t.test('third', common.mustNotCall(() => {}));
});

test('top level', (t) => {
  t.test('fourth', () => {});
  t.test('fifth', () => {
    throw new Error();
  });
});
