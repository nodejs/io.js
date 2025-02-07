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
const fs = require('fs');
const path = require('path');
const { isMainThread } = require('worker_threads');

const tmpdir = require('../common/tmpdir');
tmpdir.refresh();

let dirc = 0;
function nextdir() {
  return `test${++dirc}`;
}

// fs.mkdir creates directory using assigned path
{
  const pathname = tmpdir.resolve(nextdir());

  fs.mkdir(pathname, common.mustCall(function(err) {
    assert.strictEqual(err, null);
    assert.strictEqual(fs.existsSync(pathname), true);
  }));
}

// fs.mkdir creates directory with assigned mode value
{
  const pathname = tmpdir.resolve(nextdir());

  fs.mkdir(pathname, 0o777, common.mustCall(function(err) {
    assert.strictEqual(err, null);
    assert.strictEqual(fs.existsSync(pathname), true);
  }));
}

// fs.mkdir creates directory with mode passed as an options object
{
  const pathname = tmpdir.resolve(nextdir());

  fs.mkdir(pathname, common.mustNotMutateObjectDeep({ mode: 0o777 }), common.mustCall(function(err) {
    assert.strictEqual(err, null);
    assert.strictEqual(fs.existsSync(pathname), true);
  }));
}

// fs.mkdirSync creates directory with mode passed as an options object
{
  const pathname = tmpdir.resolve(nextdir());

  fs.mkdirSync(pathname, common.mustNotMutateObjectDeep({ mode: 0o777 }));

  assert.strictEqual(fs.existsSync(pathname), true);
}

// mkdirSync successfully creates directory from given path
{
  const pathname = tmpdir.resolve(nextdir());

  fs.mkdirSync(pathname);

  const exists = fs.existsSync(pathname);
  assert.strictEqual(exists, true);
}

// mkdirSync and mkdir require path to be a string, buffer or url.
// Anything else generates an error.
[false, 1, {}, [], null, undefined].forEach((i) => {
  assert.throws(
    () => fs.mkdir(i, common.mustNotCall()),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError'
    }
  );
  assert.throws(
    () => fs.mkdirSync(i),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError'
    }
  );
});

// mkdirpSync when both top-level, and sub-folders do not exist.
{
  const pathname = tmpdir.resolve(nextdir(), nextdir());

  fs.mkdirSync(pathname, common.mustNotMutateObjectDeep({ recursive: true }));

  const exists = fs.existsSync(pathname);
  assert.strictEqual(exists, true);
  assert.strictEqual(fs.statSync(pathname).isDirectory(), true);
}

// mkdirpSync when folder already exists.
{
  const pathname = tmpdir.resolve(nextdir(), nextdir());

  fs.mkdirSync(pathname, { recursive: true });
  // Should not cause an error.
  fs.mkdirSync(pathname, { recursive: true });

  const exists = fs.existsSync(pathname);
  assert.strictEqual(exists, true);
  assert.strictEqual(fs.statSync(pathname).isDirectory(), true);
}

// mkdirpSync ../
{
  const pathname = `${tmpdir.path}/${nextdir()}/../${nextdir()}/${nextdir()}`;
  fs.mkdirSync(pathname, { recursive: true });
  const exists = fs.existsSync(pathname);
  assert.strictEqual(exists, true);
  assert.strictEqual(fs.statSync(pathname).isDirectory(), true);
}

// mkdirpSync when path is a file.
{
  const pathname = tmpdir.resolve(nextdir(), nextdir());

  fs.mkdirSync(path.dirname(pathname));
  fs.writeFileSync(pathname, '', 'utf8');

  assert.throws(
    () => { fs.mkdirSync(pathname, common.mustNotMutateObjectDeep({ recursive: true })); },
    {
      code: 'EEXIST',
      message: /EEXIST: .*mkdir/,
      name: 'Error',
      syscall: 'mkdir',
    }
  );
}

// mkdirpSync when part of the path is a file.
{
  const filename = tmpdir.resolve(nextdir(), nextdir());
  const pathname = path.join(filename, nextdir(), nextdir());

  fs.mkdirSync(path.dirname(filename));
  fs.writeFileSync(filename, '', 'utf8');

  assert.throws(
    () => { fs.mkdirSync(pathname, { recursive: true }); },
    {
      code: 'ENOTDIR',
      message: /ENOTDIR: .*mkdir/,
      name: 'Error',
      syscall: 'mkdir',
      path: pathname // See: https://github.com/nodejs/node/issues/28015
    }
  );
}

