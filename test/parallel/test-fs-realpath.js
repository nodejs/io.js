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
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('process.chdir is not available in Workers');
}

const assert = require('assert');
const fs = require('fs');
const path = require('path');
let async_completed = 0;
let async_expected = 0;
const unlink = [];
const skipSymlinks = !common.canCreateSymLink();
const tmpDir = tmpdir.path;

tmpdir.refresh();

let root = '/';
let assertEqualPath = assert.strictEqual;
if (common.isWindows) {
  // Something like "C:\\"
  root = process.cwd().slice(0, 3);
  assertEqualPath = function(path_left, path_right, message) {
    assert
      .strictEqual(path_left.toLowerCase(), path_right.toLowerCase(), message);
  };
}

process.nextTick(runTest);

function tmp(p) {
  return path.join(tmpDir, p);
}

const targetsAbsDir = path.join(tmpDir, 'targets');
const tmpAbsDir = tmpDir;

// Set up targetsAbsDir and expected subdirectories
fs.mkdirSync(targetsAbsDir);
fs.mkdirSync(path.join(targetsAbsDir, 'nested-index'));
fs.mkdirSync(path.join(targetsAbsDir, 'nested-index', 'one'));
fs.mkdirSync(path.join(targetsAbsDir, 'nested-index', 'two'));

function asynctest(testBlock, args, callback, assertBlock) {
  async_expected++;
  testBlock.apply(testBlock, args.concat(function(err) {
    let ignoreError = false;
    if (assertBlock) {
      try {
        ignoreError = assertBlock.apply(assertBlock, arguments);
      } catch (e) {
        err = e;
      }
    }
    async_completed++;
    callback(ignoreError ? null : err);
  }));
}

// sub-tests:
function test_simple_error_callback(realpath, realpathSync, cb) {
  realpath('/this/path/does/not/exist', common.mustCall(function(err, s) {
    assert(err);
    assert(!s);
    cb();
  }));
}

function test_simple_error_cb_with_null_options(realpath, realpathSync, cb) {
  realpath('/this/path/does/not/exist', null, common.mustCall(function(err, s) {
    assert(err);
    assert(!s);
    cb();
  }));
}

function test_simple_relative_symlink(realpath, realpathSync, callback) {
  console.log('test_simple_relative_symlink');
  if (skipSymlinks) {
    common.printSkipMessage('symlink test (no privs)');
    return callback();
  }
  const entry = `${tmpDir}/symlink`;
  const expected = `${tmpDir}/cycles/root.js`;
  [
    [entry, `../${path.basename(tmpDir)}/cycles/root.js`],
  ].forEach(function(t) {
    try { fs.unlinkSync(t[0]); } catch {
      // Continue regardless of error.
    }
    console.log('fs.symlinkSync(%j, %j, %j)', t[1], t[0], 'file');
    fs.symlinkSync(t[1], t[0], 'file');
    unlink.push(t[0]);
  });
  const result = realpathSync(entry);
  assertEqualPath(result, path.resolve(expected));
  asynctest(realpath, [entry], callback, function(err, result) {
    assertEqualPath(result, path.resolve(expected));
  });
}

function test_simple_absolute_symlink(realpath, realpathSync, callback) {
  console.log('test_simple_absolute_symlink');

  // This one should still run, even if skipSymlinks is set,
  // because it uses a junction.
  const type = skipSymlinks ? 'junction' : 'dir';

  console.log('using type=%s', type);

  const entry = `${tmpAbsDir}/symlink`;
  const expected = fixtures.path('nested-index', 'one');
  [
    [entry, expected],
  ].forEach(function(t) {
    try { fs.unlinkSync(t[0]); } catch {
      // Continue regardless of error.
    }
    console.error('fs.symlinkSync(%j, %j, %j)', t[1], t[0], type);
    fs.symlinkSync(t[1], t[0], type);
    unlink.push(t[0]);
  });
  const result = realpathSync(entry);
  assertEqualPath(result, path.resolve(expected));
  asynctest(realpath, [entry], callback, function(err, result) {
    assertEqualPath(result, path.resolve(expected));
  });
}

