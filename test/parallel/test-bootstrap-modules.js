'use strict';

// This list must be computed before we require any builtins to
// to eliminate the noise.
const list = process.moduleLoadList.slice();

const common = require('../common');
const assert = require('assert');
const { inspect } = require('util');

const preExecIndex =
  list.findIndex((i) => i.includes('pre_execution'));
const actual = {
  beforePreExec: new Set(list.slice(0, preExecIndex)),
  atRunTime: new Set(list.slice(preExecIndex)),
};

// Currently, we don't add additional builtins to worker snapshots.
// So for worker snapshots we'll just concatenate the two. Once we
// add more builtins to worker snapshots, we should also distinguish
// the two stages for them.
const expected = {};

expected.beforePreExec = new Set([
  'Internal Binding builtins',
  'Internal Binding encoding_binding',
  'Internal Binding modules',
  'Internal Binding errors',
  'Internal Binding util',
  'NativeModule internal/errors',
  'Internal Binding config',
  'Internal Binding timers',
  'Internal Binding async_wrap',
  'Internal Binding task_queue',
  'Internal Binding symbols',
  'NativeModule internal/async_hooks',
  'Internal Binding constants',
  'Internal Binding types',
  'NativeModule internal/util',
  'NativeModule internal/util/types',
  'NativeModule internal/validators',
  'NativeModule internal/linkedlist',
  'NativeModule internal/priority_queue',
  'NativeModule internal/assert',
  'NativeModule internal/util/inspect',
  'NativeModule internal/util/debuglog',
  'NativeModule internal/timers',
  'NativeModule events',
  'Internal Binding buffer',
  'Internal Binding string_decoder',
  'NativeModule internal/buffer',
  'NativeModule buffer',
  'Internal Binding messaging',
  'NativeModule internal/worker/js_transferable',
  'Internal Binding process_methods',
  'NativeModule internal/process/per_thread',
  'Internal Binding credentials',
  'NativeModule internal/process/promises',
  'NativeModule internal/fixed_queue',
  'NativeModule async_hooks',
  'NativeModule internal/process/task_queues',
  'NativeModule timers',
  'Internal Binding trace_events',
  'NativeModule internal/constants',
  'NativeModule path',
  'NativeModule internal/process/execution',
  'NativeModule internal/process/permission',
  'NativeModule internal/process/warning',
  'NativeModule internal/console/constructor',
  'NativeModule internal/console/global',
  'NativeModule internal/querystring',
  'NativeModule querystring',
  'Internal Binding url',
  'Internal Binding blob',
  'NativeModule internal/url',
  'NativeModule util',
  'NativeModule internal/webidl',
  'Internal Binding performance',
  'Internal Binding permission',
  'NativeModule internal/perf/utils',
  'NativeModule internal/event_target',
  'Internal Binding mksnapshot',
  'NativeModule internal/v8/startup_snapshot',
  'NativeModule internal/process/signal',
  'Internal Binding fs',
  'NativeModule internal/encoding',
  'NativeModule internal/webstreams/util',
  'NativeModule internal/webstreams/queuingstrategies',
  'NativeModule internal/blob',
  'NativeModule internal/fs/utils',
  'NativeModule fs',
  'Internal Binding options',
  'NativeModule internal/options',
  'NativeModule internal/source_map/source_map_cache',
  'Internal Binding contextify',
  'NativeModule internal/vm',
  'NativeModule internal/modules/helpers',
  'NativeModule internal/modules/package_json_reader',
  'Internal Binding module_wrap',
  'NativeModule internal/modules/cjs/loader',
  'NativeModule diagnostics_channel',
  'Internal Binding wasm_web_api',
  'NativeModule internal/events/abort_listener',
]);

expected.atRunTime = new Set([
  'Internal Binding worker',
  'NativeModule internal/modules/run_main',
  'NativeModule internal/net',
  'NativeModule internal/dns/utils',
  'NativeModule internal/process/pre_execution',
  'NativeModule internal/modules/esm/utils',
]);

