// Flags: --expose-internals
'use strict';

const common = require('../common');

common.skipIfInspectorDisabled();

const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  // https://github.com/nodejs/node/issues/22767
  common.skip('This test only works on a main thread');
}

const { internalBinding } = require('internal/test/binding');

const {
  trace: {
    TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN: kBeforeEvent
  }
} = internalBinding('constants');

const { trace } = internalBinding('trace_events');

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

async function test() {
  session.connect();

  const events = [];
  let tracingComplete = false;
  session.on('NodeTracing.dataCollected', (n) => {
    assert.ok(n?.params?.value);
    events.push(...n.params.value);  // append the events.
  });
  session.on('NodeTracing.tracingComplete', () => tracingComplete = true);

  trace(kBeforeEvent, 'foo', 'test1', 0, 'test');

  const traceConfig = { includedCategories: ['foo'] };
  await post('NodeTracing.start', { traceConfig });

  trace(kBeforeEvent, 'foo', 'test2', 0, 'test');
  trace(kBeforeEvent, 'bar', 'test3', 0, 'test');

  await post('NodeTracing.stop', { traceConfig });

  trace(kBeforeEvent, 'foo', 'test4', 0, 'test');
  session.disconnect();

  assert.ok(tracingComplete);

  const marks = events.filter((t) => null !== /foo/.exec(t.cat));
  assert.strictEqual(marks.length, 1);
  assert.strictEqual(marks[0].name, 'test2');
}

test();
