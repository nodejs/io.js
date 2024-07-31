'use strict';
const common = require('../common');
const assert = require('assert');
const { Worker, parentPort } = require('worker_threads');

// Do not use isMainThread so that this test itself can be run inside a Worker.
if (!process.env.HAS_STARTED_WORKER) {
  process.env.HAS_STARTED_WORKER = 1;
  process.env.NODE_CHANNEL_FD = 'foo'; // Make worker think it has IPC.
  const w = new Worker(__filename);
  w.on('message', common.mustCall((message) => {
    assert.strictEqual(message, true);
  }));
} else {
  {
    const before = process.title;
    const after = before + ' in worker';
    process.title = after;
    assert.strictEqual(process.title, after);
  }

  {
    const before = process.debugPort;
    const after = before + 1;
    process.debugPort = after;
    assert.strictEqual(process.debugPort, after);
  }

  {
    const mask = 0o600;
    assert.throws(() => { process.umask(mask); }, {
      code: 'ERR_WORKER_UNSUPPORTED_OPERATION',
      message: 'Setting process.umask() is not supported in workers'
    });
  }

  const stubs = ['abort', 'chdir', 'send', 'disconnect'];

  if (!common.isWindows) {
    stubs.push('setuid', 'seteuid', 'setgid',
               'setegid', 'setgroups', 'initgroups');
  }

  stubs.forEach((fn) => {
    assert.strictEqual(process[fn].disabled, true);
    assert.throws(() => {
      process[fn]();
    }, {
      code: 'ERR_WORKER_UNSUPPORTED_OPERATION',
      message: `process.${fn}() is not supported in workers`
    });
  });

  ['channel', 'connected'].forEach((fn) => {
    assert.throws(() => {
      process[fn]; // eslint-disable-line no-unused-expressions
    }, {
      code: 'ERR_WORKER_UNSUPPORTED_OPERATION',
      message: `process.${fn} is not supported in workers`
    });
  });

  assert.strictEqual('_startProfilerIdleNotifier' in process, false);
  assert.strictEqual('_stopProfilerIdleNotifier' in process, false);
  assert.strictEqual('_debugProcess' in process, false);
  assert.strictEqual('_debugPause' in process, false);
  assert.strictEqual('_debugEnd' in process, false);

  parentPort.postMessage(true);
}
