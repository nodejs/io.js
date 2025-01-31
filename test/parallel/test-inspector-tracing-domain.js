'use strict';

const common = require('../common');

common.skipIfInspectorDisabled();

const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  // https://github.com/nodejs/node/issues/22767
  common.skip('This test only works on a main thread');
}

const assert = require('assert');
const { Session } = require('inspector');

const session = new Session();

function post(message, data) {
  return new Promise((resolve, reject) => {
    session.post(message, data, (err, result) => {
      if (err)
        reject(new Error(JSON.stringify(err)));
      else
        resolve(result);
    });
  });
}

function generateTrace() {
  return new Promise((resolve) => setTimeout(() => {
    for (let i = 0; i < 1000000; i++) {
      'test' + i; // eslint-disable-line no-unused-expressions
    }
    resolve();
  }, 1));
}

async function test() {
  // This interval ensures Node does not terminate till the test is finished.
  // Inspector session does not keep the node process running (e.g. it does not
  // have async handles on the main event loop). It is debatable whether this
  // should be considered a bug, and there are no plans to fix it atm.
  const interval = setInterval(() => {}, 5000);
  session.connect();
  let traceNotification = null;
  let tracingComplete = false;
  session.on('NodeTracing.dataCollected', (n) => traceNotification = n);
  session.on('NodeTracing.tracingComplete', () => tracingComplete = true);
  const { categories } = await post('NodeTracing.getCategories');
  const expectedCategories = [
    'node',
    'node.async_hooks',
    'node.bootstrap',
    'node.console',
    'node.dns.native',
    'node.environment',
    'node.fs.async',
    'node.fs.sync',
    'node.fs_dir.async',
    'node.fs_dir.sync',
    'node.http',
    'node.net.native',
    'node.perf',
    'node.perf.timerify',
    'node.perf.usertiming',
    'node.promises.rejections',
    'node.threadpoolwork.async',
    'node.threadpoolwork.sync',
    'node.vm.script',
    'v8',
  ].sort();
  assert.ok(categories.length === expectedCategories.length);
  categories.forEach((category, index) => {
    const value = expectedCategories[index];
    assert.ok(category === value, `${category} is out of order, expect ${value}`);
  });

  const traceConfig = { includedCategories: ['v8'] };
  await post('NodeTracing.start', { traceConfig });

  for (let i = 0; i < 5; i++)
    await generateTrace();
  JSON.stringify(await post('NodeTracing.stop', { traceConfig }));
  session.disconnect();
  assert(traceNotification.params.value.length > 0);
  assert(tracingComplete);
  clearInterval(interval);
  console.log('Success');
}

test().then(common.mustCall());
