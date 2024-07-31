// Flags: --test-reporter=spec
'use strict';
const { after, afterEach, before, beforeEach, test } = require('node:test');

before(() => {
  console.log('BEFORE');
});

beforeEach(() => {
  console.log('BEFORE EACH');
});

after(() => {
  console.log('AFTER');
});

afterEach(() => {
  console.log('AFTER EACH');
});

test('skipped test', { skip: true });
