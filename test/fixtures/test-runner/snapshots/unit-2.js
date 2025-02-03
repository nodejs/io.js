'use strict';
const { snapshot, test } = require('node:test');
const { basename, join } = require('node:path');

snapshot.setResolveSnapshotPath((testFile) => {
  return join(process.cwd(), `${basename(testFile)}.snapshot`);
});

test('has a snapshot', (t) => {
  t.assert.snapshot('a snapshot from ' + __filename);
});
