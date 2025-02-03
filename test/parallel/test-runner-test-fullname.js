'use strict';
require('../common');
const { strictEqual } = require('node:assert');
const { suite, test } = require('node:test');

suite('suite', (t) => {
  strictEqual(t.fullName, 'suite');

  test('test', (t) => {
    strictEqual(t.fullName, 'suite > test');

    t.test('subtest', (t) => {
      strictEqual(t.fullName, 'suite > test > subtest');

      t.test('subsubtest', (t) => {
        strictEqual(t.fullName, 'suite > test > subtest > subsubtest');
      });
    });
  });
});

test((t) => {
  strictEqual(t.fullName, '<anonymous>');
});
