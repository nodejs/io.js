'use strict';

const common = require('../common');
const assert = require('assert');
const { exec } = require('child_process');

// Should throw if -c and -e flags are both passed
['-c', '--check'].forEach(function(checkFlag) {
  ['-e', '--eval'].forEach(function(evalFlag) {
    exec(...common.escapePOSIXShell`"${process.execPath}" ${checkFlag} ${evalFlag} foo`, common.mustCall((err, stdout, stderr) => {
      assert.strictEqual(err instanceof Error, true);
      assert.strictEqual(err.code, 9);
      assert(
        stderr.startsWith(
          `${process.execPath}: either --check or --eval can be used, not both`
        )
      );
    }));
  });
});
