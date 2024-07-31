'use strict';

// This tests snapshot JS API using the example in the docs.

require('../common');
const assert = require('assert');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');
const { spawnSyncAndAssert, spawnSyncAndExitWithoutError } = require('../common/child_process');
const fs = require('fs');

const v8 = require('v8');

// By default it should be false. We'll test that it's true in snapshot
// building mode in the fixture.
assert(!v8.startupSnapshot.isBuildingSnapshot());

tmpdir.refresh();
const blobPath = tmpdir.resolve('snapshot.blob');
const entry = fixtures.path('snapshot', 'v8-startup-snapshot-api.js');
{
  for (const book of [
    'book1.en_US.txt',
    'book1.es_ES.txt',
    'book2.zh_CN.txt',
  ]) {
    const content = `This is ${book}`;
    fs.writeFileSync(tmpdir.resolve(book), content, 'utf8');
  }
  fs.copyFileSync(entry, tmpdir.resolve('entry.js'));
  spawnSyncAndExitWithoutError(process.execPath, [
    '--snapshot-blob',
    blobPath,
    '--build-snapshot',
    'entry.js',
  ], {
    cwd: tmpdir.path
  });
  const stats = fs.statSync(tmpdir.resolve('snapshot.blob'));
  assert(stats.isFile());
}

{
  spawnSyncAndAssert(process.execPath, [
    '--snapshot-blob',
    blobPath,
    'book1',
  ], {
    cwd: tmpdir.path,
    env: {
      ...process.env,
      BOOK_LANG: 'en_US',
    }
  }, {
    stderr: 'Reading book1.en_US.txt',
    stdout: 'This is book1.en_US.txt',
    trim: true
  });
}
