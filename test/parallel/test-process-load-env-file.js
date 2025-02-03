'use strict';

const common = require('../common');
const fixtures = require('../../test/common/fixtures');
const assert = require('node:assert');
const { describe, it } = require('node:test');
const { join } = require('node:path');
const { isMainThread } = require('worker_threads');

const basicValidEnvFilePath = fixtures.path('dotenv/basic-valid.env');
const validEnvFilePath = fixtures.path('dotenv/valid.env');
const missingEnvFile = fixtures.path('dir%20with unusual"chars \'åß∂ƒ©∆¬…`/non-existent-file.env');

describe('process.loadEnvFile()', () => {

  it('supports passing path', async () => {
    const code = `
      process.loadEnvFile(${JSON.stringify(validEnvFilePath)});
      const assert = require('assert');
      assert.strictEqual(process.env.BASIC, 'basic');
    `.trim();
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--eval', code ],
      { cwd: __dirname },
    );
    assert.strictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
  });

  it('supports not-passing a path', async () => {
    // Uses `../fixtures/dotenv/.env` file.
    const code = `
      process.loadEnvFile();
      const assert = require('assert');
      assert.strictEqual(process.env.BASIC, 'basic');
    `.trim();
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--eval', code ],
      { cwd: fixtures.path('dotenv/') },
    );
    assert.strictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
  });

  it('should throw when file does not exist', async () => {
    assert.throws(() => {
      process.loadEnvFile(missingEnvFile);
    }, { code: 'ENOENT', syscall: 'open', path: missingEnvFile });
  });

  // The whole chdir flow here is to address a case where a developer
  // has a .env in the worktree which is probably in the global .gitignore.
  // In that case this test would fail. To avoid confusion, chdir to lib will
  // make this way less likely to happen. Probably a temporary directory would
  // be the best choice but given how edge this case is this is fine.
  it('should throw when `.env` does not exist', async () => {
    const originalCwd = process.cwd();

    try {
      if (isMainThread) {
        process.chdir(join(originalCwd, 'lib'));
      }

      assert.throws(() => {
        process.loadEnvFile();
      }, { code: 'ENOENT', syscall: 'open', path: '.env' });
    } finally {
      if (isMainThread) {
        process.chdir(originalCwd);
      }
    }
  });

  it('should check for permissions', async () => {
    const code = `
      process.loadEnvFile(${JSON.stringify(missingEnvFile)});
    `.trim();
    const child = await common.spawnPromisified(
      process.execPath,
      [ '--eval', code, '--permission' ],
      { cwd: __dirname },
    );
    assert.match(child.stderr, /Error: Access to this API has been restricted/);
    assert.match(child.stderr, /code: 'ERR_ACCESS_DENIED'/);
    assert.match(child.stderr, /permission: 'FileSystemRead'/);
    if (!common.isWindows) {
      const resource = /^\s+resource: (['"])(.+)\1$/m.exec(child.stderr);
      assert(resource);
      assert.strictEqual(resource[2], resource[1] === "'" ?
        missingEnvFile.replaceAll("'", "\\'") :
        JSON.stringify(missingEnvFile).slice(1, -1));
    }
    assert.strictEqual(child.code, 1);
  });

  it('loadEnvFile does not mutate --env-file output', async () => {
    const code = `
      process.loadEnvFile(${JSON.stringify(basicValidEnvFilePath)});
      require('assert')(process.env.BASIC === 'basic');
    `.trim();
    const child = await common.spawnPromisified(
      process.execPath,
      [ `--env-file=${validEnvFilePath}`, '--eval', code ],
      { cwd: __dirname },
    );
    assert.strictEqual(child.stdout, '');
    assert.strictEqual(child.stderr, '');
    assert.strictEqual(child.code, 0);
  });
});
