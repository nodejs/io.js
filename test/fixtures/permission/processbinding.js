const common = require('../../common');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

const assert = require('assert');

{
  assert.throws(() => {
    process.binding();
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
  }));
}

{
  assert.throws(() => {
    process.binding('fs');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
  }));
}
