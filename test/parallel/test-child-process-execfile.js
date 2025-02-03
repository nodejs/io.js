'use strict';

const common = require('../common');
const assert = require('assert');
const { execFile, execFileSync } = require('child_process');
const { getEventListeners } = require('events');
const { getSystemErrorName } = require('util');
const fixtures = require('../common/fixtures');
const os = require('os');

const fixture = fixtures.path('exit.js');
const echoFixture = fixtures.path('echo.js');
const execOpts = { encoding: 'utf8', shell: true, env: { ...process.env, NODE: process.execPath, FIXTURE: fixture } };

{
  execFile(
    process.execPath,
    [fixture, 42],
    common.mustCall((e) => {
      // Check that arguments are included in message
      assert.strictEqual(e.message.trim(),
                         `Command failed: ${process.execPath} ${fixture} 42`);
      assert.strictEqual(e.code, 42);
    })
  );
}

{
  // Verify that negative exit codes can be translated to UV error names.
  const errorString = `Error: Command failed: ${process.execPath}`;
  const code = -1;
  const callback = common.mustCall((err, stdout, stderr) => {
    assert.strictEqual(err.toString().trim(), errorString);
    assert.strictEqual(err.code, getSystemErrorName(code));
    assert.strictEqual(err.killed, true);
    assert.strictEqual(err.signal, null);
    assert.strictEqual(err.cmd, process.execPath);
    assert.strictEqual(stdout.trim(), '');
    assert.strictEqual(stderr.trim(), '');
  });
  const child = execFile(process.execPath, callback);

  child.kill();
  child.emit('close', code, null);
}

{
  // Verify the shell option works properly
  execFile(
    `"${common.isWindows ? execOpts.env.NODE : '$NODE'}"`,
    [`"${common.isWindows ? execOpts.env.FIXTURE : '$FIXTURE'}"`, 0],
    execOpts,
    common.mustSucceed(),
  );
}

{
  // Verify that the signal option works properly
  const ac = new AbortController();
  const { signal } = ac;

  const test = () => {
    const check = common.mustCall((err) => {
      assert.strictEqual(err.code, 'ABORT_ERR');
      assert.strictEqual(err.name, 'AbortError');
      assert.strictEqual(err.signal, undefined);
    });
    execFile(process.execPath, [echoFixture, 0], { signal }, check);
  };

  // Verify that it still works the same way now that the signal is aborted.
  test();
  ac.abort();
}

{
  // Verify that does not spawn a child if already aborted
  const signal = AbortSignal.abort();

  const check = common.mustCall((err) => {
    assert.strictEqual(err.code, 'ABORT_ERR');
    assert.strictEqual(err.name, 'AbortError');
    assert.strictEqual(err.signal, undefined);
  });
  execFile(process.execPath, [echoFixture, 0], { signal }, check);
}

{
  // Verify that if something different than Abortcontroller.signal
  // is passed, ERR_INVALID_ARG_TYPE is thrown
  assert.throws(() => {
    const callback = common.mustNotCall();

    execFile(process.execPath, [echoFixture, 0], { signal: 'hello' }, callback);
  }, { code: 'ERR_INVALID_ARG_TYPE', name: 'TypeError' });
}
{
  // Verify that the process completing removes the abort listener
  const ac = new AbortController();
  const { signal } = ac;

  const callback = common.mustCall((err) => {
    assert.strictEqual(getEventListeners(ac.signal).length, 0);
    assert.strictEqual(err, null);
  });
  execFile(process.execPath, [fixture, 0], { signal }, callback);
}

// Verify the execFile() stdout is the same as execFileSync().
{
  const file = 'echo';
  const args = ['foo', 'bar'];

  // Test with and without `{ shell: true }`
  [
    // Skipping shell-less test on Windows because its echo command is a shell built-in command.
    ...(common.isWindows ? [] : [{ encoding: 'utf8' }]),
    { shell: true, encoding: 'utf8' },
  ].forEach((options) => {
    const execFileSyncStdout = execFileSync(file, args, options);
    assert.strictEqual(execFileSyncStdout, `foo bar${os.EOL}`);

    execFile(file, args, options, common.mustCall((_, stdout) => {
      assert.strictEqual(stdout, execFileSyncStdout);
    }));
  });
}
