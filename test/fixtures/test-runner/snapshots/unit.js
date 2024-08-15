'use strict';
const { snapshot, suite, test } = require('node:test');
const { basename, join } = require('node:path');

snapshot.setResolveSnapshotPath((testFile) => {
  return join(process.cwd(), `${basename(testFile)}.snapshot`);
});

suite('suite', () => {
  test('test with plan', (t) => {
    t.plan(2);
    t.assert.snapshot({ foo: 1, bar: 2 });
    t.assert.snapshot(5);
  });
});

test('test', async (t) => {
  t.assert.snapshot({ baz: 9 });
});

test('`${foo}`', async (t) => {
  const options = { serializers: [() => { return '***'; }]};
  t.assert.snapshot('snapshotted string', options);
});

test('escapes in `\\${foo}`\n', async (t) => {
  t.assert.snapshot('`\\${foo}`\n');
});

require('./imported-tests');
