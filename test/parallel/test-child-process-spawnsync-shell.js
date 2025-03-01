// Flags: --expose-internals
'use strict';
const common = require('../common');
const assert = require('assert');
const cp = require('child_process');
const internalCp = require('internal/child_process');
const oldSpawnSync = internalCp.spawnSync;

// Verify that a shell is, in fact, executed
const doesNotExist = cp.spawnSync('does-not-exist', { shell: true });

assert.notStrictEqual(doesNotExist.file, 'does-not-exist');
assert.strictEqual(doesNotExist.error, undefined);
assert.strictEqual(doesNotExist.signal, null);

if (common.isWindows)
  assert.strictEqual(doesNotExist.status, 1);    // Exit code of cmd.exe
else
  assert.strictEqual(doesNotExist.status, 127);  // Exit code of /bin/sh

// Verify that passing arguments works
common.expectWarning(
  'DeprecationWarning',
  'Passing args to a child process with shell option true can lead to security ' +
  'vulnerabilities, as the arguments are not escaped, only concatenated.',
  'DEP0190');

internalCp.spawnSync = common.mustCall(function(opts) {
  assert.strictEqual(opts.args[opts.args.length - 1].replace(/"/g, ''),
                     'echo foo');
  return oldSpawnSync(opts);
});
const echo = cp.spawnSync('echo', ['foo'], { shell: true });
internalCp.spawnSync = oldSpawnSync;

assert.strictEqual(echo.stdout.toString().trim(), 'foo');

// Verify that shell features can be used
const cmd = 'echo bar | cat';
const command = cp.spawnSync(cmd, { shell: true });

assert.strictEqual(command.stdout.toString().trim(), 'bar');

// Verify that the environment is properly inherited
const env = cp.spawnSync(`"${common.isWindows ? process.execPath : '$NODE'}" -pe process.env.BAZ`, {
  env: { ...process.env, BAZ: 'buzz', NODE: process.execPath },
  shell: true
});

assert.strictEqual(env.stdout.toString().trim(), 'buzz');

// Verify that the shell internals work properly across platforms.
{
  const originalComspec = process.env.comspec;

  // Enable monkey patching process.platform.
  const originalPlatform = process.platform;
  let platform = null;
  Object.defineProperty(process, 'platform', { get: () => platform });

  function test(testPlatform, shell, shellOutput) {
    platform = testPlatform;
    const isCmd = /^(?:.*\\)?cmd(?:\.exe)?$/i.test(shellOutput);
    const cmd = 'not_a_real_command';

    const shellFlags = isCmd ? ['/d', '/s', '/c'] : ['-c'];
    const outputCmd = isCmd ? `"${cmd}"` : cmd;
    const windowsVerbatim = isCmd ? true : undefined;
    internalCp.spawnSync = common.mustCall(function(opts) {
      assert.strictEqual(opts.file, shellOutput);
      assert.deepStrictEqual(opts.args,
                             [shellOutput, ...shellFlags, outputCmd]);
      assert.strictEqual(opts.shell, shell);
      assert.strictEqual(opts.file, opts.file);
      assert.deepStrictEqual(opts.args, opts.args);
      assert.strictEqual(opts.windowsHide, false);
      assert.strictEqual(opts.windowsVerbatimArguments,
                         !!windowsVerbatim);
    });
    cp.spawnSync(cmd, { shell });
    internalCp.spawnSync = oldSpawnSync;
  }

  // Test Unix platforms with the default shell.
  test('darwin', true, '/bin/sh');

  // Test Unix platforms with a user specified shell.
  test('darwin', '/bin/csh', '/bin/csh');

  // Test Android platforms.
  test('android', true, '/system/bin/sh');

  // Test Windows platforms with a user specified shell.
  test('win32', 'powershell.exe', 'powershell.exe');

  // Test Windows platforms with the default shell and no comspec.
  delete process.env.comspec;
  test('win32', true, 'cmd.exe');

  // Test Windows platforms with the default shell and a comspec value.
  process.env.comspec = 'powershell.exe';
  test('win32', true, process.env.comspec);

  // Restore the original value of process.platform.
  platform = originalPlatform;

  // Restore the original comspec environment variable if necessary.
  if (originalComspec)
    process.env.comspec = originalComspec;
}