function test_deep_relative_file_symlink(realpath, realpathSync, callback) {
  console.log('test_deep_relative_file_symlink');
  if (skipSymlinks) {
    common.printSkipMessage('symlink test (no privs)');
    return callback();
  }

  const expected = fixtures.path('cycles', 'root.js');
  const linkData1 = path
                      .relative(path.join(targetsAbsDir, 'nested-index', 'one'),
                                expected);
  const linkPath1 = path.join(targetsAbsDir,
                              'nested-index', 'one', 'symlink1.js');
  try { fs.unlinkSync(linkPath1); } catch {
    // Continue regardless of error.
  }
  fs.symlinkSync(linkData1, linkPath1, 'file');

  const linkData2 = '../one/symlink1.js';
  const entry = path.join(targetsAbsDir,
                          'nested-index', 'two', 'symlink1-b.js');
  try { fs.unlinkSync(entry); } catch {
    // Continue regardless of error.
  }
  fs.symlinkSync(linkData2, entry, 'file');
  unlink.push(linkPath1);
  unlink.push(entry);

  assertEqualPath(realpathSync(entry), path.resolve(expected));
  asynctest(realpath, [entry], callback, function(err, result) {
    assertEqualPath(result, path.resolve(expected));
  });
}

function test_deep_relative_dir_symlink(realpath, realpathSync, callback) {
  console.log('test_deep_relative_dir_symlink');
  if (skipSymlinks) {
    common.printSkipMessage('symlink test (no privs)');
    return callback();
  }
  const expected = fixtures.path('cycles', 'folder');
  const path1b = path.join(targetsAbsDir, 'nested-index', 'one');
  const linkPath1b = path.join(path1b, 'symlink1-dir');
  const linkData1b = path.relative(path1b, expected);
  try { fs.unlinkSync(linkPath1b); } catch {
    // Continue regardless of error.
  }
  fs.symlinkSync(linkData1b, linkPath1b, 'dir');

  const linkData2b = '../one/symlink1-dir';
  const entry = path.join(targetsAbsDir,
                          'nested-index', 'two', 'symlink12-dir');
  try { fs.unlinkSync(entry); } catch {
    // Continue regardless of error.
  }
  fs.symlinkSync(linkData2b, entry, 'dir');
  unlink.push(linkPath1b);
  unlink.push(entry);

  assertEqualPath(realpathSync(entry), path.resolve(expected));

  asynctest(realpath, [entry], callback, function(err, result) {
    assertEqualPath(result, path.resolve(expected));
  });
}

function test_cyclic_link_protection(realpath, realpathSync, callback) {
  console.log('test_cyclic_link_protection');
  if (skipSymlinks) {
    common.printSkipMessage('symlink test (no privs)');
    return callback();
  }
  const entry = path.join(tmpDir, '/cycles/realpath-3a');
  [
    [entry, '../cycles/realpath-3b'],
    [path.join(tmpDir, '/cycles/realpath-3b'), '../cycles/realpath-3c'],
    [path.join(tmpDir, '/cycles/realpath-3c'), '../cycles/realpath-3a'],
  ].forEach(function(t) {
    try { fs.unlinkSync(t[0]); } catch {
      // Continue regardless of error.
    }
    fs.symlinkSync(t[1], t[0], 'dir');
    unlink.push(t[0]);
  });
  assert.throws(() => {
    realpathSync(entry);
  }, { code: 'ELOOP', name: 'Error' });
  asynctest(
    realpath, [entry], callback, common.mustCall(function(err, result) {
      assert.strictEqual(err.path, entry);
      assert.strictEqual(result, undefined);
      return true;
    }));
}

function test_cyclic_link_overprotection(realpath, realpathSync, callback) {
  console.log('test_cyclic_link_overprotection');
  if (skipSymlinks) {
    common.printSkipMessage('symlink test (no privs)');
    return callback();
  }
  const cycles = `${tmpDir}/cycles`;
  const expected = realpathSync(cycles);
  const folder = `${cycles}/folder`;
  const link = `${folder}/cycles`;
  let testPath = cycles;
  testPath += '/folder/cycles'.repeat(10);
  try { fs.unlinkSync(link); } catch {
    // Continue regardless of error.
  }
  fs.symlinkSync(cycles, link, 'dir');
  unlink.push(link);
  assertEqualPath(realpathSync(testPath), path.resolve(expected));
  asynctest(realpath, [testPath], callback, function(er, res) {
    assertEqualPath(res, path.resolve(expected));
  });
}

