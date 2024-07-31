// Flags: --test-force-exit --test-reporter=spec
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

test('passes but oops', () => {
  setTimeout(() => {
    throw new Error('this should not have a chance to be thrown');
  }, 1000);
});

test('also passes');
