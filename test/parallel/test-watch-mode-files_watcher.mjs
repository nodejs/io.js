// Flags: --expose-internals
import * as common from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import tmpdir from '../common/tmpdir.js';
import path from 'node:path';
import assert from 'node:assert';
import process from 'node:process';
import { describe, it, beforeEach, afterEach } from 'node:test';
import { writeFileSync, mkdirSync } from 'node:fs';
import { setTimeout } from 'node:timers/promises';
import { once } from 'node:events';
import { spawn } from 'node:child_process';
import watcher from 'internal/watch_mode/files_watcher';

if (common.isIBMi)
  common.skip('IBMi does not support `fs.watch()`');

const supportsRecursiveWatching = common.isMacOS || common.isWindows;

const { FilesWatcher } = watcher;
tmpdir.refresh();

describe('watch mode file watcher', () => {
  let watcher;
  let changesCount;

  beforeEach(() => {
    changesCount = 0;
    watcher = new FilesWatcher({ debounce: 100 });
    watcher.on('changed', () => changesCount++);
  });

  afterEach(() => watcher.clear());

  let counter = 0;
  function writeAndWaitForChanges(watcher, file) {
    return new Promise((resolve) => {
      const interval = setInterval(() => writeFileSync(file, `write ${counter++}`), 100);
      watcher.once('changed', () => {
        clearInterval(interval);
        resolve();
      });
    });
  }

  it('should watch changed files', async () => {
    const file = tmpdir.resolve('file1');
    writeFileSync(file, 'written');
    watcher.filterFile(file);
    await writeAndWaitForChanges(watcher, file);
    assert.strictEqual(changesCount, 1);
  });

  it('should debounce changes', async () => {
    const file = tmpdir.resolve('file2');
    writeFileSync(file, 'written');
    watcher.filterFile(file);
    await writeAndWaitForChanges(watcher, file);

    writeFileSync(file, '1');
    writeFileSync(file, '2');
    writeFileSync(file, '3');
    writeFileSync(file, '4');
    await setTimeout(200); // debounce * 2
    writeFileSync(file, '5');
    const changed = once(watcher, 'changed');
    writeFileSync(file, 'after');
    await changed;
    // Unfortunately testing that changesCount === 2 is flaky
    assert.ok(changesCount < 5);
  });

  it('should debounce changes on multiple files', async () => {
    const files = [];
    for (let i = 0; i < 10; i++) {
      const file = tmpdir.resolve(`file-debounced-${i}`);
      writeFileSync(file, 'written');
      watcher.filterFile(file);
      files.push(file);
    }

    files.forEach((file) => writeFileSync(file, '1'));
    files.forEach((file) => writeFileSync(file, '2'));
    files.forEach((file) => writeFileSync(file, '3'));
    files.forEach((file) => writeFileSync(file, '4'));

    await setTimeout(200); // debounce * 2
    files.forEach((file) => writeFileSync(file, '5'));
    const changed = once(watcher, 'changed');
    files.forEach((file) => writeFileSync(file, 'after'));
    await changed;
    // Unfortunately testing that changesCount === 2 is flaky
    assert.ok(changesCount < 5);
  });

  it('should ignore files in watched directory if they are not filtered',
     { skip: !supportsRecursiveWatching }, async () => {
       watcher.on('changed', common.mustNotCall());
       watcher.watchPath(tmpdir.path);
       writeFileSync(tmpdir.resolve('file3'), '1');
       // Wait for this long to make sure changes are not triggered
       await setTimeout(1000);
     });

  it('should allow clearing filters', async () => {
    const file = tmpdir.resolve('file4');
    writeFileSync(file, 'written');
    watcher.filterFile(file);
    await writeAndWaitForChanges(watcher, file);

    writeFileSync(file, '1');
    assert.strictEqual(changesCount, 1);

    watcher.clearFileFilters();
    writeFileSync(file, '2');
    // Wait for this long to make sure changes are triggered only once
    await setTimeout(1000);
    assert.strictEqual(changesCount, 1);
  });

  it('should watch all files in watched path when in "all" mode',
     { skip: !supportsRecursiveWatching }, async () => {
       watcher = new FilesWatcher({ debounce: 100, mode: 'all' });
       watcher.on('changed', () => changesCount++);

       const file = tmpdir.resolve('file5');
       watcher.watchPath(tmpdir.path);

       const changed = once(watcher, 'changed');
       await setTimeout(common.platformTimeout(100)); // avoid throttling
       writeFileSync(file, 'changed');
       await changed;
       assert.strictEqual(changesCount, 1);
     });

  it('should ruse existing watcher if it exists',
     { skip: !supportsRecursiveWatching }, () => {
       assert.deepStrictEqual(watcher.watchedPaths, []);
       watcher.watchPath(tmpdir.path);
       assert.deepStrictEqual(watcher.watchedPaths, [tmpdir.path]);
       watcher.watchPath(tmpdir.path);
       assert.deepStrictEqual(watcher.watchedPaths, [tmpdir.path]);
     });

  it('should ruse existing watcher of a parent directory',
     { skip: !supportsRecursiveWatching }, () => {
       assert.deepStrictEqual(watcher.watchedPaths, []);
       watcher.watchPath(tmpdir.path);
       assert.deepStrictEqual(watcher.watchedPaths, [tmpdir.path]);
       watcher.watchPath(tmpdir.resolve('subdirectory'));
       assert.deepStrictEqual(watcher.watchedPaths, [tmpdir.path]);
     });

  it('should remove existing watcher if adding a parent directory watcher',
     { skip: !supportsRecursiveWatching }, () => {
       assert.deepStrictEqual(watcher.watchedPaths, []);
       const subdirectory = tmpdir.resolve('subdirectory');
       mkdirSync(subdirectory);
       watcher.watchPath(subdirectory);
       assert.deepStrictEqual(watcher.watchedPaths, [subdirectory]);
       watcher.watchPath(tmpdir.path);
       assert.deepStrictEqual(watcher.watchedPaths, [tmpdir.path]);
     });

  it('should clear all watchers when calling clear',
     { skip: !supportsRecursiveWatching }, () => {
       assert.deepStrictEqual(watcher.watchedPaths, []);
       watcher.watchPath(tmpdir.path);
       assert.deepStrictEqual(watcher.watchedPaths, [tmpdir.path]);
       watcher.clear();
       assert.deepStrictEqual(watcher.watchedPaths, []);
     });

  it('should watch files from subprocess IPC events', async () => {
    const file = fixtures.path('watch-mode/ipc.js');
    const child = spawn(process.execPath, [file], { stdio: ['pipe', 'pipe', 'pipe', 'ipc'], encoding: 'utf8' });
    watcher.watchChildProcessModules(child);
    await once(child, 'exit');
    let expected = [file, tmpdir.resolve('file')];
    if (supportsRecursiveWatching) {
      expected = expected.map((file) => path.dirname(file));
    }
    assert.deepStrictEqual(watcher.watchedPaths, expected);
  });
});
