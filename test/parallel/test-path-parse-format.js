// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';
const common = require('../common');
const assert = require('assert');
const path = require('path');

const winPaths = [
  // [path, root]
  ['C:\\path\\dir\\index.html', 'C:\\'],
  ['C:\\another_path\\DIR\\1\\2\\33\\\\index', 'C:\\'],
  ['another_path\\DIR with spaces\\1\\2\\33\\index', ''],
  ['\\', '\\'],
  ['\\foo\\C:', '\\'],
  ['file', ''],
  ['file:stream', ''],
  ['.\\file', ''],
  ['C:', 'C:'],
  ['C:.', 'C:'],
  ['C:..', 'C:'],
  ['C:abc', 'C:'],
  ['C:\\', 'C:\\'],
  ['C:\\abc', 'C:\\' ],
  ['', ''],

  // unc
  ['\\\\server\\share\\file_path', '\\\\server\\share\\'],
  ['\\\\server two\\shared folder\\file path.zip',
   '\\\\server two\\shared folder\\'],
  ['\\\\teela\\admin$\\system32', '\\\\teela\\admin$\\'],
  ['\\\\?\\UNC\\server\\share', '\\\\?\\UNC\\'],
];

const winSpecialCaseParseTests = [
  ['t', { base: 't', name: 't', root: '', dir: '', ext: '' }],
  ['/foo/bar', { root: '/', dir: '/foo', base: 'bar', ext: '', name: 'bar' }],
];

const winSpecialCaseFormatTests = [
  [{ dir: 'some\\dir' }, 'some\\dir\\'],
  [{ base: 'index.html' }, 'index.html'],
  [{ root: 'C:\\' }, 'C:\\'],
  [{ name: 'index', ext: '.html' }, 'index.html'],
  [{ dir: 'some\\dir', name: 'index', ext: '.html' }, 'some\\dir\\index.html'],
  [{ root: 'C:\\', name: 'index', ext: '.html' }, 'C:\\index.html'],
  [{}, ''],
];

const unixPaths = [
  // [path, root]
  ['/home/user/dir/file.txt', '/'],
  ['/home/user/a dir/another File.zip', '/'],
  ['/home/user/a dir//another&File.', '/'],
  ['/home/user/a$$$dir//another File.zip', '/'],
  ['user/dir/another File.zip', ''],
  ['file', ''],
  ['.\\file', ''],
  ['./file', ''],
  ['C:\\foo', ''],
  ['/', '/'],
  ['', ''],
  ['.', ''],
  ['..', ''],
  ['/foo', '/'],
  ['/foo.', '/'],
  ['/foo.bar', '/'],
  ['/.', '/'],
  ['/.foo', '/'],
  ['/.foo.bar', '/'],
  ['/foo/bar.baz', '/'],
];

const unixSpecialCaseFormatTests = [
  [{ dir: 'some/dir' }, 'some/dir/'],
  [{ base: 'index.html' }, 'index.html'],
  [{ root: '/' }, '/'],
  [{ name: 'index', ext: '.html' }, 'index.html'],
  [{ dir: 'some/dir', name: 'index', ext: '.html' }, 'some/dir/index.html'],
  [{ root: '/', name: 'index', ext: '.html' }, '/index.html'],
  [{}, ''],
];

const errors = [
  { method: 'parse', input: [null] },
  { method: 'parse', input: [{}] },
  { method: 'parse', input: [true] },
  { method: 'parse', input: [1] },
  { method: 'parse', input: [] },
  { method: 'format', input: [null] },
  { method: 'format', input: [''] },
  { method: 'format', input: [true] },
  { method: 'format', input: [1] },
];

checkParseFormat(path.win32, winPaths);
checkParseFormat(path.posix, unixPaths);
checkSpecialCaseParseFormat(path.win32, winSpecialCaseParseTests);
checkErrors(path.win32);
checkErrors(path.posix);
checkFormat(path.win32, winSpecialCaseFormatTests);
checkFormat(path.posix, unixSpecialCaseFormatTests);