// `mkdirp` when folder does not yet exist.
{
  const pathname = tmpdir.resolve(nextdir(), nextdir());

  fs.mkdir(pathname, common.mustNotMutateObjectDeep({ recursive: true }), common.mustCall(function(err) {
    assert.strictEqual(err, null);
    assert.strictEqual(fs.existsSync(pathname), true);
    assert.strictEqual(fs.statSync(pathname).isDirectory(), true);
  }));
}

// `mkdirp` when path is a file.
{
  const pathname = tmpdir.resolve(nextdir(), nextdir());

  fs.mkdirSync(path.dirname(pathname));
  fs.writeFileSync(pathname, '', 'utf8');
  fs.mkdir(pathname, common.mustNotMutateObjectDeep({ recursive: true }), common.mustCall((err) => {
    assert.strictEqual(err.code, 'EEXIST');
    assert.strictEqual(err.syscall, 'mkdir');
    assert.strictEqual(fs.statSync(pathname).isDirectory(), false);
  }));
}

// `mkdirp` when part of the path is a file.
{
  const filename = tmpdir.resolve(nextdir(), nextdir());
  const pathname = path.join(filename, nextdir(), nextdir());

  fs.mkdirSync(path.dirname(filename));
  fs.writeFileSync(filename, '', 'utf8');
  fs.mkdir(pathname, common.mustNotMutateObjectDeep({ recursive: true }), common.mustCall((err) => {
    assert.strictEqual(err.code, 'ENOTDIR');
    assert.strictEqual(err.syscall, 'mkdir');
    assert.strictEqual(fs.existsSync(pathname), false);
    // See: https://github.com/nodejs/node/issues/28015
    // The path field varies slightly in Windows errors, vs., other platforms
    // see: https://github.com/libuv/libuv/issues/2661, for this reason we
    // use startsWith() rather than comparing to the full "pathname".
    assert(err.path.startsWith(filename));
  }));
}

// mkdirpSync dirname loop
// XXX: windows and smartos have issues removing a directory that you're in.
if (isMainThread && (common.isLinux || common.isMacOS)) {
  const pathname = tmpdir.resolve(nextdir());
  fs.mkdirSync(pathname);
  process.chdir(pathname);
  fs.rmdirSync(pathname);
  assert.throws(
    () => { fs.mkdirSync('X', common.mustNotMutateObjectDeep({ recursive: true })); },
    {
      code: 'ENOENT',
      message: /ENOENT: .*mkdir/,
      name: 'Error',
      syscall: 'mkdir',
    }
  );
  fs.mkdir('X', common.mustNotMutateObjectDeep({ recursive: true }), (err) => {
    assert.strictEqual(err.code, 'ENOENT');
    assert.strictEqual(err.syscall, 'mkdir');
  });
}

// mkdirSync and mkdir require options.recursive to be a boolean.
// Anything else generates an error.
{
  const pathname = tmpdir.resolve(nextdir());
  ['', 1, {}, [], null, Symbol('test'), () => {}].forEach((recursive) => {
    const received = common.invalidArgTypeHelper(recursive);
    assert.throws(
      () => fs.mkdir(pathname, common.mustNotMutateObjectDeep({ recursive }), common.mustNotCall()),
      {
        code: 'ERR_INVALID_ARG_TYPE',
        name: 'TypeError',
        message: 'The "options.recursive" property must be of type boolean.' +
          received
      }
    );
    assert.throws(
      () => fs.mkdirSync(pathname, common.mustNotMutateObjectDeep({ recursive })),
      {
        code: 'ERR_INVALID_ARG_TYPE',
        name: 'TypeError',
        message: 'The "options.recursive" property must be of type boolean.' +
          received
      }
    );
  });
}

// `mkdirp` returns first folder created, when all folders are new.
{
  const dir1 = nextdir();
  const dir2 = nextdir();
  const firstPathCreated = tmpdir.resolve(dir1);
  const pathname = tmpdir.resolve(dir1, dir2);

  fs.mkdir(pathname, common.mustNotMutateObjectDeep({ recursive: true }), common.mustCall(function(err, result) {
    assert.strictEqual(err, null);
    assert.strictEqual(fs.existsSync(pathname), true);
    assert.strictEqual(fs.statSync(pathname).isDirectory(), true);
    assert.strictEqual(result, path.toNamespacedPath(firstPathCreated));
  }));
}

