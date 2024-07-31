// Flags: --experimental-permission --allow-fs-read=*
'use strict';

const common = require('../common');
common.skipIfWorker();
if (!common.hasCrypto)
  common.skip('no crypto');

const assert = require('assert');

{
  assert.throws(() => {
    process.report.writeReport('./secret.txt');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: './secret.txt',
  }));
}

{
  assert.throws(() => {
    process.report.writeReport();
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: process.cwd(),
  }));
}
