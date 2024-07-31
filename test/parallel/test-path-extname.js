'use strict';
require('../common');
const assert = require('assert');
const path = require('path');

const failures = [];
const slashRE = /\//g;

const testPaths = [
  [__filename, '.js'],
  ['', ''],
  ['/path/to/file', ''],
  ['/path/to/file.ext', '.ext'],
  ['/path.to/file.ext', '.ext'],
  ['/path.to/file', ''],
  ['/path.to/.file', ''],
  ['/path.to/.file.ext', '.ext'],
  ['/path/to/f.ext', '.ext'],
  ['/path/to/..ext', '.ext'],
  ['/path/to/..', ''],
  ['file', ''],
  ['file.ext', '.ext'],
  ['.file', ''],
  ['.file.ext', '.ext'],
  ['/file', ''],
  ['/file.ext', '.ext'],
  ['/.file', ''],
  ['/.file.ext', '.ext'],
  ['.path/file.ext', '.ext'],
  ['file.ext.ext', '.ext'],
  ['file.', '.'],
  ['.', ''],
  ['./', ''],
  ['.file.ext', '.ext'],
  ['.file', ''],
  ['.file.', '.'],
  ['.file..', '.'],
  ['..', ''],
  ['../', ''],
  ['..file.ext', '.ext'],
  ['..file', '.file'],
  ['..file.', '.'],
  ['..file..', '.'],
  ['...', '.'],
  ['...ext', '.ext'],
  ['....', '.'],
  ['file.ext/', '.ext'],
  ['file.ext//', '.ext'],
  ['file/', ''],
  ['file//', ''],
  ['file./', '.'],
  ['file.//', '.'],
];

for (const testPath of testPaths) {
  const expected = testPath[1];
  const extNames = [path.posix.extname, path.win32.extname];
  for (const extname of extNames) {
    let input = testPath[0];
    let os;
    if (extname === path.win32.extname) {
      input = input.replace(slashRE, '\\');
      os = 'win32';
    } else {
      os = 'posix';
    }
    const actual = extname(input);
    const message = `path.${os}.extname(${JSON.stringify(input)})\n  expect=${
      JSON.stringify(expected)}\n  actual=${JSON.stringify(actual)}`;
    if (actual !== expected)
      failures.push(`\n${message}`);
  }
  const input = `C:${testPath[0].replace(slashRE, '\\')}`;
  const actual = path.win32.extname(input);
  const message = `path.win32.extname(${JSON.stringify(input)})\n  expect=${
    JSON.stringify(expected)}\n  actual=${JSON.stringify(actual)}`;
  if (actual !== expected)
    failures.push(`\n${message}`);
}
assert.strictEqual(failures.length, 0, failures.join(''));

// On Windows, backslash is a path separator.
assert.strictEqual(path.win32.extname('.\\'), '');
assert.strictEqual(path.win32.extname('..\\'), '');
assert.strictEqual(path.win32.extname('file.ext\\'), '.ext');
assert.strictEqual(path.win32.extname('file.ext\\\\'), '.ext');
assert.strictEqual(path.win32.extname('file\\'), '');
assert.strictEqual(path.win32.extname('file\\\\'), '');
assert.strictEqual(path.win32.extname('file.\\'), '.');
assert.strictEqual(path.win32.extname('file.\\\\'), '.');

// On *nix, backslash is a valid name component like any other character.
assert.strictEqual(path.posix.extname('.\\'), '');
assert.strictEqual(path.posix.extname('..\\'), '.\\');
assert.strictEqual(path.posix.extname('file.ext\\'), '.ext\\');
assert.strictEqual(path.posix.extname('file.ext\\\\'), '.ext\\\\');
assert.strictEqual(path.posix.extname('file\\'), '');
assert.strictEqual(path.posix.extname('file\\\\'), '');
assert.strictEqual(path.posix.extname('file.\\'), '.\\');
assert.strictEqual(path.posix.extname('file.\\\\'), '.\\\\');
