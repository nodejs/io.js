// Flags: --expose-internals
import * as common from '../common/index.mjs';
import { describe, it } from 'node:test';
import assert from 'node:assert';
import { spawn } from 'node:child_process';
import { writeFileSync } from 'node:fs';
import tmpdir from '../common/tmpdir.js';

if (common.isIBMi)
  common.skip('IBMi does not support `fs.watch()`');

if (common.isAIX)
  common.skip('folder watch capability is limited in AIX.');

tmpdir.refresh();

// Set up test files and dependencies
const fixtureContent = {
  'dependency.js': 'module.exports = {};',
  'test.js': `
const test = require('node:test');
require('./dependency.js');
test('first test has ran');`,
  'test-2.js': `
const test = require('node:test');
require('./dependency.js');
test('second test has ran');`,
};

const fixturePaths = Object.fromEntries(Object.keys(fixtureContent)
  .map((file) => [file, tmpdir.resolve(file)]));

Object.entries(fixtureContent)
  .forEach(([file, content]) => writeFileSync(fixturePaths[file], content));

describe('test runner watch mode with more complex setup', () => {
  it('should re-run appropriate tests when dependencies change', async () => {
    // Start the test runner in watch mode
    const child = spawn(process.execPath,
                        ['--watch', '--test'],
                        { encoding: 'utf8', stdio: 'pipe', cwd: tmpdir.path });

    let currentRunOutput = '';
    const testRuns = [];

    const firstRunCompleted = Promise.withResolvers();
    const secondRunCompleted = Promise.withResolvers();
    const thirdRunCompleted = Promise.withResolvers();
    const fourthRunCompleted = Promise.withResolvers();

    child.stdout.on('data', (data) => {
      const str = data.toString();
      currentRunOutput += str;

      if (/duration_ms\s\d+/.test(str)) {
        // Test run has completed
        testRuns.push(currentRunOutput);
        currentRunOutput = '';
        switch (testRuns.length) {
          case 1:
            firstRunCompleted.resolve();
            break;
          case 2:
            secondRunCompleted.resolve();
            break;
          case 3:
            thirdRunCompleted.resolve();
            break;
          case 4:
            fourthRunCompleted.resolve();
            break;
        }
      }
    });

    // Wait for the initial test run to complete
    await firstRunCompleted.promise;

    // Modify 'dependency.js' to trigger re-run of both tests
    writeFileSync(fixturePaths['dependency.js'], 'module.exports = { modified: true };');

    // Wait for the second test run to complete
    await secondRunCompleted.promise;

    // Modify 'test.js' to trigger re-run of only 'test.js'
    writeFileSync(fixturePaths['test.js'], `
const test = require('node:test');
require('./dependency.js');
test('first test has ran again');`);

    // Wait for the third test run to complete
    await thirdRunCompleted.promise;

    // Modify 'dependency.js' again to trigger re-run of both tests
    writeFileSync(fixturePaths['dependency.js'], 'module.exports = { modified: true, again: true };');

    // Wait for the fourth test run to complete
    await fourthRunCompleted.promise;

    // Kill the child process
    child.kill();

    // Analyze the test runs
    assert.strictEqual(testRuns.length, 4);

    // First test run - Both tests should run
    const firstRunOutput = testRuns[0];
    assert.match(firstRunOutput, /first test has ran/);
    assert.match(firstRunOutput, /second test has ran/);

    // Second test run - We have modified 'dependency.js' only, so both tests should re-run
    const secondRunOutput = testRuns[1];
    assert.match(secondRunOutput, /first test has ran/);
    assert.match(secondRunOutput, /second test has ran/);

    // Third test run - We have modified 'test.js' only
    const thirdRunOutput = testRuns[2];
    assert.match(thirdRunOutput, /first test has ran again/);
    assert.doesNotMatch(thirdRunOutput, /second test has ran/);

    // Fourth test run - We have modified 'dependency.js' again, so both tests should re-run
    const fourthRunOutput = testRuns[3];
    assert.match(fourthRunOutput, /first test has ran again/);
    assert.match(fourthRunOutput, /second test has ran/);
  });
});
