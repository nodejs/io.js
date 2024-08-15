'use strict';

const common = require('../common');
const { it, describe } = require('node:test');
const assert = require('node:assert');

const fixtures = require('../common/fixtures');
const envSuffix = common.isWindows ? '-windows' : '';

describe('node --run [command]', () => {
  it('should emit experimental warning', async () => {
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--run', 'test'],
      { cwd: __dirname },
    );
    assert.match(child.stderr, /ExperimentalWarning: Task runner is an experimental feature and might change at any time/);
    assert.match(child.stderr, /Can't read package\.json/);
    assert.strictEqual(child.stdout, '');
    assert.strictEqual(child.code, 1);
  });

  it('returns error on non-existent file', async () => {
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--no-warnings', '--run', 'test'],
      { cwd: __dirname },
    );
    assert.match(child.stderr, /Can't read package\.json/);
    assert.strictEqual(child.stdout, '');
    assert.strictEqual(child.code, 1);
  });

  it('runs a valid command', async () => {
    // Run a script that just log `no test specified`
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--run', 'test', '--no-warnings'],
      { cwd: fixtures.path('run-script') },
    );
    assert.match(child.stdout, /Error: no test specified/);
    assert.strictEqual(child.code, 1);
  });

  it('adds node_modules/.bin to path', async () => {
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--no-warnings', '--run', `ada${envSuffix}`],
      { cwd: fixtures.path('run-script') },
    );
    assert.match(child.stdout, /06062023/);
    assert.strictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
  });

  it('appends positional arguments', async () => {
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--no-warnings', '--run', `positional-args${envSuffix}`, '--', '--help "hello world test"', 'A', 'B', 'C'],
      { cwd: fixtures.path('run-script') },
    );
    if (common.isWindows) {
      assert.match(child.stdout, /Arguments: '--help ""hello world test"" A B C'/);
    } else {
      assert.match(child.stdout, /Arguments: '--help "hello world test" A B C'/);
    }
    assert.match(child.stdout, /The total number of arguments are: 4/);
    assert.strictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
  });

  it('should set PATH environment variable with paths appended with node_modules/.bin', async () => {
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--no-warnings', '--run', `path-env${envSuffix}`],
      { cwd: fixtures.path('run-script/sub-directory') },
    );
    assert.ok(child.stdout.includes(fixtures.path('run-script/node_modules/.bin')));

    // The following test ensures that we do not add paths that does not contain
    // "node_modules/.bin"
    assert.ok(!child.stdout.includes(fixtures.path('node_modules/.bin')));

    // The following test ensures that we add paths that contains "node_modules/.bin"
    assert.ok(child.stdout.includes(fixtures.path('run-script/sub-directory/node_modules/.bin')));

    assert.strictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
  });

  it('should set special environment variables', async () => {
    const scriptName = `special-env-variables${envSuffix}`;
    const packageJsonPath = fixtures.path('run-script/package.json');
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--no-warnings', '--run', scriptName],
      { cwd: fixtures.path('run-script') },
    );
    assert.ok(child.stdout.includes(scriptName));
    assert.ok(child.stdout.includes(packageJsonPath));
    assert.strictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
  });

  it('will search parent directories for a package.json file', async () => {
    const packageJsonPath = fixtures.path('run-script/package.json');
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--no-warnings', '--run', `special-env-variables${envSuffix}`],
      { cwd: fixtures.path('run-script/sub-directory') },
    );
    assert.ok(child.stdout.includes(packageJsonPath));
    assert.strictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
  });

  it('returns error on unparsable file', async () => {
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--no-warnings', '--run', 'test'],
      { cwd: fixtures.path('run-script/cannot-parse') },
    );
    assert.match(child.stderr, /Can't parse package\.json/);
    assert.strictEqual(child.stdout, '');
    assert.strictEqual(child.code, 1);
  });

  it('returns error when there is no "scripts" field file', async () => {
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--no-warnings', '--run', 'test'],
      { cwd: fixtures.path('run-script/cannot-find-script') },
    );
    assert.match(child.stderr, /Can't find "scripts" field in package\.json/);
    assert.strictEqual(child.stdout, '');
    assert.strictEqual(child.code, 1);
  });
});
