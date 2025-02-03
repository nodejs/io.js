'use strict';

require('../common');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
const { describe, it } = require('node:test');
const { spawnSync } = require('node:child_process');
const assert = require('node:assert');
const fs = require('node:fs');

const testFile = fixtures.path('test-runner/reporters.js');
tmpdir.refresh();

let tmpFiles = 0;
describe('node:test reporters', { concurrency: true }, () => {
  it('should default to outputing TAP to stdout', async () => {
    const child = spawnSync(process.execPath, ['--test', testFile]);
    assert.strictEqual(child.stderr.toString(), '');
    assert.match(child.stdout.toString(), /✖ failing tests:/);
    assert.match(child.stdout.toString(), /✔ ok/);
    assert.match(child.stdout.toString(), /✖ failing/);
    assert.match(child.stdout.toString(), /✔ top level/);
  });

  it('should default destination to stdout when passing a single reporter', async () => {
    const child = spawnSync(process.execPath, ['--test', '--test-reporter', 'dot', testFile]);
    assert.strictEqual(child.stderr.toString(), '');
    assert.match(child.stdout.toString(), /\.XX\.\n/);
    assert.match(child.stdout.toString(), /Failed tests:/);
    assert.match(child.stdout.toString(), /✖ failing/);
    assert.match(child.stdout.toString(), /✖ nested/);
  });

  it('should throw when passing reporters without a destination', async () => {
    const child = spawnSync(process.execPath, ['--test', '--test-reporter', 'dot', '--test-reporter', 'tap', testFile]);
    assert.match(child.stderr.toString(), /The argument '--test-reporter' must match the number of specified '--test-reporter-destination'\. Received \[ 'dot', 'tap' \]/);
    assert.strictEqual(child.stdout.toString(), '');
  });

  it('should throw when passing a destination without a reporter', async () => {
    const child = spawnSync(process.execPath, ['--test', '--test-reporter-destination', 'tap', testFile]);
    assert.match(child.stderr.toString(), /The argument '--test-reporter' must match the number of specified '--test-reporter-destination'\. Received \[\]/);
    assert.strictEqual(child.stdout.toString(), '');
  });

  it('should support stdout as a destination', async () => {
    const child = spawnSync(process.execPath,
                            ['--test', '--test-reporter', 'dot', '--test-reporter-destination', 'stdout', testFile]);
    assert.strictEqual(child.stderr.toString(), '');
    assert.match(child.stdout.toString(), /\.XX\.\n/);
    assert.match(child.stdout.toString(), /Failed tests:/);
    assert.match(child.stdout.toString(), /✖ failing/);
    assert.match(child.stdout.toString(), /✖ nested/);
  });

  it('should support stderr as a destination', async () => {
    const child = spawnSync(process.execPath,
                            ['--test', '--test-reporter', 'dot', '--test-reporter-destination', 'stderr', testFile]);
    assert.match(child.stderr.toString(), /\.XX\.\n/);
    assert.match(child.stderr.toString(), /Failed tests:/);
    assert.match(child.stderr.toString(), /✖ failing/);
    assert.match(child.stderr.toString(), /✖ nested/);
    assert.strictEqual(child.stdout.toString(), '');
  });

  it('should support a file as a destination', async () => {
    const file = tmpdir.resolve(`${tmpFiles++}.out`);
    const child = spawnSync(process.execPath,
                            ['--test', '--test-reporter', 'dot', '--test-reporter-destination', file, testFile]);
    assert.strictEqual(child.stderr.toString(), '');
    assert.strictEqual(child.stdout.toString(), '');
    const fileContents = fs.readFileSync(file, 'utf8');
    assert.match(fileContents, /\.XX\.\n/);
    assert.match(fileContents, /Failed tests:/);
    assert.match(fileContents, /✖ failing/);
    assert.match(fileContents, /✖ nested/);
  });

  it('should disallow using v8-serializer as reporter', async () => {
    const child = spawnSync(process.execPath, ['--test', '--test-reporter', 'v8-serializer', testFile]);
    assert.strictEqual(child.stdout.toString(), '');
    assert(child.status > 0);
    assert.match(child.stderr.toString(), /ERR_MODULE_NOT_FOUND/);
  });

  it('should support multiple reporters', async () => {
    const file = tmpdir.resolve(`${tmpFiles++}.out`);
    const file2 = tmpdir.resolve(`${tmpFiles++}.out`);
    const child = spawnSync(process.execPath,
                            ['--test',
                             '--test-reporter', 'dot', '--test-reporter-destination', file,
                             '--test-reporter', 'spec', '--test-reporter-destination', file2,
                             '--test-reporter', 'tap', '--test-reporter-destination', 'stdout',
                             testFile]);
    assert.match(child.stdout.toString(), /TAP version 13/);
    assert.match(child.stdout.toString(), /# duration_ms/);
    const fileContents = fs.readFileSync(file, 'utf8');
    assert.match(fileContents, /\.XX\.\n/);
    assert.match(fileContents, /Failed tests:/);
    assert.match(fileContents, /✖ failing/);
    assert.match(fileContents, /✖ nested/);
    const file2Contents = fs.readFileSync(file2, 'utf8');
    assert.match(file2Contents, /▶ nested/);
    assert.match(file2Contents, /✔ ok/);
    assert.match(file2Contents, /✖ failing/);
  });

  ['js', 'cjs', 'mjs'].forEach((ext) => {
    it(`should support a '${ext}' file as a custom reporter`, async () => {
      const filename = `custom.${ext}`;
      const child = spawnSync(process.execPath,
                              ['--test', '--test-reporter', fixtures.fileURL('test-runner/custom_reporters/', filename),
                               testFile]);
      assert.strictEqual(child.stderr.toString(), '');
      const stdout = child.stdout.toString();
      assert.match(stdout, /{"test:enqueue":5,"test:dequeue":5,"test:complete":5,"test:start":4,"test:pass":2,"test:fail":2,"test:plan":2,"test:summary":2,"test:diagnostic":\d+}$/);
      assert.strictEqual(stdout.slice(0, filename.length + 2), `${filename} {`);
    });
  });

  it('should support a custom reporter from node_modules', async () => {
    const child = spawnSync(process.execPath,
                            ['--test', '--test-reporter', 'reporter-cjs', 'reporters.js'],
                            { cwd: fixtures.path('test-runner') });
    assert.strictEqual(child.stderr.toString(), '');
    assert.match(
      child.stdout.toString(),
      /^package: reporter-cjs{"test:enqueue":5,"test:dequeue":5,"test:complete":5,"test:start":4,"test:pass":2,"test:fail":2,"test:plan":2,"test:summary":2,"test:diagnostic":\d+}$/,
    );
  });

  it('should support a custom ESM reporter from node_modules', async () => {
    const child = spawnSync(process.execPath,
                            ['--test', '--test-reporter', 'reporter-esm', 'reporters.js'],
                            { cwd: fixtures.path('test-runner') });
    assert.strictEqual(child.stderr.toString(), '');
    assert.match(
      child.stdout.toString(),
      /^package: reporter-esm{"test:enqueue":5,"test:dequeue":5,"test:complete":5,"test:start":4,"test:pass":2,"test:fail":2,"test:plan":2,"test:summary":2,"test:diagnostic":\d+}$/,
    );
  });

  it('should throw when reporter setup throws asynchronously', async () => {
    const child = spawnSync(
      process.execPath,
      ['--test', '--test-reporter', fixtures.fileURL('empty.js'), 'reporters.js'],
      { cwd: fixtures.path('test-runner') }
    );
    assert.strictEqual(child.status, 7);
    assert.strictEqual(child.signal, null);
    assert.strictEqual(child.stdout.toString(), '');
    assert.match(child.stderr.toString(), /ERR_INVALID_ARG_TYPE/);
  });

  it('should throw when reporter errors', async () => {
    const child = spawnSync(process.execPath,
                            ['--test', '--test-reporter', fixtures.fileURL('test-runner/custom_reporters/throwing.js'),
                             fixtures.path('test-runner/default-behavior/index.test.js')]);
    assert.strictEqual(child.status, 7);
    assert.strictEqual(child.signal, null);
    assert.strictEqual(child.stdout.toString(), 'Going to throw an error\n');
    assert.match(child.stderr.toString(), /Error: Reporting error\r?\n\s+at customReporter/);
  });

  it('should throw when reporter errors asynchronously', async () => {
    const child = spawnSync(process.execPath,
                            ['--test', '--test-reporter',
                             fixtures.fileURL('test-runner/custom_reporters/throwing-async.js'),
                             fixtures.path('test-runner/default-behavior/index.test.js')]);
    assert.strictEqual(child.status, 7);
    assert.strictEqual(child.signal, null);
    assert.strictEqual(child.stdout.toString(), 'Going to throw an error\n');
    assert.match(child.stderr.toString(), /Emitted 'error' event on Duplex instance/);
  });

  it('should support stdout as a destination with spec reporter', async () => {
    process.env.FORCE_COLOR = '1';
    const file = tmpdir.resolve(`${tmpFiles++}.txt`);
    const child = spawnSync(process.execPath,
                            ['--test', '--test-reporter', 'spec', '--test-reporter-destination', file, testFile]);
    assert.strictEqual(child.stderr.toString(), '');
    assert.strictEqual(child.stdout.toString(), '');
    const fileConent = fs.readFileSync(file, 'utf8');
    assert.match(fileConent, /▶ nested/);
    assert.match(fileConent, /✔ ok/);
    assert.match(fileConent, /✖ failing/);
    assert.match(fileConent, /ℹ tests 4/);
    assert.match(fileConent, /ℹ pass 2/);
    assert.match(fileConent, /ℹ fail 2/);
    assert.match(fileConent, /ℹ cancelled 0/);
    assert.match(fileConent, /ℹ skipped 0/);
    assert.match(fileConent, /ℹ todo 0/);
  });
});
