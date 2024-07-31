'use strict';

const common = require('../common');
const tmpdir = require('../common/tmpdir');
const fs = require('fs');

if (common.isSunOS)
  common.skip('SunOS behaves differently');

if (common.isIBMi)
  common.skip('IBMi does not support `fs.watch()`');

tmpdir.refresh();

fs.mkdirSync(tmpdir.resolve('./parent/child'), { recursive: true });

fs.writeFileSync(tmpdir.resolve('./parent/child/test.tmp'), 'test');

const toWatch = tmpdir.resolve('./parent');

const onFileUpdate = common.mustCallAtLeast((eventType, filename) => {
  // We are only checking for the filename to avoid having Windows, Linux and Mac specific assertions
  if (fs.readdirSync(tmpdir.resolve('./parent')).length === 0) {
    watcher.close();
  }
}, 1);

const watcher = fs.watch(toWatch, { recursive: true }, onFileUpdate);

// We must wait a bit `fs.rm()` to let the watcher be set up properly
setTimeout(() => {
  fs.rm(tmpdir.resolve('./parent/child'), { recursive: true }, common.mustCall());
}, common.platformTimeout(500));