function test_relative_input_cwd(realpath, realpathSync, callback) {
  console.log('test_relative_input_cwd');
  if (skipSymlinks) {
    common.printSkipMessage('symlink test (no privs)');
    return callback();
  }

  // We need to calculate the relative path to the tmp dir from cwd
  const entrydir = process.cwd();
  const entry = path.relative(entrydir,
                              path.join(`${tmpDir}/cycles/realpath-3a`));
  const expected = `${tmpDir}/cycles/root.js`;
  [
    [entry, '../cycles/realpath-3b'],
    [`${tmpDir}/cycles/realpath-3b`, '../cycles/realpath-3c'],
    [`${tmpDir}/cycles/realpath-3c`, 'root.js'],
  ].forEach(function(t) {
    const fn = t[0];
    console.error('fn=%j', fn);
    try { fs.unlinkSync(fn); } catch {
      // Continue regardless of error.
    }
    const b = path.basename(t[1]);
    const type = (b === 'root.js' ? 'file' : 'dir');
    console.log('fs.symlinkSync(%j, %j, %j)', t[1], fn, type);
    fs.symlinkSync(t[1], fn, 'file');
    unlink.push(fn);
  });

  const origcwd = process.cwd();
  process.chdir(entrydir);
  assertEqualPath(realpathSync(entry), path.resolve(expected));
  asynctest(realpath, [entry], callback, function(err, result) {
    process.chdir(origcwd);
    assertEqualPath(result, path.resolve(expected));
    return true;
  });
}

function test_deep_symlink_mix(realpath, realpathSync, callback) {
  console.log('test_deep_symlink_mix');
  if (common.isWindows) {
    // This one is a mix of files and directories, and it's quite tricky
    // to get the file/dir links sorted out correctly.
    common.printSkipMessage('symlink test (no privs)');
    return callback();
  }

  // /tmp/node-test-realpath-f1 -> $tmpDir/node-test-realpath-d1/foo
  // /tmp/node-test-realpath-d1 -> $tmpDir/node-test-realpath-d2
  // /tmp/node-test-realpath-d2/foo -> $tmpDir/node-test-realpath-f2
  // /tmp/node-test-realpath-f2
  //   -> $tmpDir/targets/nested-index/one/realpath-c
  // $tmpDir/targets/nested-index/one/realpath-c
  //   -> $tmpDir/targets/nested-index/two/realpath-c
  // $tmpDir/targets/nested-index/two/realpath-c -> $tmpDir/cycles/root.js
  // $tmpDir/targets/cycles/root.js (hard)

  const entry = tmp('node-test-realpath-f1');
  try { fs.unlinkSync(tmp('node-test-realpath-d2/foo')); } catch {
    // Continue regardless of error.
  }
  try { fs.rmdirSync(tmp('node-test-realpath-d2')); } catch {
    // Continue regardless of error.
  }
  fs.mkdirSync(tmp('node-test-realpath-d2'), 0o700);
  try {
    [
      [entry, `${tmpDir}/node-test-realpath-d1/foo`],
      [tmp('node-test-realpath-d1'),
       `${tmpDir}/node-test-realpath-d2`],
      [tmp('node-test-realpath-d2/foo'), '../node-test-realpath-f2'],
      [tmp('node-test-realpath-f2'),
       `${targetsAbsDir}/nested-index/one/realpath-c`],
      [`${targetsAbsDir}/nested-index/one/realpath-c`,
       `${targetsAbsDir}/nested-index/two/realpath-c`],
      [`${targetsAbsDir}/nested-index/two/realpath-c`,
       `${tmpDir}/cycles/root.js`],
    ].forEach(function(t) {
      try { fs.unlinkSync(t[0]); } catch {
        // Continue regardless of error.
      }
      fs.symlinkSync(t[1], t[0]);
      unlink.push(t[0]);
    });
  } finally {
    unlink.push(tmp('node-test-realpath-d2'));
  }
  const expected = `${tmpAbsDir}/cycles/root.js`;
  assertEqualPath(realpathSync(entry), path.resolve(expected));
  asynctest(realpath, [entry], callback, function(err, result) {
    assertEqualPath(result, path.resolve(expected));
    return true;
  });
}

