'use strict';
const common = require('../common');
const assert = require('assert');
const cp = require('child_process');

// Verify that a shell is, in fact, executed
const doesNotExist = cp.spawn('does-not-exist', { shell: true });

assert.notStrictEqual(doesNotExist.spawnfile, 'does-not-exist');
doesNotExist.on('error', common.mustNotCall());
doesNotExist.on('exit', common.mustCall((code, signal) => {
  assert.strictEqual(signal, null);

  if (common.isWindows)
    assert.strictEqual(code, 1);    // Exit code of cmd.exe
  else
    assert.strictEqual(code, 127);  // Exit code of /bin/sh
}));

// Verify that passing arguments works
common.expectWarning(
  'DeprecationWarning',
  'Passing args to a child process with shell option true can lead to security ' +
  'vulnerabilities, as the arguments are not escaped, only concatenated.',
  'DEP0190');

const echo = cp.spawn('echo', ['foo'], {
  encoding: 'utf8',
  shell: true
});
let echoOutput = '';

assert.strictEqual(echo.spawnargs[echo.spawnargs.length - 1].replace(/"/g, ''),
                   'echo foo');
echo.stdout.on('data', (data) => {
  echoOutput += data;
});
echo.on('close', common.mustCall((code, signal) => {
  assert.strictEqual(echoOutput.trim(), 'foo');
}));

// Verify that shell features can be used
const cmd = 'echo bar | cat';
const command = cp.spawn(cmd, {
  encoding: 'utf8',
  shell: true
});
let commandOutput = '';

command.stdout.on('data', (data) => {
  commandOutput += data;
});
command.on('close', common.mustCall((code, signal) => {
  assert.strictEqual(commandOutput.trim(), 'bar');
}));

// Verify that the environment is properly inherited
const env = cp.spawn(`"${common.isWindows ? process.execPath : '$NODE'}" -pe process.env.BAZ`, {
  env: { ...process.env, BAZ: 'buzz', NODE: process.execPath },
  encoding: 'utf8',
  shell: true
});
let envOutput = '';

env.stdout.on('data', (data) => {
  envOutput += data;
});
env.on('close', common.mustCall((code, signal) => {
  assert.strictEqual(envOutput.trim(), 'buzz');
}));
