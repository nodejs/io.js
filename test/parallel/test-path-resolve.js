'use strict';
const common = require('../common');
const fixtures = require('../common/fixtures');
const assert = require('assert');
const child = require('child_process');
const path = require('path');

const failures = [];
const slashRE = /\//g;
const backslashRE = /\\/g;

const posixyCwd = common.isWindows ?
  (() => {
    const _ = process.cwd()
      .replaceAll(path.sep, path.posix.sep);
    return _.slice(_.indexOf(path.posix.sep));
  })() :
  process.cwd();

const resolveTests = [
  [ path.win32.resolve,
    // Arguments                               result
    [[['c:/blah\\blah', 'd:/games', 'c:../a'], 'c:\\blah\\a'],
     [['c:/ignore', 'd:\\a/b\\c/d', '\\e.exe'], 'd:\\e.exe'],
     [['c:/ignore', 'c:/some/file'], 'c:\\some\\file'],
     [['d:/ignore', 'd:some/dir//'], 'd:\\ignore\\some\\dir'],
     [['.'], process.cwd()],
     [['//server/share', '..', 'relative\\'], '\\\\server\\share\\relative'],
     [['c:/', '//'], 'c:\\'],
     [['c:/', '//dir'], 'c:\\dir'],
     [['c:/', '//server/share'], '\\\\server\\share\\'],
     [['c:/', '//server//share'], '\\\\server\\share\\'],
     [['c:/', '///some//dir'], 'c:\\some\\dir'],
     [['C:\\foo\\tmp.3\\', '..\\tmp.3\\cycles\\root.js'],
      'C:\\foo\\tmp.3\\cycles\\root.js'],
     [['\\\\.\\PHYSICALDRIVE0'], '\\\\.\\PHYSICALDRIVE0'],
     [['\\\\?\\PHYSICALDRIVE0'], '\\\\?\\PHYSICALDRIVE0'],
    ],
  ],
  [ path.posix.resolve,
    // Arguments                    result
    [[['/var/lib', '../', 'file/'], '/var/file'],
     [['/var/lib', '/../', 'file/'], '/file'],
     [['a/b/c/', '../../..'], posixyCwd],
     [['.'], posixyCwd],
     [['/some/dir', '.', '/absolute/'], '/absolute'],
     [['/foo/tmp.3/', '../tmp.3/cycles/root.js'], '/foo/tmp.3/cycles/root.js'],
    ],
  ],
];
resolveTests.forEach(([resolve, tests]) => {
  tests.forEach(([test, expected]) => {
    const actual = resolve.apply(null, test);
    let actualAlt;
    const os = resolve === path.win32.resolve ? 'win32' : 'posix';
    if (resolve === path.win32.resolve && !common.isWindows)
      actualAlt = actual.replace(backslashRE, '/');
    else if (resolve !== path.win32.resolve && common.isWindows)
      actualAlt = actual.replace(slashRE, '\\');

    const message =
      `path.${os}.resolve(${test.map(JSON.stringify).join(',')})\n  expect=${
        JSON.stringify(expected)}\n  actual=${JSON.stringify(actual)}`;
    if (actual !== expected && actualAlt !== expected)
      failures.push(message);
  });
});
assert.strictEqual(failures.length, 0, failures.join('\n'));

if (common.isWindows) {
  // Test resolving the current Windows drive letter from a spawned process.
  // See https://github.com/nodejs/node/issues/7215
  const currentDriveLetter = path.parse(process.cwd()).root.substring(0, 2);
  const resolveFixture = fixtures.path('path-resolve.js');
  const spawnResult = child.spawnSync(
    process.argv[0], [resolveFixture, currentDriveLetter]);
  const resolvedPath = spawnResult.stdout.toString().trim();
  assert.strictEqual(resolvedPath.toLowerCase(), process.cwd().toLowerCase());
}

if (!common.isWindows) {
  // Test handling relative paths to be safe when process.cwd() fails.
  process.cwd = () => '';
  assert.strictEqual(process.cwd(), '');
  const resolved = path.resolve();
  const expected = '.';
  assert.strictEqual(resolved, expected);
}