// Test removal of trailing path separators
const trailingTests = [
  [ path.win32.parse,
    [['.\\', { root: '', dir: '', base: '.', ext: '', name: '.' }],
     ['\\\\', { root: '\\', dir: '\\', base: '', ext: '', name: '' }],
     ['\\\\', { root: '\\', dir: '\\', base: '', ext: '', name: '' }],
     ['c:\\foo\\\\\\',
      { root: 'c:\\', dir: 'c:\\', base: 'foo', ext: '', name: 'foo' }],
     ['D:\\foo\\\\\\bar.baz',
      { root: 'D:\\',
        dir: 'D:\\foo\\\\',
        base: 'bar.baz',
        ext: '.baz',
        name: 'bar' },
     ],
    ],
  ],
  [ path.posix.parse,
    [['./', { root: '', dir: '', base: '.', ext: '', name: '.' }],
     ['//', { root: '/', dir: '/', base: '', ext: '', name: '' }],
     ['///', { root: '/', dir: '/', base: '', ext: '', name: '' }],
     ['/foo///', { root: '/', dir: '/', base: 'foo', ext: '', name: 'foo' }],
     ['/foo///bar.baz',
      { root: '/', dir: '/foo//', base: 'bar.baz', ext: '.baz', name: 'bar' },
     ],
    ],
  ],
];
const failures = [];
for (const [parse, testList] of trailingTests) {
  const os = parse === path.win32.parse ? 'win32' : 'posix';
  for (const [input, expected] of testList) {
    const actual = parse(input);
    const message = `path.${os}.parse(${JSON.stringify(input)})\n  expect=${
      JSON.stringify(expected)}\n  actual=${JSON.stringify(actual)}`;
    const actualKeys = Object.keys(actual);
    const expectedKeys = Object.keys(expected);
    let failed = (actualKeys.length !== expectedKeys.length);
    if (!failed) {
      for (let i = 0; i < actualKeys.length; ++i) {
        const key = actualKeys[i];
        if (!expectedKeys.includes(key) || actual[key] !== expected[key]) {
          failed = true;
          break;
        }
      }
    }
    if (failed)
      failures.push(`\n${message}`);
  }
}
assert.strictEqual(failures.length, 0, failures.join(''));

function checkErrors(path) {
  errors.forEach(({ method, input }) => {
    assert.throws(() => {
      path[method].apply(path, input);
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError'
    });
  });
}

function checkParseFormat(path, paths) {
  paths.forEach(([element, root]) => {
    const output = path.parse(element);
    assert.strictEqual(typeof output.root, 'string');
    assert.strictEqual(typeof output.dir, 'string');
    assert.strictEqual(typeof output.base, 'string');
    assert.strictEqual(typeof output.ext, 'string');
    assert.strictEqual(typeof output.name, 'string');
    assert.strictEqual(path.format(output), element);
    assert.strictEqual(output.root, root);
    assert(output.dir.startsWith(output.root));
    assert.strictEqual(output.dir, output.dir ? path.dirname(element) : '');
    assert.strictEqual(output.base, path.basename(element));
    assert.strictEqual(output.ext, path.extname(element));
  });
}

function checkSpecialCaseParseFormat(path, testCases) {
  testCases.forEach(([element, expect]) => {
    assert.deepStrictEqual(path.parse(element), expect);
  });
}

function checkFormat(path, testCases) {
  testCases.forEach(([element, expect]) => {
    assert.strictEqual(path.format(element), expect);
  });

  [null, undefined, 1, true, false, 'string'].forEach((pathObject) => {
    assert.throws(() => {
      path.format(pathObject);
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: 'The "pathObject" argument must be of type object.' +
               common.invalidArgTypeHelper(pathObject)
    });
  });
}

// See https://github.com/nodejs/node/issues/44343
assert.strictEqual(path.format({ name: 'x', ext: 'png' }), 'x.png');
assert.strictEqual(path.format({ name: 'x', ext: '.png' }), 'x.png');
