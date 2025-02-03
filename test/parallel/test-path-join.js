'use strict';
require('../common');
const assert = require('assert');
const path = require('path');

const failures = [];
const backslashRE = /\\/g;

const joinTests = [
  [ [path.posix.join, path.win32.join],
    // Arguments                     result
    [[['.', 'x/b', '..', '/b/c.js'], 'x/b/c.js'],
     [[], '.'],
     [['/.', 'x/b', '..', '/b/c.js'], '/x/b/c.js'],
     [['/foo', '../../../bar'], '/bar'],
     [['foo', '../../../bar'], '../../bar'],
     [['foo/', '../../../bar'], '../../bar'],
     [['foo/x', '../../../bar'], '../bar'],
     [['foo/x', './bar'], 'foo/x/bar'],
     [['foo/x/', './bar'], 'foo/x/bar'],
     [['foo/x/', '.', 'bar'], 'foo/x/bar'],
     [['./'], './'],
     [['.', './'], './'],
     [['.', '.', '.'], '.'],
     [['.', './', '.'], '.'],
     [['.', '/./', '.'], '.'],
     [['.', '/////./', '.'], '.'],
     [['.'], '.'],
     [['', '.'], '.'],
     [['', 'foo'], 'foo'],
     [['foo', '/bar'], 'foo/bar'],
     [['', '/foo'], '/foo'],
     [['', '', '/foo'], '/foo'],
     [['', '', 'foo'], 'foo'],
     [['foo', ''], 'foo'],
     [['foo/', ''], 'foo/'],
     [['foo', '', '/bar'], 'foo/bar'],
     [['./', '..', '/foo'], '../foo'],
     [['./', '..', '..', '/foo'], '../../foo'],
     [['.', '..', '..', '/foo'], '../../foo'],
     [['', '..', '..', '/foo'], '../../foo'],
     [['/'], '/'],
     [['/', '.'], '/'],
     [['/', '..'], '/'],
     [['/', '..', '..'], '/'],
     [[''], '.'],
     [['', ''], '.'],
     [[' /foo'], ' /foo'],
     [[' ', 'foo'], ' /foo'],
     [[' ', '.'], ' '],
     [[' ', '/'], ' /'],
     [[' ', ''], ' '],
     [['/', 'foo'], '/foo'],
     [['/', '/foo'], '/foo'],
     [['/', '//foo'], '/foo'],
     [['/', '', '/foo'], '/foo'],
     [['', '/', 'foo'], '/foo'],
     [['', '/', '/foo'], '/foo'],
    ],
  ],
];

// Windows-specific join tests
joinTests.push([
  path.win32.join,
  joinTests[0][1].slice(0).concat(
    [// Arguments                     result
      // UNC path expected
      [['//foo/bar'], '\\\\foo\\bar\\'],
      [['\\/foo/bar'], '\\\\foo\\bar\\'],
      [['\\\\foo/bar'], '\\\\foo\\bar\\'],
      // UNC path expected - server and share separate
      [['//foo', 'bar'], '\\\\foo\\bar\\'],
      [['//foo/', 'bar'], '\\\\foo\\bar\\'],
      [['//foo', '/bar'], '\\\\foo\\bar\\'],
      // UNC path expected - questionable
      [['//foo', '', 'bar'], '\\\\foo\\bar\\'],
      [['//foo/', '', 'bar'], '\\\\foo\\bar\\'],
      [['//foo/', '', '/bar'], '\\\\foo\\bar\\'],
      // UNC path expected - even more questionable
      [['', '//foo', 'bar'], '\\\\foo\\bar\\'],
      [['', '//foo/', 'bar'], '\\\\foo\\bar\\'],
      [['', '//foo/', '/bar'], '\\\\foo\\bar\\'],
      // No UNC path expected (no double slash in first component)
      [['\\', 'foo/bar'], '\\foo\\bar'],
      [['\\', '/foo/bar'], '\\foo\\bar'],
      [['', '/', '/foo/bar'], '\\foo\\bar'],
      // No UNC path expected (no non-slashes in first component -
      // questionable)
      [['//', 'foo/bar'], '\\foo\\bar'],
      [['//', '/foo/bar'], '\\foo\\bar'],
      [['\\\\', '/', '/foo/bar'], '\\foo\\bar'],
      [['//'], '\\'],
      // No UNC path expected (share name missing - questionable).
      [['//foo'], '\\foo'],
      [['//foo/'], '\\foo\\'],
      [['//foo', '/'], '\\foo\\'],
      [['//foo', '', '/'], '\\foo\\'],
      // No UNC path expected (too many leading slashes - questionable)
      [['///foo/bar'], '\\foo\\bar'],
      [['////foo', 'bar'], '\\foo\\bar'],
      [['\\\\\\/foo/bar'], '\\foo\\bar'],
      // Drive-relative vs drive-absolute paths. This merely describes the
      // status quo, rather than being obviously right
      [['c:'], 'c:.'],
      [['c:.'], 'c:.'],
      [['c:', ''], 'c:.'],
      [['', 'c:'], 'c:.'],
      [['c:.', '/'], 'c:.\\'],
      [['c:.', 'file'], 'c:file'],
      [['c:', '/'], 'c:\\'],
      [['c:', 'file'], 'c:\\file'],
      // Path traversal in previous versions of Node.js.
      [['./upload', '/../C:/Windows'], '.\\C:\\Windows'],
      [['upload', '../', 'C:foo'], '.\\C:foo'],
      [['test/..', '??/D:/Test'], '.\\??\\D:\\Test'],
      [['test', '..', 'D:'], '.\\D:'],
      [['test', '..', 'D:\\'], '.\\D:\\'],
      [['test', '..', 'D:foo'], '.\\D:foo'],
    ]
  ),
]);
joinTests.forEach((test) => {
  if (!Array.isArray(test[0]))
    test[0] = [test[0]];
  test[0].forEach((join) => {
    test[1].forEach((test) => {
      const actual = join.apply(null, test[0]);
      const expected = test[1];
      // For non-Windows specific tests with the Windows join(), we need to try
      // replacing the slashes since the non-Windows specific tests' `expected`
      // use forward slashes
      let actualAlt;
      let os;
      if (join === path.win32.join) {
        actualAlt = actual.replace(backslashRE, '/');
        os = 'win32';
      } else {
        os = 'posix';
      }
      if (actual !== expected && actualAlt !== expected) {
        const delimiter = test[0].map(JSON.stringify).join(',');
        const message = `path.${os}.join(${delimiter})\n  expect=${
          JSON.stringify(expected)}\n  actual=${JSON.stringify(actual)}`;
        failures.push(`\n${message}`);
      }
    });
  });
});
assert.strictEqual(failures.length, 0, failures.join(''));
