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
const assert = require('assert');
const fs = require('fs');

const tmpdir = require('../common/tmpdir');

// Test creating and reading symbolic link
const linkData = fixtures.path('cycles');
const linkPath = tmpdir.resolve('cycles_link');

tmpdir.refresh();

fs.symlink(linkData, linkPath, 'junction', common.mustSucceed(() => {
  fs.lstat(linkPath, common.mustSucceed((stats) => {
    assert.ok(stats.isSymbolicLink());

    fs.readlink(linkPath, common.mustSucceed((destination) => {
      assert.strictEqual(destination, linkData);

      fs.unlink(linkPath, common.mustSucceed(() => {
        assert(!fs.existsSync(linkPath));
        assert(fs.existsSync(linkData));
      }));
    }));
  }));
}));

// Test invalid symlink
{
  const linkData = fixtures.path('/not/exists/dir');
  const linkPath = tmpdir.resolve('invalid_junction_link');

  fs.symlink(linkData, linkPath, 'junction', common.mustSucceed(() => {
    assert(!fs.existsSync(linkPath));

    fs.unlink(linkPath, common.mustSucceed(() => {
      assert(!fs.existsSync(linkPath));
    }));
  }));
}
