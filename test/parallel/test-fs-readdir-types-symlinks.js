'use strict';

// Refs: https://github.com/nodejs/node/issues/52663
const common = require('../common');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

if (!common.canCreateSymLink())
  common.skip('insufficient privileges');

const tmpdir = require('../common/tmpdir');
const readdirDir = tmpdir.path;
// clean up the tmpdir
tmpdir.refresh();

// a/1, a/2
const a = path.join(readdirDir, 'a');
fs.mkdirSync(a);
fs.writeFileSync(path.join(a, '1'), 'irrelevant');
fs.writeFileSync(path.join(a, '2'), 'irrelevant');

// b/1
const b = path.join(readdirDir, 'b');
fs.mkdirSync(b);
fs.writeFileSync(path.join(b, '1'), 'irrelevant');

// b/c -> a
const c = path.join(readdirDir, 'b', 'c');
fs.symlinkSync(a, c, 'dir');

// Just check that the number of entries are the same
assert.strictEqual(
  fs.readdirSync(b, { recursive: true, withFileTypes: true }).length,
  fs.readdirSync(b, { recursive: true, withFileTypes: false }).length
);