function test_non_symlinks(realpath, realpathSync, callback) {
  console.log('test_non_symlinks');
  const entrydir = path.dirname(tmpAbsDir);
  const entry = `${tmpAbsDir.slice(entrydir.length + 1)}/cycles/root.js`;
  const expected = `${tmpAbsDir}/cycles/root.js`;
  const origcwd = process.cwd();
  process.chdir(entrydir);
  assertEqualPath(realpathSync(entry), path.resolve(expected));
  asynctest(realpath, [entry], callback, function(err, result) {
    process.chdir(origcwd);
    assertEqualPath(result, path.resolve(expected));
    return true;
  });
}

const upone = path.join(process.cwd(), '..');
function test_escape_cwd(realpath, realpathSync, cb) {
  console.log('test_escape_cwd');
  asynctest(realpath, ['..'], cb, function(er, uponeActual) {
    assertEqualPath(
      upone, uponeActual,
      `realpath("..") expected: ${path.resolve(upone)} actual:${uponeActual}`);
  });
}

function test_upone_actual(realpath, realpathSync, cb) {
  console.log('test_upone_actual');
  const uponeActual = realpathSync('..');
  assertEqualPath(upone, uponeActual);
  cb();
}

// Going up with .. multiple times
// .
// `-- a/
//     |-- b/
//     |   `-- e -> ..
//     `-- d -> ..
// realpath(a/b/e/d/a/b/e/d/a) ==> a
function test_up_multiple(realpath, realpathSync, cb) {
  console.error('test_up_multiple');
  if (skipSymlinks) {
    common.printSkipMessage('symlink test (no privs)');
    return cb();
  }
  const tmpdir = require('../common/tmpdir');
  tmpdir.refresh();
  fs.mkdirSync(tmp('a'), 0o755);
  fs.mkdirSync(tmp('a/b'), 0o755);
  fs.symlinkSync('..', tmp('a/d'), 'dir');
  unlink.push(tmp('a/d'));
  fs.symlinkSync('..', tmp('a/b/e'), 'dir');
  unlink.push(tmp('a/b/e'));

  const abedabed = tmp('abedabed'.split('').join('/'));
  const abedabed_real = tmp('');

  const abedabeda = tmp('abedabeda'.split('').join('/'));
  const abedabeda_real = tmp('a');

  assertEqualPath(realpathSync(abedabeda), abedabeda_real);
  assertEqualPath(realpathSync(abedabed), abedabed_real);

  realpath(abedabeda, function(er, real) {
    assert.ifError(er);
    assertEqualPath(abedabeda_real, real);
    realpath(abedabed, function(er, real) {
      assert.ifError(er);
      assertEqualPath(abedabed_real, real);
      cb();
    });
  });
}


// Going up with .. multiple times with options = null
// .
// `-- a/
//     |-- b/
//     |   `-- e -> ..
//     `-- d -> ..
// realpath(a/b/e/d/a/b/e/d/a) ==> a
function test_up_multiple_with_null_options(realpath, realpathSync, cb) {
  console.error('test_up_multiple');
  if (skipSymlinks) {
    common.printSkipMessage('symlink test (no privs)');
    return cb();
  }
  const tmpdir = require('../common/tmpdir');
  tmpdir.refresh();
  fs.mkdirSync(tmp('a'), 0o755);
  fs.mkdirSync(tmp('a/b'), 0o755);
  fs.symlinkSync('..', tmp('a/d'), 'dir');
  unlink.push(tmp('a/d'));
  fs.symlinkSync('..', tmp('a/b/e'), 'dir');
  unlink.push(tmp('a/b/e'));

  const abedabed = tmp('abedabed'.split('').join('/'));
  const abedabed_real = tmp('');

  const abedabeda = tmp('abedabeda'.split('').join('/'));
  const abedabeda_real = tmp('a');

  assertEqualPath(realpathSync(abedabeda), abedabeda_real);
  assertEqualPath(realpathSync(abedabed), abedabed_real);

  realpath(abedabeda, null, function(er, real) {
    assert.ifError(er);
    assertEqualPath(abedabeda_real, real);
    realpath(abedabed, null, function(er, real) {
      assert.ifError(er);
      assertEqualPath(abedabed_real, real);
      cb();
    });
  });
}

