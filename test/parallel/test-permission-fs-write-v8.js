// Flags: --permission --allow-fs-read=*
'use strict';

const common = require('../common');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

if (!common.hasCrypto) {
  common.skip('no crypto');
}

const assert = require('assert');
const v8 = require('v8');
const path = require('path');

{
  assert.throws(() => {
    v8.writeHeapSnapshot('./secret.txt');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath('./secret.txt'),
  }));
}

{
  assert.throws(() => {
    v8.writeHeapSnapshot();
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));
}
