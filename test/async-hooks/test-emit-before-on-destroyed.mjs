// Flags: --expose-internals
import { mustCall } from '../common/index.mjs';
import { strictEqual, match } from 'assert';
import internalAsyncHooks from 'internal/async_hooks';
import { spawn } from 'child_process';
import { argv, execPath } from 'process';

const corruptedMsg = /async hook stack has become corrupted/;
const heartbeatMsg = /heartbeat: still alive/;

const {
  newAsyncId, getDefaultTriggerAsyncId,
  emitInit, emitBefore, emitAfter, emitDestroy
} = internalAsyncHooks;

import initHooks from './init-hooks.mjs';

if (argv[2] === 'child') {
  const hooks = initHooks();
  hooks.enable();

  // Once 'destroy' has been emitted, we can no longer emit 'before'

  // Emitting 'before', 'after' and then 'destroy'
  {
    const asyncId = newAsyncId();
    const triggerId = getDefaultTriggerAsyncId();
    emitInit(asyncId, 'event1', triggerId, {});
    emitBefore(asyncId, triggerId);
    emitAfter(asyncId);
    emitDestroy(asyncId);
  }

  // Emitting 'before' after 'destroy'
  {
    const asyncId = newAsyncId();
    const triggerId = getDefaultTriggerAsyncId();
    emitInit(asyncId, 'event2', triggerId, {});
    emitDestroy(asyncId);

    console.log('heartbeat: still alive');
    emitBefore(asyncId, triggerId);
  }
} else {
  const args = ['--expose-internals']
    .concat(argv.slice(1))
    .concat('child');
  let errData = Buffer.from('');
  let outData = Buffer.from('');

  const child = spawn(execPath, args);
  child.stderr.on('data', (d) => { errData = Buffer.concat([ errData, d ]); });
  child.stdout.on('data', (d) => { outData = Buffer.concat([ outData, d ]); });

  child.on('close', mustCall((code) => {
    strictEqual(code, 1);
    match(outData.toString(), heartbeatMsg,
                 'did not crash until we reached offending line of code ' +
                 `(found ${outData})`);
    match(errData.toString(), corruptedMsg,
                 'printed error contains corrupted message ' +
                 `(found ${errData})`);
  }));
}