// `mkdirp` returns first folder created, when last folder is new.
{
  const dir1 = nextdir();
  const dir2 = nextdir();
  const pathname = tmpdir.resolve(dir1, dir2);
  fs.mkdirSync(tmpdir.resolve(dir1));
  fs.mkdir(pathname, common.mustNotMutateObjectDeep({ recursive: true }), common.mustCall(function(err, result) {
    assert.strictEqual(err, null);
    assert.strictEqual(fs.existsSync(pathname), true);
    assert.strictEqual(fs.statSync(pathname).isDirectory(), true);
    assert.strictEqual(result, path.toNamespacedPath(pathname));
  }));
}

// `mkdirp` returns undefined, when no new folders are created.
{
  const dir1 = nextdir();
  const dir2 = nextdir();
  const pathname = tmpdir.resolve(dir1, dir2);
  fs.mkdirSync(tmpdir.resolve(dir1, dir2), common.mustNotMutateObjectDeep({ recursive: true }));
  fs.mkdir(pathname, common.mustNotMutateObjectDeep({ recursive: true }), common.mustCall(function(err, path) {
    assert.strictEqual(err, null);
    assert.strictEqual(fs.existsSync(pathname), true);
    assert.strictEqual(fs.statSync(pathname).isDirectory(), true);
    assert.strictEqual(path, undefined);
  }));
}

// `mkdirp.sync` returns first folder created, when all folders are new.
{
  const dir1 = nextdir();
  const dir2 = nextdir();
  const firstPathCreated = tmpdir.resolve(dir1);
  const pathname = tmpdir.resolve(dir1, dir2);
  const p = fs.mkdirSync(pathname, common.mustNotMutateObjectDeep({ recursive: true }));
  assert.strictEqual(fs.existsSync(pathname), true);
  assert.strictEqual(fs.statSync(pathname).isDirectory(), true);
  assert.strictEqual(p, path.toNamespacedPath(firstPathCreated));
}

// `mkdirp.sync` returns first folder created, when last folder is new.
{
  const dir1 = nextdir();
  const dir2 = nextdir();
  const pathname = tmpdir.resolve(dir1, dir2);
  fs.mkdirSync(tmpdir.resolve(dir1), common.mustNotMutateObjectDeep({ recursive: true }));
  const p = fs.mkdirSync(pathname, common.mustNotMutateObjectDeep({ recursive: true }));
  assert.strictEqual(fs.existsSync(pathname), true);
  assert.strictEqual(fs.statSync(pathname).isDirectory(), true);
  assert.strictEqual(p, path.toNamespacedPath(pathname));
}

// `mkdirp.sync` returns undefined, when no new folders are created.
{
  const dir1 = nextdir();
  const dir2 = nextdir();
  const pathname = tmpdir.resolve(dir1, dir2);
  fs.mkdirSync(tmpdir.resolve(dir1, dir2), common.mustNotMutateObjectDeep({ recursive: true }));
  const p = fs.mkdirSync(pathname, common.mustNotMutateObjectDeep({ recursive: true }));
  assert.strictEqual(fs.existsSync(pathname), true);
  assert.strictEqual(fs.statSync(pathname).isDirectory(), true);
  assert.strictEqual(p, undefined);
}

// `mkdirp.promises` returns first folder created, when all folders are new.
{
  const dir1 = nextdir();
  const dir2 = nextdir();
  const firstPathCreated = tmpdir.resolve(dir1);
  const pathname = tmpdir.resolve(dir1, dir2);
  async function testCase() {
    const p = await fs.promises.mkdir(pathname, common.mustNotMutateObjectDeep({ recursive: true }));
    assert.strictEqual(fs.existsSync(pathname), true);
    assert.strictEqual(fs.statSync(pathname).isDirectory(), true);
    assert.strictEqual(p, path.toNamespacedPath(firstPathCreated));
  }
  testCase();
}

// Keep the event loop alive so the async mkdir() requests
// have a chance to run (since they don't ref the event loop).
process.nextTick(() => {});
