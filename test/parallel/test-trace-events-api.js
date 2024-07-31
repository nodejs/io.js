// Flags: --expose-gc --no-warnings --expose-internals
'use strict';

const common = require('../common');
common.skipIfWorker(); // https://github.com/nodejs/node/issues/22767

try {
  require('trace_events');
} catch {
  common.skip('missing trace events');
}

const assert = require('assert');
const cp = require('child_process');
const fs = require('fs');
const tmpdir = require('../common/tmpdir');
const {
  createTracing,
  getEnabledCategories
} = require('trace_events');

function getEnabledCategoriesFromCommandLine() {
  const indexOfCatFlag = process.execArgv.indexOf('--trace-event-categories');
  if (indexOfCatFlag === -1) {
    return undefined;
  }
  return process.execArgv[indexOfCatFlag + 1];
}

const isChild = process.argv[2] === 'child';
const enabledCategories = getEnabledCategoriesFromCommandLine();

assert.strictEqual(getEnabledCategories(), enabledCategories);
for (const i of [1, 'foo', true, false, null, undefined]) {
  assert.throws(() => createTracing(i), {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError'
  });
  assert.throws(() => createTracing({ categories: i }), {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError'
  });
}

assert.throws(
  () => createTracing({ categories: [] }),
  {
    code: 'ERR_TRACE_EVENTS_CATEGORY_REQUIRED',
    name: 'TypeError'
  }
);

const tracing = createTracing({ categories: [ 'node.perf' ] });

assert.strictEqual(tracing.categories, 'node.perf');
assert.strictEqual(tracing.enabled, false);

assert.strictEqual(getEnabledCategories(), enabledCategories);
tracing.enable();
tracing.enable();  // Purposefully enable twice to test calling twice
assert.strictEqual(tracing.enabled, true);

assert.strictEqual(getEnabledCategories(),
                   [
                     ...[enabledCategories].filter((_) => !!_), 'node.perf',
                   ].join(','));

tracing.disable();
assert.strictEqual(tracing.enabled, false);

const tracing2 = createTracing({ categories: [ 'foo' ] });
tracing2.enable();
assert.strictEqual(getEnabledCategories(), 'foo');

tracing2.disable();
tracing2.disable();  // Purposefully disable twice to test calling twice
assert.strictEqual(getEnabledCategories(), enabledCategories);

if (isChild) {
  const { internalBinding } = require('internal/test/binding');

  const {
    trace: {
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN: kBeforeEvent,
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_END: kEndEvent,
    }
  } = internalBinding('constants');

  const { trace } = internalBinding('trace_events');

  tracing.enable();

  trace(kBeforeEvent, 'foo', 'test1', 0, 'test');
  setTimeout(() => {
    trace(kEndEvent, 'foo', 'test1');
  }, 1);
} else {
  // Test that enabled tracing references do not get garbage collected
  // until after they are disabled.
  {
    {
      let tracing3 = createTracing({ categories: [ 'abc' ] });
      tracing3.enable();
      assert.strictEqual(getEnabledCategories(), 'abc');
      tracing3 = undefined;
    }
    global.gc();
    assert.strictEqual(getEnabledCategories(), 'abc');
    // Not able to disable the thing after this point, however.
  }

  {
    common.expectWarning(
      'Warning',
      'Possible trace_events memory leak detected. There are more than ' +
      '10 enabled Tracing objects.');
    for (let n = 0; n < 10; n++) {
      const tracing = createTracing({ categories: [ `a${n}` ] });
      tracing.enable();
    }
  }

  testApiInChildProcess(['--trace-event-categories', 'foo'], () => {
    testApiInChildProcess(['--trace-event-categories', 'foo']);
  });
}

function testApiInChildProcess(execArgs, cb) {
  tmpdir.refresh();
  // Save the current directory so we can chdir back to it later
  const parentDir = process.cwd();
  process.chdir(tmpdir.path);

  const expectedBegins = [{ cat: 'foo', name: 'test1' }];
  const expectedEnds = [{ cat: 'foo', name: 'test1' }];

  const proc = cp.fork(__filename,
                       ['child'],
                       {
                         execArgv: [
                           '--expose-gc',
                           '--expose-internals',
                           '--no-warnings',
                           ...execArgs,
                         ]
                       });

  proc.once('exit', common.mustCall(() => {
    const file = tmpdir.resolve('node_trace.1.log');

    assert(fs.existsSync(file));
    fs.readFile(file, common.mustSucceed((data) => {
      const traces = JSON.parse(data.toString()).traceEvents
        .filter((trace) => trace.cat !== '__metadata');

      assert.strictEqual(
        traces.length,
        expectedBegins.length + expectedEnds.length);
      for (const trace of traces) {
        assert.strictEqual(trace.pid, proc.pid);
        switch (trace.ph) {
          case 'b': {
            const expectedBegin = expectedBegins.shift();
            assert.strictEqual(trace.cat, expectedBegin.cat);
            assert.strictEqual(trace.name, expectedBegin.name);
            break;
          }
          case 'e': {
            const expectedEnd = expectedEnds.shift();
            assert.strictEqual(trace.cat, expectedEnd.cat);
            assert.strictEqual(trace.name, expectedEnd.name);
            break;
          }
          default:
            assert.fail('Unexpected trace event phase');
        }
      }
      process.chdir(parentDir);
      cb && process.nextTick(cb);
    }));
  }));
}