if (common.isMainThread) {
  [
    'NativeModule internal/idna',
    'NativeModule url',
  ].forEach(expected.beforePreExec.add.bind(expected.beforePreExec));
} else {  // Worker.
  [
    'NativeModule diagnostics_channel',
    'NativeModule internal/abort_controller',
    'NativeModule internal/error_serdes',
    'NativeModule internal/perf/event_loop_utilization',
    'NativeModule internal/process/worker_thread_only',
    'NativeModule internal/streams/add-abort-signal',
    'NativeModule internal/streams/compose',
    'NativeModule internal/streams/destroy',
    'NativeModule internal/streams/duplex',
    'NativeModule internal/streams/duplexpair',
    'NativeModule internal/streams/end-of-stream',
    'NativeModule internal/streams/from',
    'NativeModule internal/streams/legacy',
    'NativeModule internal/streams/operators',
    'NativeModule internal/streams/passthrough',
    'NativeModule internal/streams/pipeline',
    'NativeModule internal/streams/readable',
    'NativeModule internal/streams/state',
    'NativeModule internal/streams/transform',
    'NativeModule internal/streams/utils',
    'NativeModule internal/streams/writable',
    'NativeModule internal/worker',
    'NativeModule internal/worker/io',
    'NativeModule internal/worker/messaging',
    'NativeModule stream',
    'NativeModule stream/promises',
    'NativeModule string_decoder',
    'NativeModule worker_threads',
  ].forEach(expected.atRunTime.add.bind(expected.atRunTime));
  // For now we'll concatenate the two stages for workers. We prefer
  // atRunTime here because that's what currently happens for these.
}

if (common.isWindows) {
  // On Windows fs needs SideEffectFreeRegExpPrototypeExec which uses vm.
  expected.atRunTime.add('NativeModule vm');
}

if (common.hasIntl) {
  expected.beforePreExec.add('Internal Binding icu');
}

if (process.features.inspector) {
  expected.beforePreExec.add('Internal Binding inspector');
  expected.beforePreExec.add('NativeModule internal/util/inspector');
  expected.atRunTime.add('NativeModule internal/inspector_async_hook');
}

// This is loaded if the test is run with NODE_V8_COVERAGE.
if (process.env.NODE_V8_COVERAGE) {
  expected.atRunTime.add('Internal Binding profiler');
}

// Accumulate all the errors and print them at the end instead of throwing
// immediately which makes it harder to update the test.
const errorLogs = [];
function err(message) {
  if (typeof message === 'string') {
    errorLogs.push(message);
  } else {
    // Show the items in individual lines for easier copy-pasting.
    errorLogs.push(inspect(message, { compact: false }));
  }
}

if (common.isMainThread) {
  const missing = expected.beforePreExec.difference(actual.beforePreExec);
  const extra = actual.beforePreExec.difference(expected.beforePreExec);
  if (missing.size !== 0) {
    err('These builtins are now no longer loaded before pre-execution.');
    err('If this is intentional, remove them from `expected.beforePreExec`.');
    err('\n--- These could be removed from expected.beforePreExec ---');
    err([...missing].sort());
    err('');
  }
  if (extra.size !== 0) {
    err('These builtins are now unexpectedly loaded before pre-execution.');
    err('If this is intentional, add them to `expected.beforePreExec`.');
    err('\n# Note: loading more builtins before pre-execution can lead to ' +
        'startup performance regression or invalid snapshots.');
    err('- Consider lazy loading builtins that are not used universally.');
    err('- Make sure that the builtins do not access environment dependent ' +
        'states e.g. command line arguments or environment variables ' +
        'during loading.');
    err('- When in doubt, ask @nodejs/startup.');
    err('\n--- These could be added to expected.beforePreExec ---');
    err([...extra].sort());
    err('');
  }
}

if (!common.isMainThread) {
  // For workers, just merge beforePreExec into atRunTime for now.
  // When we start adding modules to the worker snapshot, this branch
  // can be removed and  we can just remove the common.isMainThread
  // conditions.
  expected.beforePreExec.forEach(expected.atRunTime.add.bind(expected.atRunTime));
  actual.beforePreExec.forEach(actual.atRunTime.add.bind(actual.atRunTime));
}

{
  const missing = expected.atRunTime.difference(actual.atRunTime);
  const extra = actual.atRunTime.difference(expected.atRunTime);
  if (missing.size !== 0) {
    err('These builtins are now no longer loaded at run time.');
    err('If this is intentional, remove them from `expected.atRunTime`.');
    err('\n--- These could be removed from expected.atRunTime ---');
    err([...missing].sort());
    err('');
  }
  if (extra.size !== 0) {
    err('These builtins are now unexpectedly loaded at run time.');
    err('If this is intentional, add them to `expected.atRunTime`.');
    err('\n# Note: loading more builtins at run time can lead to ' +
        'startup performance regression.');
    err('- Consider lazy loading builtins that are not used universally.');
    err('\n--- These could be added to expected.atRunTime ---');
    err([...extra].sort());
    err('');
  }
}

assert.strictEqual(errorLogs.length, 0, errorLogs.join('\n'));