// Absolute symlinks with children.
// .
// `-- a/
//     |-- b/
//     |   `-- c/
//     |       `-- x.txt
//     `-- link -> /tmp/node-test-realpath-abs-kids/a/b/
// realpath(root+'/a/link/c/x.txt') ==> root+'/a/b/c/x.txt'
function test_abs_with_kids(realpath, realpathSync, cb) {
  console.log('test_abs_with_kids');

  // This one should still run, even if skipSymlinks is set,
  // because it uses a junction.
  const type = skipSymlinks ? 'junction' : 'dir';

  console.log('using type=%s', type);

  const root = `${tmpAbsDir}/node-test-realpath-abs-kids`;
  function cleanup() {
    ['/a/b/c/x.txt',
     '/a/link',
    ].forEach(function(file) {
      try { fs.unlinkSync(root + file); } catch {
        // Continue regardless of error.
      }
    });
    ['/a/b/c',
     '/a/b',
     '/a',
     '',
    ].forEach(function(folder) {
      try { fs.rmdirSync(root + folder); } catch {
        // Continue regardless of error.
      }
    });
  }

  function setup() {
    cleanup();
    ['',
     '/a',
     '/a/b',
     '/a/b/c',
    ].forEach(function(folder) {
      console.log(`mkdir ${root}${folder}`);
      fs.mkdirSync(root + folder, 0o700);
    });
    fs.writeFileSync(`${root}/a/b/c/x.txt`, 'foo');
    fs.symlinkSync(`${root}/a/b`, `${root}/a/link`, type);
  }
  setup();
  const linkPath = `${root}/a/link/c/x.txt`;
  const expectPath = `${root}/a/b/c/x.txt`;
  const actual = realpathSync(linkPath);
  // console.log({link:linkPath,expect:expectPath,actual:actual},'sync');
  assertEqualPath(actual, path.resolve(expectPath));
  asynctest(realpath, [linkPath], cb, function(er, actual) {
    // console.log({link:linkPath,expect:expectPath,actual:actual},'async');
    assertEqualPath(actual, path.resolve(expectPath));
    cleanup();
  });
}

function test_root(realpath, realpathSync, cb) {
  assertEqualPath(root, realpathSync('/'));
  realpath('/', function(err, result) {
    assert.ifError(err);
    assertEqualPath(root, result);
    cb();
  });
}

function test_root_with_null_options(realpath, realpathSync, cb) {
  realpath('/', null, function(err, result) {
    assert.ifError(err);
    assertEqualPath(root, result);
    cb();
  });
}

// ----------------------------------------------------------------------------

const tests = [
  test_simple_error_callback,
  test_simple_error_cb_with_null_options,
  test_simple_relative_symlink,
  test_simple_absolute_symlink,
  test_deep_relative_file_symlink,
  test_deep_relative_dir_symlink,
  test_cyclic_link_protection,
  test_cyclic_link_overprotection,
  test_relative_input_cwd,
  test_deep_symlink_mix,
  test_non_symlinks,
  test_escape_cwd,
  test_upone_actual,
  test_abs_with_kids,
  test_up_multiple,
  test_up_multiple_with_null_options,
  test_root,
  test_root_with_null_options,
];
const numtests = tests.length;
let testsRun = 0;
function runNextTest(err) {
  assert.ifError(err);
  const test = tests.shift();
  if (!test) {
    return console.log(`${numtests} subtests completed OK for fs.realpath`);
  }
  testsRun++;
  test(fs.realpath, fs.realpathSync, common.mustSucceed(() => {
    testsRun++;
    test(fs.realpath.native,
         fs.realpathSync.native,
         common.mustCall(runNextTest));
  }));
}

function runTest() {
  const tmpDirs = ['cycles', 'cycles/folder'];
  tmpDirs.forEach(function(t) {
    t = tmp(t);
    fs.mkdirSync(t, 0o700);
  });
  fs.writeFileSync(tmp('cycles/root.js'), "console.error('roooot!');");
  console.error('start tests');
  runNextTest();
}


process.on('exit', function() {
  assert.strictEqual(2 * numtests, testsRun);
  assert.strictEqual(async_completed, async_expected);
});
