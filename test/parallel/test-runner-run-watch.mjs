// Flags: --expose-internals
import * as common from '../common/index.mjs';
import { describe, it, beforeEach } from 'node:test';
import assert from 'node:assert';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { writeFileSync, renameSync, unlinkSync, existsSync } from 'node:fs';
import util from 'internal/util';
import tmpdir from '../common/tmpdir.js';
import { join } from 'node:path';

if (common.isIBMi)
  common.skip('IBMi does not support `fs.watch()`');

// This test updates these files repeatedly,
// Reading them from disk is unreliable due to race conditions.
const fixtureContent = {
  'dependency.js': 'module.exports = {};',
  'dependency.mjs': 'export const a = 1;',
  'test.js': `
const test = require('node:test');
require('./dependency.js');
import('./dependency.mjs');
import('data:text/javascript,');
test('test has ran');`,
};

let fixturePaths;

function refresh() {
  tmpdir.refresh();

  fixturePaths = Object.keys(fixtureContent)
    .reduce((acc, file) => ({ ...acc, [file]: tmpdir.resolve(file) }), {});
  Object.entries(fixtureContent)
    .forEach(([file, content]) => writeFileSync(fixturePaths[file], content));
}

const runner = join(import.meta.dirname, '..', 'fixtures', 'test-runner-watch.mjs');

async function testWatch({ fileToUpdate, file, action = 'update', cwd = tmpdir.path }) {
  const ran1 = util.createDeferredPromise();
  const ran2 = util.createDeferredPromise();
  const args = [runner];
  if (file) args.push('--file', file);
  const child = spawn(process.execPath,
                      args,
                      { encoding: 'utf8', stdio: 'pipe', cwd });
  let stdout = '';
  let currentRun = '';
  const runs = [];

  child.stdout.on('data', (data) => {
    stdout += data.toString();
    currentRun += data.toString();
    const testRuns = stdout.match(/# duration_ms\s\d+/g);
    if (testRuns?.length >= 1) ran1.resolve();
    if (testRuns?.length >= 2) ran2.resolve();
  });

  const testUpdate = async () => {
    await ran1.promise;
    const content = fixtureContent[fileToUpdate];
    const path = fixturePaths[fileToUpdate];
    const interval = setInterval(() => writeFileSync(path, content), common.platformTimeout(1000));
    await ran2.promise;
    runs.push(currentRun);
    clearInterval(interval);
    child.kill();
    await once(child, 'exit');
    for (const run of runs) {
      assert.doesNotMatch(run, /run\(\) is being called recursively/);
      assert.match(run, /# tests 1/);
      assert.match(run, /# pass 1/);
      assert.match(run, /# fail 0/);
      assert.match(run, /# cancelled 0/);
    }
  };

  const testRename = async () => {
    await ran1.promise;
    const fileToRenamePath = tmpdir.resolve(fileToUpdate);
    const newFileNamePath = tmpdir.resolve(`test-renamed-${fileToUpdate}`);
    const interval = setInterval(() => renameSync(fileToRenamePath, newFileNamePath), common.platformTimeout(1000));
    await ran2.promise;
    runs.push(currentRun);
    clearInterval(interval);
    child.kill();
    await once(child, 'exit');

    for (const run of runs) {
      assert.doesNotMatch(run, /run\(\) is being called recursively/);
      if (action === 'rename2') {
        assert.match(run, /MODULE_NOT_FOUND/);
      } else {
        assert.doesNotMatch(run, /MODULE_NOT_FOUND/);
      }
      assert.match(run, /# tests 1/);
      assert.match(run, /# pass 1/);
      assert.match(run, /# fail 0/);
      assert.match(run, /# cancelled 0/);
    }
  };

  const testDelete = async () => {
    await ran1.promise;
    const fileToDeletePath = tmpdir.resolve(fileToUpdate);
    const interval = setInterval(() => {
      if (existsSync(fileToDeletePath)) {
        unlinkSync(fileToDeletePath);
      } else {
        ran2.resolve();
      }
    }, common.platformTimeout(1000));
    await ran2.promise;
    runs.push(currentRun);
    clearInterval(interval);
    child.kill();
    await once(child, 'exit');

    for (const run of runs) {
      assert.doesNotMatch(run, /MODULE_NOT_FOUND/);
    }
  };

  action === 'update' && await testUpdate();
  action === 'rename' && await testRename();
  action === 'rename2' && await testRename();
  action === 'delete' && await testDelete();
}

describe('test runner watch mode', () => {
  beforeEach(refresh);
  it('should run tests repeatedly', async () => {
    await testWatch({ file: 'test.js', fileToUpdate: 'test.js' });
  });

  it('should run tests with dependency repeatedly', async () => {
    await testWatch({ file: 'test.js', fileToUpdate: 'dependency.js' });
  });

  it('should run tests with ESM dependency', async () => {
    await testWatch({ file: 'test.js', fileToUpdate: 'dependency.mjs' });
  });

  it('should support running tests without a file', async () => {
    await testWatch({ fileToUpdate: 'test.js' });
  });

  it('should support a watched test file rename', async () => {
    await testWatch({ fileToUpdate: 'test.js', action: 'rename' });
  });

  it('should not throw when deleting a watched test file', { skip: common.isAIX }, async () => {
    await testWatch({ fileToUpdate: 'test.js', action: 'delete' });
  });

  it('should run tests with dependency repeatedly in a different cwd', async () => {
    await testWatch({
      file: join(tmpdir.path, 'test.js'),
      fileToUpdate: 'dependency.js',
      cwd: import.meta.dirname,
      action: 'rename2'
    });
  });

  it('should handle renames in a different cwd', async () => {
    await testWatch({
      file: join(tmpdir.path, 'test.js'),
      fileToUpdate: 'test.js',
      cwd: import.meta.dirname,
      action: 'rename2'
    });
  });
});
