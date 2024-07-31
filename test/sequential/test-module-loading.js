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
const tmpdir = require('../common/tmpdir');

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const backslash = /\\/g;

process.on('warning', common.mustCall());

console.error('load test-module-loading.js');

assert.strictEqual(require.main.id, '.');
assert.strictEqual(require.main, module);
assert.strictEqual(process.mainModule, module);

// Assert that it's *not* the main module in the required module.
require('../fixtures/not-main-module.js');

{
  // Require a file with a request that includes the extension
  const a_js = require('../fixtures/a.js');
  assert.strictEqual(a_js.number, 42);
}

{
  // Require a file without any extensions
  const foo_no_ext = require('../fixtures/foo');
  assert.strictEqual(foo_no_ext.foo, 'ok');
}

const a = require('../fixtures/a');
const d = require('../fixtures/b/d');
const d2 = require('../fixtures/b/d');

{
  const c = require('../fixtures/b/c');
  // Absolute
  const d3 = require(path.join(__dirname, '../fixtures/b/d'));
  // Relative
  const d4 = require('../fixtures/b/d');

  assert.ok(a.A instanceof Function);
  assert.strictEqual(a.A(), 'A');

  assert.ok(a.C instanceof Function);
  assert.strictEqual(a.C(), 'C');

  assert.ok(a.D instanceof Function);
  assert.strictEqual(a.D(), 'D');

  assert.ok(d.D instanceof Function);
  assert.strictEqual(d.D(), 'D');

  assert.ok(d2.D instanceof Function);
  assert.strictEqual(d2.D(), 'D');

  assert.ok(d3.D instanceof Function);
  assert.strictEqual(d3.D(), 'D');

  assert.ok(d4.D instanceof Function);
  assert.strictEqual(d4.D(), 'D');

  assert.ok((new a.SomeClass()) instanceof c.SomeClass);
}

{
  console.error('test index.js modules ids and relative loading');
  const one = require('../fixtures/nested-index/one');
  const two = require('../fixtures/nested-index/two');
  assert.notStrictEqual(one.hello, two.hello);
}

{
  console.error('test index.js in a folder with a trailing slash');
  const three = require('../fixtures/nested-index/three');
  const threeFolder = require('../fixtures/nested-index/three/');
  const threeIndex = require('../fixtures/nested-index/three/index.js');
  assert.strictEqual(threeFolder, threeIndex);
  assert.notStrictEqual(threeFolder, three);
}

assert.strictEqual(require('../fixtures/packages/index').ok, 'ok');
assert.strictEqual(require('../fixtures/packages/main').ok, 'ok');
assert.strictEqual(require('../fixtures/packages/main-index').ok, 'ok');

common.expectWarning(
  'DeprecationWarning',
  "Invalid 'main' field in '" +
  path.toNamespacedPath(require.resolve('../fixtures/packages/missing-main/package.json')) +
  "' of 'doesnotexist.js'. Please either fix that or report it to the" +
  ' module author',
  'DEP0128');
assert.strictEqual(require('../fixtures/packages/missing-main').ok, 'ok');

assert.throws(
  () => require('../fixtures/packages/missing-main-no-index'),
  {
    code: 'MODULE_NOT_FOUND',
    message: /packages[/\\]missing-main-no-index[/\\]doesnotexist\.js'\. Please.+package\.json.+valid "main"/,
    path: /fixtures[/\\]packages[/\\]missing-main-no-index[/\\]package\.json/,
    requestPath: /^\.\.[/\\]fixtures[/\\]packages[/\\]missing-main-no-index$/
  }
);

assert.throws(
  function() { require('../fixtures/packages/unparseable'); },
  { code: 'ERR_INVALID_PACKAGE_CONFIG' }
);

{
  console.error('test cycles containing a .. path');
  const root = require('../fixtures/cycles/root');
  const foo = require('../fixtures/cycles/folder/foo');
  assert.strictEqual(root.foo, foo);
  assert.strictEqual(root.sayHello(), root.hello);
}

