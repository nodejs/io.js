'use strict';
const { test } = require('node:test');

test('snapshot file path is created', (t) => {
  t.assert.fileSnapshot({ baz: 9 }, './foo/bar/baz/1.json');
});

test('test with plan', (t) => {
  t.plan(2);
  t.assert.fileSnapshot({ foo: 1, bar: 2 }, '2.json');
  t.assert.fileSnapshot(5, '3.txt');
});

test('custom serializers are supported', (t) => {
  t.assert.fileSnapshot({ foo: 1 }, '4.txt', {
    serializers: [
      (value) => { return value + '424242'; },
      (value) => { return JSON.stringify(value); },
    ]
  });
});
