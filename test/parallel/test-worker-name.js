'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');

common.skipIfInspectorDisabled();

const {
  Worker,
  isMainThread,
} = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

const assert = require('assert');

if (isMainThread) {
  const name = 'Hello Thread';
  const expectedTitle = `[worker 1] ${name}`;
  const worker = new Worker(fixtures.path('worker-name.js'), {
    name,
  });
  worker.once('message', common.mustCall((message) => {
    assert.strictEqual(message, expectedTitle);
    worker.postMessage('done');
  }));
}