console.error('test node_modules folders');
// Asserts are in the fixtures files themselves,
// since they depend on the folder structure.
require('../fixtures/node_modules/foo');

{
  console.error('test name clashes');
  // This one exists and should import the local module
  const my_path = require('../fixtures/path');
  assert.ok(my_path.path_func instanceof Function);
  // This one does not exist and should throw
  assert.throws(function() { require('./utils'); },
                /^Error: Cannot find module '\.\/utils'/);
}

let errorThrown = false;
try {
  require('../fixtures/throws_error');
} catch (e) {
  errorThrown = true;
  assert.strictEqual(e.message, 'blah');
}

assert.strictEqual(path.dirname(__filename), __dirname);

console.error('load custom file types with extensions');
require.extensions['.test'] = function(module, filename) {
  let content = fs.readFileSync(filename).toString();
  assert.strictEqual(content, 'this is custom source\n');
  content = content.replace('this is custom source',
                            'exports.test = \'passed\'');
  module._compile(content, filename);
};

assert.strictEqual(require('../fixtures/registerExt').test, 'passed');
// Unknown extension, load as .js
assert.strictEqual(require('../fixtures/registerExt.hello.world').test,
                   'passed');

console.error('load custom file types that return non-strings');
require.extensions['.test'] = function(module) {
  module.exports = {
    custom: 'passed'
  };
};

assert.strictEqual(require('../fixtures/registerExt2').custom, 'passed');

assert.strictEqual(require('../fixtures/foo').foo, 'ok');

// Should not attempt to load a directory
assert.throws(
  () => {
    tmpdir.refresh();
    require(tmpdir.path);
  },
  (err) => err.message.startsWith(`Cannot find module '${tmpdir.path}`)
);

{
  // Check load order is as expected
  console.error('load order');

  const loadOrder = '../fixtures/module-load-order/';

  require.extensions['.reg'] = require.extensions['.js'];
  require.extensions['.reg2'] = require.extensions['.js'];

  assert.strictEqual(require(`${loadOrder}file1`).file1, 'file1');
  assert.strictEqual(require(`${loadOrder}file2`).file2, 'file2.js');
  assert.throws(
    () => require(`${loadOrder}file3`),
    (e) => {
      // Not a real .node module, but we know we require'd the right thing.
      if (common.isOpenBSD) { // OpenBSD errors with non-ELF object error
        assert.match(e.message, /File not an ELF object/);
      } else {
        assert.match(e.message, /file3\.node/);
      }
      return true;
    }
  );

  assert.strictEqual(require(`${loadOrder}file4`).file4, 'file4.reg');
  assert.strictEqual(require(`${loadOrder}file5`).file5, 'file5.reg2');
  assert.strictEqual(require(`${loadOrder}file6`).file6, 'file6/index.js');
  assert.throws(
    () => require(`${loadOrder}file7`),
    (e) => {
      if (common.isOpenBSD) {
        assert.match(e.message, /File not an ELF object/);
      } else {
        assert.match(e.message.replace(backslash, '/'), /file7\/index\.node/);
      }
      return true;
    }
  );

  assert.strictEqual(require(`${loadOrder}file8`).file8, 'file8/index.reg');
  assert.strictEqual(require(`${loadOrder}file9`).file9, 'file9/index.reg2');
}

{
  // Make sure that module.require() is the same as
  // doing require() inside of that module.
  const parent = require('../fixtures/module-require/parent/');
  const child = require('../fixtures/module-require/child/');
  assert.strictEqual(child.loaded, parent.loaded);
}

{
  // Loading JSON files with require()
  // See https://github.com/nodejs/node-v0.x-archive/issues/1357.
  const json = require('../fixtures/packages/main/package.json');
  assert.deepStrictEqual(json, {
    name: 'package-name',
    version: '1.2.3',
    main: 'package-main-module'
  });
}


