import * as common from '../common/index.mjs';
import { describe, it } from 'node:test';
import assert from 'node:assert';
import { spawn } from 'node:child_process';
import { writeFile, unlink } from 'node:fs/promises';
import { setTimeout } from 'node:timers/promises';
import tmpdir from '../common/tmpdir.js';

if (common.isIBMi)
  common.skip('IBMi does not support `fs.watch()`');

if (common.isAIX)
  common.skip('folder watch capability is limited in AIX.');

tmpdir.refresh();

// This test updates these files repeatedly,
// Reading them from disk is unreliable due to race conditions.
const fixtureContent = {
  'dependency.js': 'module.exports = {};',
  'dependency.mjs': 'export const a = 1;',
  // Test 1
  'test.js': `
const test = require('node:test');
require('./dependency.js');
import('./dependency.mjs');
import('data:text/javascript,');
test('first test has ran');`,
  // Test 2
  'test-2.mjs': `
import test from 'node:test';
import './dependency.js';
import { a } from './dependency.mjs';
import 'data:text/javascript,';
test('second test has ran');`,
  // Test file to be deleted
  'test-to-delete.mjs': `
import test from 'node:test';
import './dependency.js';
import { a } from './dependency.mjs';
import 'data:text/javascript,';
test('test to delete has ran');`,
};

const fixturePaths = Object.fromEntries(Object.keys(fixtureContent)
  .map((file) => [file, tmpdir.resolve(file)]));

async function setupFixtures() {
  return Promise.all(Object.entries(fixtureContent)
    .map(([file, content]) => writeFile(fixturePaths[file], content, { flush: true })));
}

await setupFixtures();

describe('test runner watch mode with more complex setup', () => {
  it('should run tests when a dependency changed after a watched test file being deleted', async () => {
    const ran1 = Promise.withResolvers();
    const ran2 = Promise.withResolvers();
    const child = spawn(process.execPath,
                        ['--watch', '--test'],
                        { encoding: 'utf8', stdio: 'pipe', cwd: tmpdir.path });
    let stdout = '';
    let currentRun = '';
    const runs = [];

    child.stdout.on('data', (data) => {
      stdout += data.toString();
      currentRun += data.toString();
      const testRuns = stdout.match(/duration_ms\s\d+/g);

      if (testRuns?.length >= 2) {
        ran2.resolve();
        return;
      }
      if (testRuns?.length >= 1) ran1.resolve();
    });

    await ran1.promise;
    runs.push(currentRun);
    currentRun = '';
    const fileToDeletePathLocal = tmpdir.resolve('test-to-delete.mjs');
    await unlink(fileToDeletePathLocal);
    await setTimeout(common.platformTimeout(1000));

    const content = fixtureContent['dependency.mjs'];
    const path = fixturePaths['dependency.mjs'];
    await writeFile(path, content, { flush: true });
    await setTimeout(common.platformTimeout(1000));
    await ran2.promise;
    runs.push(currentRun);
    currentRun = '';
    child.kill();

    assert.strictEqual(runs.length, 2);

    const [firstRun, secondRun] = runs;

    assert.match(firstRun, /tests 3/);
    assert.match(firstRun, /pass 3/);
    assert.match(firstRun, /fail 0/);
    assert.match(firstRun, /cancelled 0/);

    assert.match(secondRun, /tests 2/);
    assert.match(secondRun, /pass 2/);
    assert.match(secondRun, /fail 0/);
    assert.match(secondRun, /cancelled 0/);
  });
});
