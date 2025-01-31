// Flags: --expose-gc
'use strict';

require('../common');
const { aborted } = require('util');
const {
  match,
  rejects,
  strictEqual,
} = require('assert');
const { getEventListeners } = require('events');
const { inspect } = require('util');

const {
  test,
} = require('node:test');

test('Aborted works when provided a resource', async () => {
  const ac = new AbortController();
  const promise = aborted(ac.signal, {});
  ac.abort();
  await promise;
  strictEqual(ac.signal.aborted, true);
  strictEqual(getEventListeners(ac.signal, 'abort').length, 0);
});

test('Aborted with gc cleanup', async () => {
  // Test aborted with gc cleanup
  const ac = new AbortController();

  const abortedPromise = aborted(ac.signal, {});
  const { promise, resolve } = Promise.withResolvers();

  setImmediate(() => {
    globalThis.gc();
    ac.abort();
    strictEqual(ac.signal.aborted, true);
    strictEqual(getEventListeners(ac.signal, 'abort').length, 0);
    resolve();
  });

  await promise;

  // Ensure that the promise is still pending
  match(inspect(abortedPromise), /<pending>/);
});

test('Fails with error if not provided AbortSignal', async () => {
  await Promise.all([{}, null, undefined, Symbol(), [], 1, 0, 1n, true, false, 'a', () => {}].map((sig) =>
    rejects(aborted(sig, {}), {
      code: 'ERR_INVALID_ARG_TYPE',
    })
  ));
});

test('Fails if not provided a resource', async () => {
  // Fails if not provided a resource
  const ac = new AbortController();
  await Promise.all([null, undefined, 0, 1, 0n, 1n, Symbol(), '', 'a'].map((resource) =>
    rejects(aborted(ac.signal, resource), {
      code: 'ERR_INVALID_ARG_TYPE',
    })
  ));
});

// To allow this case to be more flexibly tested on runtimes that do not have
// child_process.spawn, we lazily require it and skip the test if it is not
// present.

let spawn;
function lazySpawn() {
  if (spawn === undefined) {
    try {
      spawn = require('child_process').spawn;
    } catch {
      // Ignore if spawn does not exist.
    }
  }
  return spawn;
}

test('Does not hang forever', { skip: !lazySpawn() }, async () => {
  const { promise, resolve } = Promise.withResolvers();
  const childProcess = spawn(process.execPath, ['--input-type=module']);
  childProcess.on('exit', (code) => {
    strictEqual(code, 13);
    resolve();
  });
  childProcess.stdin.end(`
    import { aborted } from 'node:util';
    await aborted(new AbortController().signal, {});
  `);
  await promise;
});
