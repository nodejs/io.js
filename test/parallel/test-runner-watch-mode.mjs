import * as common from '../common/index.mjs';
import { describe, it, beforeEach } from 'node:test';
import { once } from 'node:events';
import assert from 'node:assert';
import { spawn } from 'node:child_process';
import { writeFileSync, renameSync, unlinkSync } from 'node:fs';
import { setTimeout } from 'node:timers/promises';
import tmpdir from '../common/tmpdir.js';

if (common.isIBMi)
  common.skip('IBMi does not support `fs.watch()`');

if (common.isAIX)
  common.skip('folder watch capability is limited in AIX.');

let fixturePaths;

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

function refresh() {
  tmpdir.refresh();
  fixturePaths = Object.keys(fixtureContent)
    .reduce((acc, file) => ({ ...acc, [file]: tmpdir.resolve(file) }), {});
  Object.entries(fixtureContent)
    .forEach(([file, content]) => writeFileSync(fixturePaths[file], content));
}

async function testWatch({
  fileToUpdate,
  file,
  action = 'update',
  fileToCreate,
  isolation,
}) {
  const ran1 = Promise.withResolvers();
  const ran2 = Promise.withResolvers();
  const child = spawn(process.execPath,
                      ['--watch', '--test', '--test-reporter=spec',
                       isolation ? `--test-isolation=${isolation}` : '',
                       file ? fixturePaths[file] : undefined].filter(Boolean),
                      { encoding: 'utf8', stdio: 'pipe', cwd: tmpdir.path });
  let stdout = '';
  let currentRun = '';
  const runs = [];

  child.stdout.on('data', (data) => {
    stdout += data.toString();
    currentRun += data.toString();
    const testRuns = stdout.match(/duration_ms\s\d+/g);
    if (testRuns?.length >= 1) ran1.resolve();
    if (testRuns?.length >= 2) ran2.resolve();
  });

  const testUpdate = async () => {
    await ran1.promise;
    runs.push(currentRun);
    currentRun = '';
    const content = fixtureContent[fileToUpdate];
    const path = fixturePaths[fileToUpdate];
    writeFileSync(path, content);
    await setTimeout(common.platformTimeout(1000));
    await ran2.promise;
    runs.push(currentRun);
    child.kill();
    await once(child, 'exit');

    assert.strictEqual(runs.length, 2);

    for (const run of runs) {
      assert.match(run, /tests 1/);
      assert.match(run, /pass 1/);
      assert.match(run, /fail 0/);
      assert.match(run, /cancelled 0/);
    }
  };

  const testRename = async () => {
    await ran1.promise;
    runs.push(currentRun);
    currentRun = '';
    const fileToRenamePath = tmpdir.resolve(fileToUpdate);
    const newFileNamePath = tmpdir.resolve(`test-renamed-${fileToUpdate}`);
    renameSync(fileToRenamePath, newFileNamePath);
    await setTimeout(common.platformTimeout(1000));
    await ran2.promise;
    runs.push(currentRun);
    child.kill();
    await once(child, 'exit');

    assert.strictEqual(runs.length, 2);

    for (const run of runs) {
      assert.match(run, /tests 1/);
      assert.match(run, /pass 1/);
      assert.match(run, /fail 0/);
      assert.match(run, /cancelled 0/);
    }
  };

  const testDelete = async () => {
    await ran1.promise;
    runs.push(currentRun);
    currentRun = '';
    const fileToDeletePath = tmpdir.resolve(fileToUpdate);
    unlinkSync(fileToDeletePath);
    await setTimeout(common.platformTimeout(2000));
    ran2.resolve();
    runs.push(currentRun);
    child.kill();
    await once(child, 'exit');

    assert.strictEqual(runs.length, 2);

    for (const run of runs) {
      assert.doesNotMatch(run, /MODULE_NOT_FOUND/);
    }
  };

  const testCreate = async () => {
    await ran1.promise;
    runs.push(currentRun);
    currentRun = '';
    const newFilePath = tmpdir.resolve(fileToCreate);
    writeFileSync(newFilePath, 'module.exports = {};');
    await setTimeout(common.platformTimeout(1000));
    await ran2.promise;
    runs.push(currentRun);
    child.kill();
    await once(child, 'exit');

    for (const run of runs) {
      assert.match(run, /tests 1/);
      assert.match(run, /pass 1/);
      assert.match(run, /fail 0/);
      assert.match(run, /cancelled 0/);
    }
  };

  action === 'update' && await testUpdate();
  action === 'rename' && await testRename();
  action === 'delete' && await testDelete();
  action === 'create' && await testCreate();
}

describe('test runner watch mode', () => {
  beforeEach(refresh);
  for (const isolation of ['none', 'process']) {
    describe(`isolation: ${isolation}`, () => {
      it('should run tests repeatedly', async () => {
        await testWatch({ file: 'test.js', fileToUpdate: 'test.js', isolation });
      });

      it('should run tests with dependency repeatedly', async () => {
        await testWatch({ file: 'test.js', fileToUpdate: 'dependency.js', isolation });
      });

      it('should run tests with ESM dependency', async () => {
        await testWatch({ file: 'test.js', fileToUpdate: 'dependency.mjs', isolation });
      });

      it('should support running tests without a file', async () => {
        await testWatch({ fileToUpdate: 'test.js', isolation });
      });

      it('should support a watched test file rename', async () => {
        await testWatch({ fileToUpdate: 'test.js', action: 'rename', isolation });
      });

      it('should not throw when delete a watched test file', async () => {
        await testWatch({ fileToUpdate: 'test.js', action: 'delete', isolation });
      });

      it('should run new tests when a new file is created in the watched directory', {
        todo: isolation === 'none' ?
          'This test is failing when isolation is set to none and must be fixed' :
          undefined,
      }, async () => {
        await testWatch({ action: 'create', fileToCreate: 'new-test-file.test.js', isolation });
      });
    });
  }
});