{
  // Now verify that module.children contains all the different
  // modules that we've required, and that all of them contain
  // the appropriate children, and so on.

  const visited = new Set();
  const children = module.children.reduce(function red(set, child) {
    if (visited.has(child)) return set;
    visited.add(child);
    let id = path.relative(path.dirname(__dirname), child.id);
    id = id.replace(backslash, '/');
    set[id] = child.children.reduce(red, {});
    return set;
  }, {});

  assert.deepStrictEqual(children, {
    'common/index.js': {
      'common/tmpdir.js': {}
    },
    'fixtures/not-main-module.js': {},
    'fixtures/a.js': {
      'fixtures/b/c.js': {
        'fixtures/b/d.js': {},
        'fixtures/b/package/index.js': {}
      }
    },
    'fixtures/foo': {},
    'fixtures/nested-index/one/index.js': {
      'fixtures/nested-index/one/hello.js': {}
    },
    'fixtures/nested-index/two/index.js': {
      'fixtures/nested-index/two/hello.js': {}
    },
    'fixtures/nested-index/three.js': {},
    'fixtures/nested-index/three/index.js': {},
    'fixtures/packages/index/index.js': {},
    'fixtures/packages/main/package-main-module.js': {},
    'fixtures/packages/main-index/package-main-module/index.js': {},
    'fixtures/packages/missing-main/index.js': {},
    'fixtures/cycles/root.js': {
      'fixtures/cycles/folder/foo.js': {}
    },
    'fixtures/node_modules/foo.js': {
      'fixtures/node_modules/baz/index.js': {
        'fixtures/node_modules/bar.js': {},
        'fixtures/node_modules/baz/node_modules/asdf.js': {}
      }
    },
    'fixtures/path.js': {},
    'fixtures/registerExt.test': {},
    'fixtures/registerExt.hello.world': {},
    'fixtures/registerExt2.test': {},
    'fixtures/module-load-order/file1': {},
    'fixtures/module-load-order/file2.js': {},
    'fixtures/module-load-order/file4.reg': {},
    'fixtures/module-load-order/file5.reg2': {},
    'fixtures/module-load-order/file6/index.js': {},
    'fixtures/module-load-order/file8/index.reg': {},
    'fixtures/module-load-order/file9/index.reg2': {},
    'fixtures/module-require/parent/index.js': {
      'fixtures/module-require/child/index.js': {
        'fixtures/module-require/child/node_modules/target.js': {}
      }
    },
    'fixtures/packages/main/package.json': {}
  });
}


process.on('exit', function() {
  assert.ok(a.A instanceof Function);
  assert.strictEqual(a.A(), 'A done');

  assert.ok(a.C instanceof Function);
  assert.strictEqual(a.C(), 'C done');

  assert.ok(a.D instanceof Function);
  assert.strictEqual(a.D(), 'D done');

  assert.ok(d.D instanceof Function);
  assert.strictEqual(d.D(), 'D done');

  assert.ok(d2.D instanceof Function);
  assert.strictEqual(d2.D(), 'D done');

  assert.strictEqual(errorThrown, true);

  console.log('exit');
});


// Loading files with a byte order marker.
// See https://github.com/nodejs/node-v0.x-archive/issues/1440.
assert.strictEqual(require('../fixtures/utf8-bom.js'), 42);
assert.strictEqual(require('../fixtures/utf8-bom.json'), 42);

// Loading files with BOM + shebang.
// See https://github.com/nodejs/node/issues/27767
assert.throws(() => {
  require('../fixtures/utf8-bom-shebang-shebang.js');
}, { name: 'SyntaxError' });
assert.strictEqual(require('../fixtures/utf8-shebang-bom.js'), 42);

// Error on the first line of a module should
// have the correct line number
assert.throws(function() {
  require('../fixtures/test-error-first-line-offset.js');
}, function(err) {
  return /test-error-first-line-offset\.js:1:/.test(err.stack);
}, 'Expected appearance of proper offset in Error stack');
