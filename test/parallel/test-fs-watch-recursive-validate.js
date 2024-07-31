'use strict';

const common = require('../common');

if (common.isIBMi)
  common.skip('IBMi does not support `fs.watch()`');

// fs-watch on folders have limited capability in AIX.
// The testcase makes use of folder watching, and causes
// hang. This behavior is documented. Skip this for AIX.

if (common.isAIX)
  common.skip('folder watch capability is limited in AIX.');

const assert = require('assert');
const path = require('path');
const fs = require('fs');

const tmpdir = require('../common/tmpdir');
const testDir = tmpdir.path;
tmpdir.refresh();

(async () => {
  // Handle non-boolean values for options.recursive

  if (!common.isWindows && !common.isMacOS) {
    assert.throws(() => {
      const testsubdir = fs.mkdtempSync(testDir + path.sep);
      fs.watch(testsubdir, { recursive: '1' });
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
    });
  }
})().then(common.mustCall());
