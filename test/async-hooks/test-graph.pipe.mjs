import { mustCall } from '../common/index.mjs';
import initHooks from './init-hooks.mjs';
import verifyGraph from './verify-graph.mjs';
import { spawn } from 'child_process';
import process from 'process';

const hooks = initHooks();

hooks.enable();
const sleep = spawn('sleep', [ '0.1' ]);

sleep
  .on('exit', mustCall(onsleepExit))
  .on('close', mustCall(onsleepClose));

function onsleepExit() {}

function onsleepClose() {}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  verifyGraph(
    hooks,
    [ { type: 'PROCESSWRAP', id: 'process:1', triggerAsyncId: null },
      { type: 'PIPEWRAP', id: 'pipe:1', triggerAsyncId: null },
      { type: 'PIPEWRAP', id: 'pipe:2', triggerAsyncId: null },
      { type: 'PIPEWRAP', id: 'pipe:3', triggerAsyncId: null } ]
  );
}
