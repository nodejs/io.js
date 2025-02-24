'use strict';
const common = require('../common');
const assert = require('assert');
const cp = require('child_process');
const fs = require('fs');
const tmpdir = require('../common/tmpdir');

// This test is only relevant on Windows.
if (!common.isWindows)
  common.skip('Windows specific test.');

// This test ensures that child_process.exec can work with any shells.

tmpdir.refresh();
const tmpPath = `${tmpdir.path}\\path with spaces`;
fs.mkdirSync(tmpPath);

const test = (shell) => {
  cp.exec('echo foo bar', { shell: shell },
          common.mustSucceed((stdout, stderror) => {
            assert.ok(!stderror);
            assert.ok(stdout.includes('foo') && stdout.includes('bar'));
          }));
};
const testCopy = (shellName, shellPath) => {
  // Symlink the executable to a path with spaces, to ensure there are no issues
  // related to quoting of argv0
  const copyPath = `${tmpPath}\\${shellName}`;
  fs.symlinkSync(shellPath, copyPath);
  test(copyPath);
};

const system32 = `${process.env.SystemRoot}\\System32`;

// Test CMD
test();
test('cmd');
testCopy('cmd.exe', `${system32}\\cmd.exe`);
test('cmd.exe');
test('CMD');

// Test PowerShell
test('powershell');
testCopy('powershell.exe',
         `${system32}\\WindowsPowerShell\\v1.0\\powershell.exe`);
fs.writeFile(`${tmpPath}\\test file`, 'Test', common.mustSucceed(() => {
  cp.exec(`Get-ChildItem "${tmpPath}" | Select-Object -Property Name`,
          { shell: 'PowerShell' },
          common.mustSucceed((stdout, stderror) => {
            assert.ok(!stderror);
            assert.ok(stdout.includes(
              'test file'));
          }));
}));

// Test Bash (from WSL and Git), if available
cp.exec('where bash', common.mustCall((error, stdout) => {
  if (error) {
    return;
  }
  const lines = stdout.trim().split(/[\r\n]+/g);
  for (let i = 0; i < lines.length; ++i) {
    const bashPath = lines[i].trim();
    test(bashPath);
    testCopy(`bash_${i}.exe`, bashPath);
  }
}));
