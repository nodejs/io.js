// Flags: --expose-internals
import * as common from '../common/index.mjs';
import { describe, it } from 'node:test';
import assert from 'node:assert';
import tmpdir from '../common/tmpdir.js';
import watcher from 'internal/watch_mode/files_watcher';
import { writeFileSync } from 'node:fs';

if (common.isIBMi)
  common.skip('IBMi does not support `fs.watch()`');

if (common.isAIX)
  common.skip('folder watch capability is limited in AIX.');

const { FilesWatcher } = watcher;

tmpdir.refresh();

// Set up test files and dependencies
const fixtureContent = {
  'dependency.js': 'module.exports = {};',
  'test.js': 'require(\'./dependency.js\');',
  'test-2.js': 'require(\'./dependency.js\');',
};

const fixturePaths = Object.fromEntries(Object.keys(fixtureContent)
  .map((file) => [file, tmpdir.resolve(file)]));

Object.entries(fixtureContent)
  .forEach(([file, content]) => writeFileSync(fixturePaths[file], content));

describe('watch file with shared dependency', () => {
  it('should not remove shared dependencies when unfiltering an owner', (t, done) => {
    const controller = new AbortController();
    const watcher = new FilesWatcher({ signal: controller.signal });

    watcher.on('changed', ({ owners }) => {
      if (owners.size !== 2) return;

      // If this code is never reached the test times out.
      assert.ok(owners.has(fixturePaths['test.js']));
      assert.ok(owners.has(fixturePaths['test-2.js']));
      controller.abort();
      done();
    });
    watcher.filterFile(fixturePaths['test.js']);
    watcher.filterFile(fixturePaths['test-2.js']);
    watcher.filterFile(fixturePaths['dependency.js'], fixturePaths['test.js']);
    watcher.filterFile(fixturePaths['dependency.js'], fixturePaths['test-2.js']);
    watcher.unfilterFilesOwnedBy([fixturePaths['test.js']]);
    watcher.filterFile(fixturePaths['test.js']);
    watcher.filterFile(fixturePaths['dependency.js'], fixturePaths['test.js']);

    if (common.isMacOS) {
      // Do the write with a delay to ensure that the OS is ready to notify us.
      setTimeout(() => {
        writeFileSync(
          fixturePaths['dependency.js'],
          'module.exports = { modified: true };'
        );
      }, common.platformTimeout(200));
    } else {
      writeFileSync(
        fixturePaths['dependency.js'],
        'module.exports = { modified: true };'
      );
    }
  });
});
