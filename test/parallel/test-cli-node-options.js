'use strict';
const common = require('../common');
if (process.config.variables.node_without_node_options)
  common.skip('missing NODE_OPTIONS support');

// Test options specified by env variable.

const assert = require('assert');
const path = require('path');
const exec = require('child_process').execFile;
const { Worker } = require('worker_threads');

const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
const { hasOpenSSL3 } = require('../common/crypto');
tmpdir.refresh();

const printA = path.relative(tmpdir.path, fixtures.path('printA.js'));
const printSpaceA = path.relative(tmpdir.path, fixtures.path('print A.js'));

expectNoWorker(` -r ${printA} `, 'A\nB\n');
expectNoWorker(`-r ${printA}`, 'A\nB\n');
expectNoWorker(`-r ${JSON.stringify(printA)}`, 'A\nB\n');
expectNoWorker(`-r ${JSON.stringify(printSpaceA)}`, 'A\nB\n');
expectNoWorker(`-r ${printA} -r ${printA}`, 'A\nB\n');
expectNoWorker(`   -r ${printA}    -r ${printA}`, 'A\nB\n');
expectNoWorker(`   --require ${printA}    --require ${printA}`, 'A\nB\n');
expect('--no-deprecation', 'B\n');
expect('--no-warnings', 'B\n');
expect('--no_warnings', 'B\n');
expect('--trace-warnings', 'B\n');
expect('--no-extra-info-on-fatal-exception', 'B\n');
expect('--redirect-warnings=_', 'B\n');
expect('--trace-deprecation', 'B\n');
expect('--trace-sync-io', 'B\n');
expectNoWorker('--trace-events-enabled', 'B\n');
expect('--track-heap-objects', 'B\n');
expect('--throw-deprecation',
       /.*DeprecationWarning: Buffer\(\) is deprecated due to security and usability issues.*/,
       'new Buffer(42)',
       true);
expectNoWorker('--zero-fill-buffers', 'B\n');
expectNoWorker('--v8-pool-size=10', 'B\n');
expectNoWorker('--trace-event-categories node', 'B\n');
expectNoWorker(
  // eslint-disable-next-line no-template-curly-in-string
  '--trace-event-file-pattern {pid}-${rotation}.trace_events',
  'B\n'
);
// eslint-disable-next-line no-template-curly-in-string
expectNoWorker('--trace-event-file-pattern {pid}-${rotation}.trace_events ' +
       '--trace-event-categories node.async_hooks', 'B\n');
expect('--unhandled-rejections=none', 'B\n');

if (common.isLinux) {
  expect('--perf-basic-prof', 'B\n');
  expect('--perf-basic-prof-only-functions', 'B\n');

  if (['arm', 'x64'].includes(process.arch)) {
    expect('--perf-prof', 'B\n');
    expect('--perf-prof-unwinding-info', 'B\n');
  }
}

if (common.hasCrypto) {
  expectNoWorker('--use-openssl-ca', 'B\n');
  expectNoWorker('--use-bundled-ca', 'B\n');
  if (!hasOpenSSL3)
    expectNoWorker('--openssl-config=_ossl_cfg', 'B\n');
  if (common.isMacOS) {
    expectNoWorker('--use-system-ca', 'B\n');
  }
}

// V8 options
expect('--abort_on-uncaught_exception', 'B\n');
expect('--disallow-code-generation-from-strings', 'B\n');
expect('--expose-gc', 'B\n');
expect('--jitless', 'B\n');
expect('--max-old-space-size=0', 'B\n');
expect('--max-semi-space-size=0', 'B\n');
expect('--stack-trace-limit=100',
       /(\s*at f \(\[(eval|worker eval)\]:1:\d*\)\r?\n)/,
       '(function f() { f(); })();',
       true);
// Unsupported on arm. See https://crbug.com/v8/8713.
if (!['arm', 'arm64'].includes(process.arch))
  expect('--interpreted-frames-native-stack', 'B\n');

// Workers can't eval as ES Modules. https://github.com/nodejs/node/issues/30682
expectNoWorker('--input-type=module',
               'B\n', 'console.log(await "B")');

function expectNoWorker(opt, want, command, wantsError) {
  expect(opt, want, command, wantsError, false);
}

function expect(
  opt, want, command = 'console.log("B")', wantsError = false, testWorker = true
) {
  const argv = [
    // --perf-basic-prof and --perf-basic-prof-only-functions write to /tmp by default.
    `--perf-basic-prof-path=${tmpdir.path}`,
    '-e',
    command,
  ];
  const opts = {
    cwd: tmpdir.path,
    env: Object.assign({}, process.env, { NODE_OPTIONS: opt }),
    maxBuffer: 1e6,
  };
  if (typeof want === 'string')
    want = new RegExp(want);

  const test = (type) => common.mustCall((err, stdout) => {
    const o = JSON.stringify(opt);
    if (wantsError) {
      assert.ok(err, `${type}: expected error for ${o}`);
      stdout = err.stack;
    } else {
      assert.ifError(err, `${type}: failed for ${o}`);
    }
    if (want.test(stdout)) return;

    assert.fail(
      `${type}: for ${o}, failed to find ${want} in: <\n${stdout}\n>`
    );
  });

  exec(process.execPath, argv, opts, test('child process'));
  if (testWorker)
    workerTest(opts, command, wantsError, test('worker'));
}

async function collectStream(readable) {
  readable.setEncoding('utf8');
  let data = '';
  for await (const chunk of readable) {
    data += chunk;
  }
  return data;
}

function workerTest(opts, command, wantsError, test) {
  let workerError = null;
  const worker = new Worker(command, {
    ...opts,
    execArgv: [],
    eval: true,
    stdout: true,
    stderr: true
  });
  worker.on('error', (err) => {
    workerError = err;
  });
  worker.on('exit', common.mustCall((code) => {
    assert.strictEqual(code, wantsError ? 1 : 0);
    collectStream(worker.stdout).then((stdout) => {
      test(workerError, stdout);
    });
  }));
}
